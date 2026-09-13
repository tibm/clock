# Next steps — firmware, after the power bring-up of 2026-09-13

Working document. `FIRMWARE.md` stays the architecture source of truth; this file is the
ordered queue and gets deleted when it empties.

**State of the board.** Build #1 (rev0.3) runs on a cell with the Mac on `J1`, console up,
sensors answering, knob counting, coils commutating. The power tree is understood and
documented (`power.md`, `FIRMWARE.md` §12.0.12–13). One part is on order: **`U4`
AOSD32334C → AO4838** ([DK 3152401](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO4838/3152401)).

**Bench setup for everything below** — no bridge needed once the cell is in and released:

| | |
|---|---|
| power | 18650 in the holder, **left in** |
| console | Mac → `J1` (its 5 V lands on the idle LT3652, below the 11.2 V UVLO) |
| ⚠ | no `J12` 5 V injection — retired |
| ⚠ | charging does not work until the AO4838 lands; top the cell up externally |
| ⚠ | if the cell comes out, re-insertion may trip the protector → bridge cell − to board GND once, verify `U3` pin 1 goes high, remove the bridge, then carry on (§12.0.13) |

---

## Phase 0 — what is in the way (small, do first)

### F0.1 · `motion step` is dead on a board whose homing failed — three defects
**Found on the bench 2026-09-13**, and it is worse than a missing error message. The board
homes on boot (the inhibit was released and persisted), homing fails because no index mark is
placed yet (F2.4), and `fail()` at `motion.cpp:688` leaves `state_ = State::Fault`. From there:

1. **`motion.cpp:170` swallows every target in `Homing` or `Fault`** — `motion step` included —
   and says so only at `CLK_LOGD`. A bench command whose whole purpose is raw, relative
   stepping should be the one thing that still works when the FSM has given up. Let raw
   targets (`HandTarget.raw`) through in `Fault`, or give `step` its own path.
2. **`cmd_step` prints success anyway** (`cmd_motion.cpp:98`) — it calls `svc::motion().nudge()`
   and unconditionally returns `Status::Ok`, never looking at what the layer below did. Audit
   `cmd_goto` / `cmd_home` / `cmd_stop` for the same shape.
3. **There is no CLI escape from `Fault`.** `motion stop` clears `Homing` but not `Fault`
   (`motion.cpp:266-282`); the only transition out is `HomeRequest`, which fails again. So the
   board is stuck: you need `motion step` to find the index mark, and the failed homing that
   needs the index mark is what blocks `motion step`. Either `motion stop` should clear `Fault`
   as an explicit operator action, or `motion home` should be joined by a `motion reset`.

Bench workaround until this is fixed (no rebuild): `motion power off`, reboot — homing then
hits `Denied` and lands in **`Uninit`, not `Fault`** (`motion.cpp:240-247`) — then `unsafe on`,
`motion power on`, and `motion step` works.

`cmd_step`'s silence has **two** sources below it, and both need the same treatment: the
`Denied` from `motor_esp.cpp:322` when the bench inhibit is set, and the dropped target from
`motion.cpp:170` when the FSM is in `Fault`. Rule to apply either way: **a CLI command must
never print a success line for an operation the layer below refused.**

### F0.2 · Make both gates discoverable
`board.hpp:74` `motor_inhibited_default()` is true on the physical boards, and the release is
`motion power on` (NVS-backed) — *not* `unsafe on`, which only lifts the CLI's `Unsafe` flag.
Once F0.1 lands, a refused `step` should name whichever gate stopped it — the inhibit, or a
`Fault` the FSM never left.

⚠ `board.hpp:73` carries "flip this to false when milestone 3 closes". Leave it true until
then.

---

## Phase 1 — close milestone 1: `hal::power::read()`

The last open item in milestone 1, and it has been blocked since bring-up started because it
needs a real BAT node. **That blocker is gone.**

`firmware/components/clk_hal/esp/src/hal_esp.cpp:675` is the whole ESP implementation:

```cpp
namespace power {
Result<State> read() noexcept { return Result<State>::bad(Status::NotPresent); }
}  // namespace power
```

Everything around it already exists and needs no design:

| piece | where |
|---|---|
| `State{vbat_mv, soc_pct, plugged, charging, fault}` | `hal.hpp:369` |
| CLI sampler `s_vbat` + the `sensor list` row | `cmd_sensor.cpp:41,172` |
| `adc::Ch::Vbat` — IO1, ADC1_CH0, "the /2 divider is undone here" | `hal.hpp:63` |
| `PdPg` GPB0 · `Chrg` GPB1 · `Fault` GPB2 (inputs) | `hal.hpp:302-304` |
| `FullchgEn` GPB4 · `VbatDivEn` GPB5 · `CellTest` GPB7 (outputs) | `hal.hpp:307-309` |
| **a complete reference implementation** incl. the SoC math | `hal_host.cpp:872` |

### F1.1 · Implement `power::read()` on ESP
Mirror `hal_host.cpp:872` so host and target agree.

1. `VbatDivEn` **on** → settle → `adc::read_mv(Ch::Vbat)` → undo the /2 → `VbatDivEn`
   **off** again. The divider is switched for a reason (deep-sleep draw, `power.md`); it must
   not be left in.
2. `PdPg`, `Chrg`, `Fault` off the expander. All three are **open-drain active-low** — invert.
3. mV → SoC% using the same `kVbatEmptyMv`/`kVbatFullMv` endpoints as the host.
4. `NotPresent` if the expander is not there (D16: absence is not an error, and never a faked
   reading).

### F1.2 · ⚠ R-BOARD-3 — do not report the BAT node as cell health
`VBAT_SENSE` taps cell+ against **board GND**, not across the cell. Whenever the protector's
charge FET is open, cell − floats a full cell-voltage away from PACK− and the ADC reads the
charger's output instead. Measured on build #1 2026-09-13: **~4.0 V reported for a cell
actually at 3.4 V.**

There is no hardware fix short of a differential sense. So the driver must not present the
number as a cell measurement unconditionally — cross-check against `Chrg`/`PdPg` and say which
it is. Decide the exact contract when writing it, and write it into `hal.hpp` next to `State`.

### F1.3 · `CellTest` — full cell vs empty holder
Expander GPB7 → Q8 → Q9 lifts `Q2`'s gate, and firmware keys on the **step** at switch-off
(`power.md`, and the long comment in `kicad/gen/b_charger.py`). **Plugged-only**, and
**R-BOARD-2** applies: gate every assertion on a *fresh* `PD_PG` read, because on battery
turning `Q2` off drops the rails and the board reboots. There is no hardware interlock.

### F1.4 · `FullchgEn` — policy, not part of `read()`
The 4.05 V health cap is fixed in hardware by the float divider. `FullchgEn` (GPB4 → Q1)
switches in `R16` for a 4.20 V "top to 100 %" mode. Expose it as an explicit command, default
off, and make sure `R24`'s pulldown story (expander hi-Z at POR) is reflected.

### F1.5 · Host model + tests
The host side is already complete, so this is mostly making sure the new ESP path is covered
by the same expectations. Add `clocksim` coverage for the `CellTest` step and the
`VbatDivEn` switch-in/switch-out.

### F1.6 · Bench-verify, then close milestone 1
`sensor vbat read` → cell mV tracking. `sensor exp read` → `PD_PG 1` on a Mac port, `0` on the
brick. After the AO4838 lands: `sensor vbat stream 1 600` with the brick in should show
`chrg=1` and mV settling at **4.05 V, not 4.2**.

---

## Phase 2 — milestone 3: finish the movement

Unblocked by F0.1, and none of it needs new hardware.

- **F2.1 `steps_per_rev`** — §13 open question 1, the number the whole dial depends on. Count
  microsteps for one revolution, confirm **17 280**, `motion spr` writes it to NVS.
- **F2.2 Direction** — clockwise must come out positive. If not, `kSwapB` in `motor_esp.cpp`
  is the single line.
- **F2.3 Silence** — tune microstep depth against the gear train's resonance; the 25 kHz
  carrier is already above hearing.
- **F2.4 Homing** — place the index mark with `sensor homing stream`, then the homing FSM.
  ⚠ `R99` is still 10 k; v0.4 **V2** wants 22 k, so the minute-hand step is only 149 mV until
  a rework. Decide whether to bodge `R99` on build #1 before tuning thresholds against it.

---

## Phase 3 — milestones 4–5

- **F3.1** The `J12` off-board pixel harness (5 status pixels, chain positions 3–7). The two
  on-PCB dial pixels already light.
- **F3.2** `chrono` + SNTP — **hands follow real time.** The first build that is a clock.
  Stop and enjoy it.

---

## Phase 4 — milestone 6+: the stubs that are left

Two more ESP HAL namespaces are still `NotPresent` stubs, both gated on the 12 V boost, which
is gated on `PD_PG`:

| stub | file:line | needs |
|---|---|---|
| `hal::audio` — `enable`, `set_volume_pct` | `hal_esp.cpp:668` | I²S + MCLK + TAS5760M over I²C, then the firmware biquad HPF + limiter |
| `hal::wake` — `set`, `warm`, `cool` | `hal_esp.cpp:469` | the 12 V rail and the two AO3400A PWM channels |

⚠ Before any loud-audio work, re-read **R-AUDIO-1**. With the AO4838 fitted the protector trip
moves to 3.8–6.6 A (`-GB`) and the 2.3 A sunrise alarm has 1.65× margin — the old `-GB` budget
no longer binds. Until the swap, it does, and hard.

---

## Hardware gates — what is waiting on what

| gate | blocks |
|---|---|
| **AO4838 fitted** | charging of any kind; `sensor vbat stream` showing a real charge curve; F1.6's 4.05 V confirmation |
| `R99` 10 k → 22 k (v0.4 **V2**) | homing threshold tuning against final signal levels (F2.4) |
| v0.4 **V8** supervisor | no-cell operation; recovering a cell below ~2.9 V. Not worth reworking on build #1 |
| v0.4 **V6** (`D±` on `J2`) | brick and host connected at the same time. Today it is one or the other |

---

## Ground rules carried out of this bring-up

1. **Check thresholds against real currents, both directions.** Three of the four power
   lockups were one missing inequality (`power.md`, the design-rule bullet).
2. **A refusal must never print as a success.** F0.1 is one instance; look for others.
3. **D16 holds:** absence answers `NotPresent`, never a faked reading.
4. **v0.4 items live in `kicad/REVIEW.md`, not in `kicad/gen/`.** The generated schematic and
   PCB keep matching their source until the respin.
