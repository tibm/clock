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

## Phase 0 — what was in the way ✅ done 2026-09-13

### F0.1 · `motion step` was dead on a board whose homing failed — three defects ✅
Found on the bench 2026-09-13. The board homes on boot, homing fails because no index mark is
placed yet (F2.4), `fail()` leaves `state_ = State::Fault` — and from there you needed
`motion step` to place the mark while the failed homing that needs the mark was what blocked
`motion step`. Each of the three fixes on its own leaves that circle closed, so all three
landed together:

1. **A RAW target now goes through in `Fault`** (`motion.cpp`, the `HandTarget` handler). A
   dial-frame target is still held — there is no zero to measure it from — and everything is
   still held during `Homing`, where a target would fight the FSM for the same two shafts.
   `Fault` **survives** the nudge: the homing failure is still true and `motion status` says so.
   Two knock-ons that had to come with it, both of which killed the move on the next tick:
   `retarget()` no longer enters `Moving` when `power(true)` refuses, and the coil-hold timer
   now asks "is anything being driven" instead of "which state is this".
2. **Every `motion` row prints the refusal** (`cmd_motion.cpp`). `goto`/`step`/`home`/`stop`
   route their non-Ok answer through one `refused()` helper, which also names the gate and the
   cure — F0.2, below. The four public methods on `Motion` return `Status` instead of `void`;
   `Motion::accepts(bool raw)` is the one place the gates live.
3. **`motion stop` clears a `Fault`** → `Uninit` (not `Idle`: no zero was found). It is the
   operator's explicit act, and the alternative — a new `motion reset` — was not worth a verb.

Covered by `test_motion_a_bench_step_still_works_in_a_fault` (the whole circle, end to end)
and `test_motion_an_inhibited_movement_refuses_by_name`.

### F0.2 · Make both gates discoverable ✅
A refused move now names which gate stopped it, because on a bring-up board the two are
indistinguishable from the outside:

| refusal | what it means | what to type |
|---|---|---|
| `denied` | `board.hpp:74`'s NVS bench inhibit. **`unsafe on` does not lift it** | `motion power on` |
| `notready` | a `Fault` the homing FSM never left | `motion stop` (`motion step` works anyway) |
| `busy` | a homing run has both shafts | `motion stop` |
| `notpresent` | no movement fitted (the presence mask) | `sim present motor on`, or fit it |

⚠ `board.hpp:73` still carries "flip this to false when milestone 3 closes". Leave it true.

---

## Phase 1 — milestone 1's last item: `hal::power::read()` ✅ written 2026-09-13

**One implementation, both backends.** It went to `clk_hal/shared/power.cpp` rather than being
mirrored into `hal_esp.cpp`, because every line of it is arithmetic over `hal::adc` and
`hal::expander` and there was nothing platform-specific left to put on either side. Two copies
of the SoC endpoints and three copies of an open-drain inversion is how host and target come to
disagree; this is the same call `shared/mcp23017.cpp` already makes, and it means the host tests
exercise the code the board runs (§11.2).

### F1.1 · `power::read()` ✅
`VBAT_DIV_EN` is switched in and put back **inside `adc::read_mv(Ch::Vbat)`**, which already did
it on target — `read()` does not touch the FET, so `sensor homing` and `sensor vbat` do not have
to know about each other's plumbing. `PD_PG`/`CHRG`/`FAULT` are read first, so the `NotPresent`
answer is identical on both backends (D16). All three are open-drain **active-low** and are
inverted in one helper.

⚠ **The host fake had `PD_PG` the wrong way round** (level == plugged) and now does not.
`sim plug` printed "PD_PG high (plugged)" too. Both were fixed, along with §7.4's state diagram
and interlock 1 in `FIRMWARE.md`. §12.0.11 step 6 expecting `PD_PG 0` with the brick in was
right all along.

### F1.2 · R-BOARD-3, in the return type ✅
`power::State` carries `VbatSrc src`, and `soc_pct` is `kSocUnknown` unless `src == Cell`:

- **unplugged → `Cell`.** The board is running from the cell, cell − is at PACK−, the tap is
  across the cell and a percentage means something. This is exactly the line `FIRMWARE.md`
  R-BOARD-3 draws.
- **plugged → `BatNode`.** The charger's output. Usually equal to the cell, because the charge
  FET is usually closed, and nothing on this board can tell the difference.
- `CHRG` deliberately does **not** buy its way into `Cell`. Current flowing does prove the
  charge FET is closed, but it also puts the node I × R above the cell and the LT3652 holds
  `CHRG` through its whole C/10 taper — a cell voltage plus an unknown offset is not cell health.

`sensor vbat` prints `src=` and `soc=?`. `ui`'s low-battery warning was already gated on
`!plugged`, so it is unaffected.

### F1.3 · `CellTest` ✅ `board cell`
Asserts `CELL_TEST`, settles, reads, deasserts, settles, reads — the **step at switch-off** is
the measurement: ~0 with a cell holding the holder+ net up, one `Q2` body diode (~350 mV) with
an empty holder. R-BOARD-2 is enforced in the driver, not in the command: a **fresh** `PD_PG`
read, `Denied` on battery, and `CELL_TEST` deasserted on every path out including a failed read.
The command reports `charging` too, because with the charge FET open neither reading means what
it says and the verdict is unproven (R-BOARD-3 again).

### F1.4 · `FullchgEn` ✅ `board fullchg [on|off]`
Off is the default and needs no firmware help — expander hi-Z at POR, `R24` holds `Q1` off — so
`full_charge()` reads the **pin** rather than a shadow. Reported in both directions with which
cap it means (4.05 V vs 4.20 V).

### F1.5 · Host model + tests ✅
The fake now models the two FETs on the Vbat node rather than handing out `vbat_mv`, because the
discriminator is entirely about them: `Q2` conducting ties holder+ to the BAT node (which reads
the 4.05 V float with **no cell at all** — the failure the discriminator exists for), and `Q2`
off sees the holder alone. New `sim cell <in|out>`; `sim::vbat_div_reads()` is how a test sees
that a read went through the divider leg rather than round it. Six cases in `test_motor.cpp`.

One thing `host-tsan` found on the way, and it was always there: `board::g_present` is written
by `sim present` / `board i2c scan` and read from every AO tick, as a plain `uint16_t`. It is
`std::atomic<uint16_t>` with relaxed ordering now. `hal::power::read()` asking three expander
pins per `ui` tick is what made a latent race loud enough to catch.

⚠ The host suite is **flaky on this machine and was before this work** — the `motion` AO cases
cascade when `chrono`/`ui` get a target in edgeways. Measured 2026-09-13: clean `HEAD` failed
**3/3** with the same 16 checks; this branch fails roughly 1 run in 6, usually
`test_motion_autohome_trims_a_drifted_hand`'s 0.2° tolerance. Compare against a clean worktree
before blaming a change. Worth chasing properly at some point — the cause looks like `ui`
walking its own modes (and therefore driving the hands) while a `motion` case is measuring.

### F1.6 · Bench-verify — **the unplugged half passes; the plugged half cannot be reached**
Run on build #1 2026-09-13, cell in, Mac on `J1`:

```
sensor vbat read   ->  vbat  mv=3466 soc=22 src=cell plugged=0 chrg=0 flt=0
sensor exp read    ->  exp  gpa=0001 gpb=11110010 radio=on stby=0
board fullchg      ->  full-charge off -- cap 4.05 V
board cell         ->  refused: on battery ... (R-BOARD-2)   [denied]
```

| ✅ | what it proves |
|---|---|
| `src=cell`, `plugged=0` | `PD_PG` deasserted on a Mac port — no 15 V contract, so the board runs off the cell and the tap really is across it. R-BOARD-3's ambiguity does not arise in this bench setup at all, and `soc=22` at 3466 mV is a real SoC |
| `gpb=11110010` | byte-for-byte §12.0.6's reference idle. **Bit 5 `VBAT_DIV_EN` = 0**: the divider was switched in for the read and put back, on silicon (F1.1) |
| `gpb` bit 4 = 0 / `fullchg off` | the 4.05 V cap, enforced by `R24` with no firmware help |
| `board cell` → `[denied]` | R-BOARD-2's guard firing on a real board, off a fresh `PD_PG` |

⏳ **What is left needs the wall and the console at the same time**, which build #1 cannot do on
its own: `PD_PG` asserted means the brick on `J1`, and the console is `J1` too. So `board cell` can
only ever refuse here — which it does, correctly — and `PD_PG 0`, `chrg=1` and the 4.05 V settle
are unobservable the same way.

**This is not a wait for v0.4.** `kicad/REVIEW.md` **V6** already names the temporary form and it
needs no rework: an **inline USB-C male-to-female pass-through** between brick and `J1`, tapping
`D±`+GND to the Mac, host `VBUS` unconnected, `D±` cut on the brick side. §12.0.11's rig section has
the wiring and the three conditions that decide whether it works — the one that disqualifies most
cheap boards is that it must pass **`CC1`/`CC2`** through, so a PD sniffer/analyser is the right
shape and a plain fan-out breakout is not. ⚠ Still unproven on this board: the S3 is self-powered
in that rig and asserts its own `D+` pull-up, which generally enumerates but has not been tried here.

| ⏳ | needs |
|---|---|
| `board cell` verdict, `sensor exp read` → `PD_PG 0` | the pass-through rig (§12.0.11) |
| `sensor vbat stream 1 600` → `chrg=1`, mV settling at **4.05 V not 4.2** | the rig **and** the AO4838 |

A firmware alternative, if the rig is not worth buying: **the cell keeps the board alive across a
`J1` swap.** Plug the brick with no console, sample `hal::power::read()` on a timer into a ring,
swap to the Mac, read it back. `sys ev dump` is the natural home and is registered but still
`cmd_notyet` (`cmd_sys.cpp`). Not built — say the word.

## Phase 2 — milestone 3: finish the movement

`motion step` works on a faulted board as of F0.1, so nothing here is blocked by firmware any
more. ⚠ **It is blocked by the hands**, which are on the printer as of 2026-09-13: F2.1, F2.2 and
F2.4 all need something visible on a shaft. F2.0 below is done because it must not be forgotten,
and F2.3 is the one item you can do with bare shafts.

### F2.0 · Hands swapped shafts ✅ done 2026-09-13, untestable until the hands exist
The **minute** hand is on the X40 **inner pin** (front, like a normal clock), the **hour** hand on
the **outer tube**. The wiring did not move, so firmware crosses them:
`motor_esp.cpp`'s `build()` gives MCPWM0 (the tube, `STEP_M_*`) to `Hand::Hour` and MCPWM1 (the pin,
`STEP_H_*`) to `Hand::Minute`. `board.hpp`'s arrays are `step_tube`/`step_pin` now, so the pins name
what they are soldered to and the hand assignment lives in one place. `kicad/REVIEW.md` **V12**
renames the schematic nets at the respin; `kicad/gen/` is untouched.

⚠ **Nothing on the host can catch that crossing being wrong** — the fake has no pins. So `build()`
logs `hour=tube(MCPWM0) minute=pin(MCPWM1)` at boot, and **that log plus one `motion step h` with a
hand on is the whole verification.** Do it first, before anything below leans on it.

Two consequences that are F2.4's problem, written up in `FIRMWARE.md` §6.1e:

1. The hour hand is ~4 mm nearer the QRE1113, so it **occludes** the minute hand. The `Clear`
   phase's hour-first order was arbitrary before and is now the only order that can work.
2. The minute hand is now the **far, weak** one — V2's 149 mV step is about this hand.

### F2.1 · `steps_per_rev` — needs a hand
§13 open question 1, the number the whole dial depends on. Count microsteps for one revolution,
confirm **17 280**. ⚠ `motion spr` only *prints* the constant today, and "writes it to NVS" is not a
small change: `domain::kRev` is `constexpr` and every function in `hand.hpp` is `constexpr` over it,
exhaustively tested. Worth doing **only if the count comes out wrong** — measure first.

### F2.2 · Direction — needs a hand
Clockwise must come out positive. If not, `kSwapB` in `motor_esp.cpp` is the single line and a
reflash. ⚠ It is one flag for **both** axes; the X40's two gear trains could in principle have
opposite parity, and if exactly one hand comes out backwards that flag cannot say so. Make it
per-hand (and NVS-backed, so the bench needs no rebuild) *if* that happens — not before.

### F2.3 · Silence — **the one item that does not need hands**
Tune microstep depth against the gear train's resonance; the 25 kHz carrier is already above
hearing. Bare shafts are audible, so this can be done now.

### F2.4 · Homing — needs hands, and a re-measurement first
Place the index mark with `sensor homing stream`, then the homing FSM.

⚠ **Start by re-measuring the opto, because today's span cannot see one hand.** `kOptoMarkMv` is
2600 — the *near* (hour) hand's level — so the minute hand's 3010 normalises to
`(3150-3010)/550 = 0.25`, under `motion`'s 0.45 `opto_thresh`. The far hand's index crossing is
currently **invisible**, and `Clear`'s "still lit with the hour hand moved away" branch therefore
cannot be observed at all. Do not lower the threshold on paper: those numbers were bare surfaces at
distance, a printed index mark reflects far better than one, and V2 roughly doubles the scale.

⚠ `R99` is still 10 k; v0.4 **V2** wants 22 k. Decide whether to bodge it on build #1 *after* the
re-measurement, since the real hand tabs may make it unnecessary.

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

| stub | where | needs |
|---|---|---|
| `hal::audio` — `enable`, `set_volume_pct` | `hal_esp.cpp`, `namespace audio` | I²S + MCLK + TAS5760M over I²C, then the firmware biquad HPF + limiter |
| `hal::wake` — `set`, `warm`, `cool` | `hal_esp.cpp`, `namespace wake` | the 12 V rail and the two AO3400A PWM channels |

`hal::power` used to be the third row here. It is written (Phase 1), and the file's STATUS block
at the top now lists what is real rather than what is not — these two are the whole of what is
left on that side.

⚠ Before any loud-audio work, re-read **R-AUDIO-1**. With the AO4838 fitted the protector trip
moves to 3.8–6.6 A (`-GB`) and the 2.3 A sunrise alarm has 1.65× margin — the old `-GB` budget
no longer binds. Until the swap, it does, and hard.

---

## Hardware gates — what is waiting on what

| gate | blocks |
|---|---|
| **AO4838 fitted** | charging of any kind; `sensor vbat stream` showing a real charge curve; F1.6's 4.05 V confirmation |
| an inline USB-C **pass-through** (V6's temporary form, §12.0.11 — no rework) | **every plugged-in reading.** `board cell` cannot return a verdict without it: the command needs `PD_PG` asserted (brick on `J1`) and you need the console (also `J1`). Confirmed 2026-09-13 — it refuses correctly and there is no way past it. Must pass `CC1`/`CC2`; a fan-out breakout will not do |
| **the printed hands** (on the printer 2026-09-13) | F2.0's one real check, F2.1, F2.2 and all of F2.4. F2.3 is the only Phase 2 item that works on bare shafts |
| `R99` 10 k → 22 k (v0.4 **V2**) | possibly nothing — decide after F2.4 re-measures with real hand tabs, which reflect far better than the bare surfaces V2's 149 mV came from |
| v0.4 **V8** supervisor | no-cell operation; recovering a cell below ~2.9 V. Not worth reworking on build #1 |
| v0.4 **V6** (`D±` on `J2`) | brick and host connected at the same time. Today it is one or the other |

---

## Ground rules carried out of this bring-up

1. **Check thresholds against real currents, both directions.** Three of the four power
   lockups were one missing inequality (`power.md`, the design-rule bullet).
2. **A refusal must never print as a success.** F0.1 was the instance; fixed 2026-09-13, and the
   `motion` group routes every non-Ok answer through one `refused()` helper. The audit that came
   with it: `chrono`/`ui`'s rows are setters on AO state that hardware cannot refuse, and
   `ui led`/`ui wake` already check and report. Re-run the audit when `audio` and `board` land,
   because both are full of things the hardware can say no to.
3. **D16 holds:** absence answers `NotPresent`, never a faked reading.
4. **v0.4 items live in `kicad/REVIEW.md`, not in `kicad/gen/`.** The generated schematic and
   PCB keep matching their source until the respin.
