# Next steps — firmware, after the power bring-up of 2026-09-13

Working document. `FIRMWARE.md` stays the architecture source of truth; this file is the
ordered queue and gets deleted when it empties.

**State of the board.** Build #1 (rev0.3) runs on a cell with the Mac on `J1`, console up,
sensors answering, knob counting, coils commutating. The power tree is understood and
documented (`power.md`, `FIRMWARE.md` §12.0.12–13). One part is on order: **`U4`
AOSD32334C → AO4838** ([DK 3152401](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO4838/3152401)).

> 🔧 **Two hardware reworks are outstanding on build #1** and both are blocking: `U4` →
> AO4838 (charging) and `U9` pin 1 `AVDD` → PVDD (all of audio). Step-by-step bench
> instructions with verified coordinates are in **[`REWORK.md`](REWORK.md)**.

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

## Phase 4 — milestone 6+: the stub that is left

| stub | where | needs |
|---|---|---|
| `hal::wake` — `set`, `warm`, `cool` | `hal_esp.cpp`, `namespace wake` | the 12 V rail and the two AO3400A PWM channels — gated on the 12 V boost, which is gated on `PD_PG` |

`hal::power` used to be one of three rows here, and `hal::audio` the second. Both are written
(Phase 1 and Phase 5); `hal_esp.cpp`'s STATUS block lists what is real rather than what is not,
and `wake` is now the whole of what is left on that side.

⚠ Before any loud-audio work, re-read **R-AUDIO-1**. With the AO4838 fitted the protector trip
moves to 3.8–6.6 A (`-GB`) and the 2.3 A sunrise alarm has 1.65× margin — the old `-GB` budget
no longer binds. Until the swap, it does, and hard — which is exactly what
`hal::audio::kMaxVolPct` encodes (Phase 5).

---

## Phase 5 — milestone 6 ⛔ **firmware done and proven; blocked on one net** (2026-09-14)

`hal::audio` is real and is now bench-proven *correct* (`FIRMWARE.md` §12.0.15, §12.0.16): I²S0
with MCLK on `IO43`, the TAS5760M's register set in the datasheet's start-up order, a generated
sine, and the `audio` CLI group. It still makes no sound, and the cause is **hardware**:

> ⛔ **`U9` pin 1 `AVDD` is wired to `+3V3`; its datasheet minimum is 4.5 V**
> (`amp_tas5760m.pdf` §6.3 — AVDD is 4.5–26.4 V, the same range as PVDD; only DVDD is 3.3 V).
> The digital domain runs fine — I²C is perfect, every register reads back what was written —
> and the analog domain, which holds the clock-validation circuitry, is starved. Reg 0x08 sits
> at `CLKE` with all three clocks present and correct at the amp's own pins on a scope.
> **`kicad/REVIEW.md` V13** has the fix (one net) and the bench bodge: lift pin 1, wire to
> `PVDD` (pin 28, or `C172`'s + terminal).

**Do the bodge first — nothing below can pass without it.** Everything after F5.1 was written
before the cause was known and is still the right sequence once the amp is fed.

⚠ The spec violation is certain. That it is the *sole* cause of CLKE is a strong inference and
is proven only when the bodge makes a sound — so F5.1 is now "did the bodge work", and if CLKE
survives it, the next suspect is amplitude/VIH at `U9` pin 14 (0.7 × DVDD = 2.31 V), which is
the one electrical parameter never measured.

⚠ **Before you plug the speaker in, read the ceiling.** `hal::audio::kMaxVolPct` is **25 %** and
the default is **10 %**, and that is R-AUDIO-1, not caution: with no 15 V brick the amp runs off
the 5 V rail and full scale is 3.1 W ≈ **1.9 A peak from the cell**, which is the `-GB`
protector's 1.89 A trip. A trip self-clears, so it looks like **a spontaneous reboot** — if the
board resets during a tone, that is the first thing to suspect, not the firmware.

### F5.0 · The bodge — **cut one trace**, then wire `U9` pad 1 to `PVDD`
Checked against `clock.kicad_pcb` 2026-09-14, and it is easier than it first looked: all four
zones are **GND**, so there is no `+3V3` pour, and pad 1's only path to `+3V3` is one
**0.25 mm B.Cu trace, 0.85 mm long** running in −X from **(92.850, 83.432)** to a junction at
**(92.000, 83.432)**.

1. **Cut at ≈ (92.4, 83.43) on B.Cu.** The pad stays attached to the pin, so nothing is
   lifted. ⚠ Nearest other copper is pin 2's `GVDD_REG` trace **0.65 mm** away (y = 82.782).
2. **Wire pad 1 → `C170` pad 1** — PVDD, 0603, 0.9 × 0.9 mm pad, 10.66 mm, same side, straight
   run. ⚠ **Pad 2 of `C170`/`C171`/`C172` is GND** — pin 1 only.
3. **Ohm it out before power:** pin 1↔`+3V3` open · pin 1↔GND open · pin 1↔pin 2 open · then
   after the wire, pin 1↔`U9` pin 28 ≈ 0 Ω.

PVDD pads, all verified from the PCB: `C170.1` (103.50, 83.934) · `C171.1` (106.00, 83.934) ·
`C172.1` (104.70, 64.014) · `U9.21` (100.15, 76.282) · `U9.28` (100.15, 80.832).

`C162`/`C163` stay behind on `+3V3` as harmless extra bypass; `C170` is the tie point so the
0.1 µF is right there. **The cut is reversible** — a wire from pad 1 back to the via at
(91.688, 83.101) puts it back on `+3V3`.

⚠ After the bodge `AVDD` follows `PVDD` **including up to 12 V** when the boost is enabled.
That is intended: 26.4 V recommended max, 30 V absolute, and it is what the datasheet's own
Figure 64 does.

Then: `audio tone 1000` → `audio status`. **Reg 0x08 should read `0x00`.** If it does, the
speaker should be making a 1 kHz tone at 10 % and milestone 6 is unblocked.

### F5.1 · Does it clock at all
```
board i2c scan          -> 0x6C TAS5760M amp (main board)
audio status            -> clocks=off sd_pin=LOW configured=no
audio tone 1000 2000
audio status            -> clocks=on sd_pin=high configured=yes muted=no
                           i2s 48000 Hz  mclk 12288000 Hz (256 x fs)  bclk 1536000 Hz
                           regs 0x02=0x04 0x06=0xD1 vol=0xA7  PBTL mono, 19.2 dBV
```
A scope on `IO43`/`IO10`/`IO11` before the speaker goes on is worth the minute: 12.288 MHz,
1.536 MHz and 48 kHz. ⚠ If `audio status` shows `CLK` in reg 0x08 the amp is not seeing a valid
clock triplet, and that is the one fault bit that does **not** latch — it is telling you about
right now.

### F5.2 · Does it make a sound
Speaker on, `audio tone 1000 2000` at the default 10 %. Then:

| listen for | means |
|---|---|
| a clean 1 kHz | the whole chain |
| a tick at each end | the 5 ms fade is not doing its job, or `SPK_SD` is moving while unmuted |
| a buzz rather than a tone | DMA underrun — `audio status` counts them |
| nothing, with `clocks=on sd_pin=high muted=no` | PVDD. Check the LTC4412 output, not the firmware |

Then sweep it: `audio tone 100 1000`, `audio tone 440 1000`, `audio tone 5000 1000`. The 100 Hz
one is the interesting one — the DMA58-4 has 2 mm of Xmax and **no HPF in front of it yet** (the
~150 Hz Linkwitz-Riley is the firmware biquad, which does not exist). Keep it short and quiet.

### F5.3 · Confirm the volume map with a meter
`audio vol 10` then `audio vol 20` should move the output by **+6.0 dB** — percent is amplitude,
so the map is `20·log10(pct/100)` and it is checkable with a multimeter on AC volts across the
speaker. At 10 % expect ~0.9 V rms into 4 Ω. If the numbers come out 6 dB high, the digital boost
did not get cleared (§12.0.15 finding 1) — read `audio reg 2`, it must be `0x04`.

### F5.4 · The one that costs money if it is wrong
`audio vol 25`, tone on, and **watch the cell current**. Under ~1.2 A peak is the prediction. If
the board reboots, that is the protector and the ceiling is not conservative enough — say so here
rather than raising it.

### F5.5 · ⚠ Unrelated, found while building all four profiles: `BOARD=devkit-uart` does not compile
Pre-existing, nothing to do with audio. `console_esp.cpp` calls
`esp_console_new_repl_usb_serial_jtag()` unconditionally, and that profile sets
`CONFIG_ESP_CONSOLE_UART_DEFAULT=y` — so the symbol is not declared and the build stops.
The other three profiles (`dev/devkit`, `dev/rev0_3`, `release/rev0_3`) are clean. It is the
profile you reach for when chasing a boot panic, so it is worth an `#if
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG` before you need it.

### F5.6 · What is still missing after all that
Not blockers for the above, just the honest list: `audio play <file>` (needs `storage` + the
PSRAM ring), the biquad HPF + limiter (`audio dsp`), and the pop-free 12 V PVDD ramp — which
cannot even be exercised until `PD_PG` is assertable, i.e. the pass-through rig.

---

## Hardware gates — what is waiting on what

| gate | blocks |
|---|---|
| **`U9` pin 1 `AVDD` → `PVDD`** (v0.4 **V13**, bodgeable today) | **all of audio.** The firmware is done and proven; the amp's analog domain is 1.2 V under its supply minimum and reg 0x08 sits at `CLKE`. One net at the respin; lift-pin-1-and-wire on build #1 |
| **AO4838 fitted** | charging of any kind; `sensor vbat stream` showing a real charge curve; F1.6's 4.05 V confirmation |
| an inline USB-C **pass-through** (V6's temporary form, §12.0.11 — no rework) | **every plugged-in reading.** `board cell` cannot return a verdict without it: the command needs `PD_PG` asserted (brick on `J1`) and you need the console (also `J1`). Confirmed 2026-09-13 — it refuses correctly and there is no way past it. Must pass `CC1`/`CC2`; a fan-out breakout will not do |
| **the printed hands** (on the printer 2026-09-13) | F2.0's one real check, F2.1, F2.2 and all of F2.4. F2.3 is the only Phase 2 item that works on bare shafts |
| `R99` 10 k → 22 k (v0.4 **V2**) | possibly nothing — decide after F2.4 re-measures with real hand tabs, which reflect far better than the bare surfaces V2's 149 mV came from |
| v0.4 **V8** supervisor | no-cell operation; recovering a cell below ~2.9 V. Not worth reworking on build #1 |
| v0.4 **V6** (`D±` on `J2`) | brick and host connected at the same time. Today it is one or the other |
| **AO4838 + a 15 V brick, together** | the 25 % audio ceiling (`hal::audio::kMaxVolPct`) and `ui`'s matching send-clamp. Until both, PVDD is the 5 V rail and full scale sits on the `-GB` trip — R-AUDIO-1, `FIRMWARE.md` §6.2 |

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
