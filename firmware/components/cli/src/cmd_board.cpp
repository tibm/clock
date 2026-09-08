// The `board` group -- the bus, before there is a `board` AO.   [FIRMWARE.md §9.3, §12.1]
//
// Milestone 1 is "the board is alive and safe", and the first question in it is whether
// anything answers on I2C at all.  That question does not need the AO (§6.5): it needs the
// bus, which `hal::i2c` now is.  So these rows read the HAL directly, exactly as cmd_sensor
// already does, and move behind `board`'s Command surface when the AO lands -- the group
// name and the verbs are chosen now so that move is a re-implementation, not a rename.
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "clk/board.hpp"
#include "clk/cli/registry.hpp"
#include "clk/hal/hal.hpp"

namespace clk::cli {
namespace {

// esp32.md's address table.  Naming what answered is most of the value of a scan: `0x20`
// is a number, `0x20 MCP23017 (expander)` is a board that is working.
struct Known {
    uint8_t addr;
    const char* what;
};
constexpr Known kKnown[] = {
    {0x20, "MCP23017 expander (main board)"}, {0x29, "TSL2591 light (sensor board)"},
    {0x4A, "BNO085 IMU (sensor board)"},      {0x6C, "TAS5760M amp (main board)"},
    {0x77, "BME688 env (sensor board)"},
};

const char* describe(uint8_t addr) noexcept {
    for (auto const& k : kKnown) {
        if (k.addr == addr) return k.what;
    }
    return "unknown -- not on esp32.md's address map";
}

// Accepts 0x20 or 32.  strtol's base-0 does both, and a bench types both.
bool parse_u8(const char* s, uint8_t& out) noexcept {
    if (!s || !*s) return false;
    char* end = nullptr;
    const long v = std::strtol(s, &end, 0);
    if (end == s || *end != '\0' || v < 0 || v > 0xFF) return false;
    out = static_cast<uint8_t>(v);
    return true;
}

Status cmd_i2c_scan(Args const&, Sink& out) {
    uint8_t found[16]{};
    const auto n = hal::i2c::scan(found, sizeof found);
    if (!n.ok()) return n.st;
    if (n.v == 0) {
        // Worth saying rather than printing an empty list: on this board the expander is
        // soldered down, so a silent bus is a fault (SDA/SCL, the 4.7k pull-ups, or 3V3 at
        // U13) and not a "nothing plugged in".
        out.line("no device answered 0x08-0x77");
        out.line("  U13 is on the board, so an empty bus is a fault, not an empty socket:");
        out.line("  check +3V3 at U13, R95/R96, and SDA/SCL continuity to IO8/IO9");
        return Status::Ok;
    }
    const std::size_t shown = n.v < sizeof found ? n.v : sizeof found;
    for (std::size_t i = 0; i < shown; ++i) {
        out.printf("0x%02X  %s", found[i], describe(found[i]));
    }
    if (n.v > shown) out.printf("... and %u more", static_cast<unsigned>(n.v - shown));
    out.printf("%u device%s", static_cast<unsigned>(n.v), n.v == 1 ? "" : "s");
    return Status::Ok;
}

Status cmd_i2c_read(Args const& a, Sink& out) {
    uint8_t addr = 0, reg = 0;
    if (!parse_u8(a.arg(0), addr) || !parse_u8(a.arg(1), reg)) return Status::BadArg;
    const auto v = hal::i2c::read_reg(addr, reg);
    if (!v.ok()) return v.st;
    out.printf("0x%02X[0x%02X] = 0x%02X", addr, reg, v.v);
    return Status::Ok;
}

// Unsafe, and not as a formality: this is the one command here that can drive a pin.  A
// stray write to the expander's OLATA can unmute the amp or pull STEP_STBY high.
Status cmd_i2c_write(Args const& a, Sink& out) {
    uint8_t addr = 0, reg = 0, val = 0;
    if (!parse_u8(a.arg(0), addr) || !parse_u8(a.arg(1), reg) || !parse_u8(a.arg(2), val)) {
        return Status::BadArg;
    }
    const Status st = hal::i2c::write_reg(addr, reg, val);
    if (st != Status::Ok) return st;
    out.printf("0x%02X[0x%02X] <- 0x%02X", addr, reg, val);
    return Status::Ok;
}

constexpr CmdSpec kRows[] = {
    {"board", "i2c", "scan", "", "who answers on the shared bus", ReleaseOk, cmd_i2c_scan},
    {"board", "i2c", "read", "<addr> <reg>", "one register, hex or decimal", None, cmd_i2c_read},
    {"board", "i2c", "write", "<addr> <reg> <val>", "one register -- drives real pins", Unsafe,
     cmd_i2c_write},
};

}  // namespace

extern const CmdTable kTableBoard{kRows, sizeof(kRows) / sizeof(kRows[0])};

}  // namespace clk::cli
