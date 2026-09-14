// The charger, the cell, and what the ADC is really looking at.  [FIRMWARE.md §6.8, R-BOARD-2/3]
//
// In shared/ and not in either backend, because there is nothing platform-specific left in it:
// every line below is arithmetic over `hal::adc` and `hal::expander`, both of which are real on
// target and modelled on the host.  Milestone 1's last item was a NotPresent stub on ESP sitting
// next to a complete host implementation, which is two copies of the SoC endpoints, two copies of
// three open-drain inversions, and two chances to disagree about R-BOARD-3.  One copy instead:
// the host tests exercise the code that ships (§11.2), exactly as they do for the MCP23017.
#include "clk/hal/hal.hpp"
#include "clk/log.hpp"

namespace clk::hal::power {
namespace {

using expander::Sig;

// The `Q2` body-diode drop that the whole cell/no-cell discriminator turns on, and the window
// either side of it.  With `CELL_TEST` asserted the tap sees the holder alone: unchanged if a
// cell is holding it up, one diode below the BAT node if the holder is empty.  240 mV splits
// "unchanged" from "a diode" with room on both sides -- the drop is 300-400 mV and a cell that
// is genuinely in the holder moves by a few tens of millivolts at most.
constexpr int16_t kCellStepMv = 240;

// `Q2` switches the holder, not the divider, so read_mv's own 30 ms is settling the wrong node.
// The holder+ net is a rail with the BAT-node bulk on the other side of `Q2`; 30 ms is the same
// order as the divider's five time constants and there is no reason to be quicker about a
// command a human typed.
constexpr uint32_t kCellSettleMs = 30;

// The three status pins are open-drain with pull-ups (power_values.md: "active-low
// power-good"), so a LOW pin is an ASSERTED signal and the level is the opposite of the meaning.
// Getting this backwards reads as a clock that is certain it is on battery while it charges, so
// it is one function rather than three inversions spread through the file.
Result<bool> asserted(Sig s) noexcept {
    const auto r = expander::get(s);
    return r.ok() ? Result<bool>::good(!r.v) : Result<bool>::bad(r.st);
}

uint8_t soc_from(uint16_t mv) noexcept {
    const int32_t pct =
        (static_cast<int32_t>(mv) - kVbatEmptyMv) * 100 / (kVbatFullMv - kVbatEmptyMv);
    return static_cast<uint8_t>(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
}

}  // namespace

Result<State> read() noexcept {
    // The expander first, and its absence is the whole answer rather than half of one: there is
    // no divider leg to switch without it, so there is no cell voltage to be had either.  Doing
    // it in this order also makes the Status the same on both backends (D16).
    const auto pg = asserted(Sig::PdPg);
    if (!pg.ok()) return Result<State>::bad(pg.st);
    const auto chrg = asserted(Sig::Chrg);
    if (!chrg.ok()) return Result<State>::bad(chrg.st);
    const auto flt = asserted(Sig::Fault);
    if (!flt.ok()) return Result<State>::bad(flt.st);

    // read_mv owns the `VBAT_DIV_EN` leg: it switches it in, waits out C110, reads, and puts it
    // back the way it found it.  It has to be that way round rather than left on -- the divider
    // is a permanent 20 uA drain on a backup cell (power.md) -- and it has to be here rather
    // than in this function, because `sensor homing` shares the ADC and neither caller should
    // have to know about the other's FET.
    const auto mv = adc::read_mv(adc::Ch::Vbat);
    if (!mv.ok()) return Result<State>::bad(mv.st);

    State s{};
    s.vbat_mv = mv.v;
    s.plugged = pg.v;
    s.charging = chrg.v;
    s.fault = flt.v;
    // R-BOARD-3, and the one decision in this file: on battery the tap is across the cell, and
    // plugged it is the BAT node.  An SoC computed off the BAT node would be the charger's
    // output percentage-ised -- a number that is always plausible and sometimes 600 mV wrong.
    s.src = s.plugged ? VbatSrc::BatNode : VbatSrc::Cell;
    s.soc_pct = s.src == VbatSrc::Cell ? soc_from(s.vbat_mv) : kSocUnknown;
    return Result<State>::good(s);
}

Result<CellTest> cell_test() noexcept {
    // R-BOARD-2.  A FRESH read, not read()'s: the invariant is about the state of the wall at
    // the moment `Q2` goes off, and a cached `plugged` from a hundred milliseconds ago is
    // exactly the thing that turns into an unbounded reboot loop when somebody pulls the brick.
    const auto pg = asserted(Sig::PdPg);
    if (!pg.ok()) return Result<CellTest>::bad(pg.st);
    if (!pg.v) {
        CLK_LOGW(sys, "cell test refused: on battery, CELL_TEST would cut the rails (R-BOARD-2)");
        return Result<CellTest>::bad(Status::Denied);
    }
    const auto chrg = asserted(Sig::Chrg);
    if (!chrg.ok()) return Result<CellTest>::bad(chrg.st);

    // Make sure `Q2` really is conducting before calling the first reading "rest".  If something
    // left `CELL_TEST` asserted, the set() below would be a no-op and `rest` would be the held
    // value wearing the wrong name -- a step of zero, and a confident "there is a cell".
    if (const Status st = expander::set(Sig::CellTest, false); st != Status::Ok) {
        return Result<CellTest>::bad(st);
    }
    clock_::sleep_ms(kCellSettleMs);
    const auto rest = adc::read_mv(adc::Ch::Vbat);
    if (!rest.ok()) return Result<CellTest>::bad(rest.st);

    if (const Status st = expander::set(Sig::CellTest, true); st != Status::Ok) {
        return Result<CellTest>::bad(st);
    }
    clock_::sleep_ms(kCellSettleMs);
    const auto held = adc::read_mv(adc::Ch::Vbat);
    // Deassert on EVERY path out from here, including the failed read: leaving `CELL_TEST` set
    // is what R-BOARD-2 is about, and a board that gets unplugged while the bit is up is the
    // reboot loop.  There is no interlock to fall back on.
    const Status off = expander::set(Sig::CellTest, false);
    if (!held.ok()) return Result<CellTest>::bad(held.st);
    if (off != Status::Ok) return Result<CellTest>::bad(off);
    clock_::sleep_ms(kCellSettleMs);
    const auto open = adc::read_mv(adc::Ch::Vbat);
    if (!open.ok()) return Result<CellTest>::bad(open.st);

    CellTest t{};
    t.rest_mv = rest.v;
    t.held_mv = held.v;
    t.open_mv = open.v;
    t.step_mv = static_cast<int16_t>(static_cast<int32_t>(open.v) - held.v);
    t.present = t.step_mv < kCellStepMv;
    t.charging = chrg.v;
    return Result<CellTest>::good(t);
}

Status set_full_charge(bool on) noexcept { return expander::set(Sig::FullchgEn, on); }

// The pin, not a shadow.  `R24` holds `Q1` off while the expander is hi-Z, so "what GPB4 is
// actually at" and "what firmware last asked for" differ across a POR the firmware did not see
// -- and the pin is the one that decides whether the cell is being taken to 4.2 V.
Result<bool> full_charge() noexcept { return expander::get(Sig::FullchgEn); }

}  // namespace clk::hal::power
