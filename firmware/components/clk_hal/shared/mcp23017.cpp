// MCP23017 driver.  One copy, both backends.            [FIRMWARE.md §6.5, R-BOARD-1/4]
#include "clk/hal/mcp23017.hpp"

#include "clk/board.hpp"
#include "clk/log.hpp"

namespace clk::hal::mcp23017 {
namespace {

// BANK = 0 (POR), so the ports interleave and these are the addresses that matter.
enum Reg : uint8_t {
    IODIRA = 0x00,
    IODIRB = 0x01,
    IOCON = 0x0A,
    GPPUA = 0x0C,
    GPPUB = 0x0D,
    GPIOA = 0x12,
    GPIOB = 0x13,
    OLATA = 0x14,
    OLATB = 0x15,
};

constexpr uint8_t kIoconMirror = 0x40;  // INTA and INTB both reflect both banks

// Sig order IS pin order (hal.hpp): 0-3 are GPA0-3, 4-11 are GPB0-7.  Asserted rather than
// trusted, because a reordered enum would silently move SPK_SD onto CELL_TEST's pin.
constexpr std::size_t kPortASigs = 4;
static_assert(static_cast<std::size_t>(expander::Sig::RadioOff) == 3);
static_assert(static_cast<std::size_t>(expander::Sig::PdPg) == kPortASigs);
static_assert(expander::kSigCount == kPortASigs + 8);

constexpr bool port_of(expander::Sig s) noexcept {
    return static_cast<std::size_t>(s) >= kPortASigs;  // true = port B
}
constexpr uint8_t bit_of(expander::Sig s) noexcept {
    const auto i = static_cast<std::size_t>(s);
    return static_cast<uint8_t>(1u << (port_of(s) ? i - kPortASigs : i));
}

// IODIR: 1 = input.  Derived from is_output() so the direction mask cannot drift away from
// the enum it describes -- and GPA4-7, which no signal claims, stay inputs.
constexpr uint8_t dir_mask(bool port_b) noexcept {
    uint8_t m = 0xFF;
    const std::size_t first = port_b ? kPortASigs : 0;
    const std::size_t n = port_b ? 8 : kPortASigs;
    for (std::size_t i = 0; i < n; ++i) {
        const auto s = static_cast<expander::Sig>(first + i);
        if (expander::is_output(s)) m = static_cast<uint8_t>(m & ~(1u << i));
    }
    return m;
}

// A pull-up on every input, which is the same mask.  Three reasons it is not over-reach:
// PD_PG / CHRG / FAULT / SPK_FAULT are open-drain and need one; GPB3 `ALS_INT` is
// R-BOARD-4, whose whole point is that its only other pull-up lives on a daughterboard that
// is often unplugged; and GPA4-7 are unconnected pads that would otherwise float and cost
// supply current.
constexpr uint8_t kIodirA = dir_mask(false);
constexpr uint8_t kIodirB = dir_mask(true);
static_assert(kIodirA == 0xF8, "GPA0-2 out (SPK_SD, STEP_STBY, BOOST12_EN), GPA3-7 in");
static_assert(kIodirB == 0x4F, "GPB4/5/7 out (FULLCHG_EN, VBAT_DIV_EN, CELL_TEST)");

bool g_ready = false;
uint8_t g_olat[2] = {0x00, 0x00};

Status write(uint8_t reg, uint8_t val) noexcept { return i2c::write_reg(kAddr, reg, val); }

}  // namespace

Status init() noexcept {
    if (g_ready) return Status::Ok;
    if (!board::present(board::Dev::Expander)) return Status::NotPresent;

    // IOCON FIRST -- R-BOARD-1.  INTA and INTB are tied to one line on the board and are
    // push-pull active-low by default, so per-bank interrupts with MIRROR = 0 would put two
    // outputs in contention.  POR is safe because GPINTEN = 0, and the hazard window is
    // exactly "interrupts enabled before MIRROR is set" -- so this write leads.  (IOCON.ODR
    // = 1 is the equally valid alternative; R94 is fitted as the pull-up for it.)
    if (const Status st = write(IOCON, kIoconMirror); st != Status::Ok) {
        if (st != Status::NotPresent) CLK_LOGW(drv_exp, "IOCON write: %s", clk::name(st));
        return st;
    }

    // Then the output latches, and only THEN the directions.  A pin adopts OLAT the instant
    // IODIR makes it an output, so setting direction first would drive whatever the latch
    // happened to hold.  POR OLAT is 0x00 and every one of our outputs is idle-low --
    // SPK_SD (amp muted), STEP_STBY (coils dead), BOOST12_EN (12 V off), CELL_TEST (R-BOARD-2)
    // -- so parking them explicitly is both the safe state and the documented one.
    g_olat[0] = g_olat[1] = 0x00;
    Status st = write(OLATA, g_olat[0]);
    if (st == Status::Ok) st = write(OLATB, g_olat[1]);
    if (st == Status::Ok) st = write(GPPUA, kIodirA);  // R-BOARD-4 is GPB3, inside kIodirB
    if (st == Status::Ok) st = write(GPPUB, kIodirB);
    if (st == Status::Ok) st = write(IODIRA, kIodirA);
    if (st == Status::Ok) st = write(IODIRB, kIodirB);
    if (st != Status::Ok) {
        if (st != Status::NotPresent) CLK_LOGW(drv_exp, "config: %s", clk::name(st));
        return st;
    }

    // Read one back.  A chip that ACKs its address but is not actually configured is a real
    // failure mode on a shared bus (a stray write, a brown-out mid-config), and it presents
    // later as pins that will not drive -- much harder to read than a line here.
    const auto check = i2c::read_reg(kAddr, IODIRA);
    if (!check.ok()) return check.st;
    if (check.v != kIodirA) {
        CLK_LOGW(drv_exp, "IODIRA reads 0x%02X, wrote 0x%02X", check.v, kIodirA);
        return Status::Failed;
    }

    g_ready = true;
    CLK_LOGI(drv_exp, "MCP23017 at 0x%02X: IODIR %02X/%02X, IOCON.MIRROR set", kAddr, kIodirA,
             kIodirB);
    return Status::Ok;
}

Result<bool> get(expander::Sig s) noexcept {
    if (static_cast<std::size_t>(s) >= expander::kSigCount)
        return Result<bool>::bad(Status::BadArg);
    if (const Status st = init(); st != Status::Ok) return Result<bool>::bad(st);
    // GPIO, not OLAT, even for outputs: GPIO is what the pin is actually at, and an output
    // that cannot reach its latch (shorted, or fighting something) is worth being able to see.
    const auto r = i2c::read_reg(kAddr, port_of(s) ? GPIOB : GPIOA);
    if (!r.ok()) return Result<bool>::bad(r.st);
    return Result<bool>::good((r.v & bit_of(s)) != 0);
}

Status set(expander::Sig s, bool level) noexcept {
    if (static_cast<std::size_t>(s) >= expander::kSigCount) return Status::BadArg;
    // An input is the outside world's to drive.  Saying so beats accepting a write the
    // hardware will ignore.
    if (!expander::is_output(s)) return Status::BadArg;
    if (const Status st = init(); st != Status::Ok) return st;

    const std::size_t port = port_of(s) ? 1 : 0;
    const uint8_t before = g_olat[port];
    const uint8_t after = level ? static_cast<uint8_t>(before | bit_of(s))
                                : static_cast<uint8_t>(before & ~bit_of(s));
    if (after == before) return Status::Ok;
    // The shadow is what makes this a read-free read-modify-write.  It is only ever wrong if
    // something else writes this chip, which nothing does -- and init()'s read-back is the
    // check on that assumption.
    const Status st = write(port ? OLATB : OLATA, after);
    if (st != Status::Ok) return st;
    g_olat[port] = after;
    return Status::Ok;
}

void forget() noexcept {
    g_ready = false;
    g_olat[0] = g_olat[1] = 0x00;
}

}  // namespace clk::hal::mcp23017
