# Design review — `sensor.kicad_sch` / `sensor.kicad_pcb`

**Date:** 2026-08-05 · **Reviewed at commit:** `88caebf` ·
`sensor.kicad_sch` md5 `e1f55fe8961e223662f11b4b927c64f0` ·
`sensor.kicad_pcb` md5 `b02be8381c61b9cc05c338ffa9f0fa76`

> **Status: actioned — the board is now v0.2 (2026-08-07/08).** The status list
> below says what happened to each of the 19 findings. **Findings that are
> fixed have had their write-ups removed from this file**; the fix lives in the
> design files and the reasoning in `PCB_NOTES.md`. What remains in full is the
> work that is still live, plus the "checked and found correct" record.
> The header below is the state the review was written against, not the state
> of the files today.

**Scope:** functional/electrical correctness of the sensor daughterboard's schematic and
PCB, cross-checked against `datasheet/sensor_imu_bno085.pdf`, `sensor_env_bme688.pdf`,
`sensor_light_tsl2591.pdf`, `connector_jst_zh.pdf`, and against the main board's J7.
Enclosure/optical design, the BSEC algorithm, and the main board's own internals are
**not** covered (those are `kicad/REVIEW.md`).

## Headline

**The schematic is in very good shape.** Every one of CEVA's eight I²C application
notes is honoured, the BME688 and TSL2591 follow their datasheets' own connection
diagrams, and I found **no wiring error** — nothing in the class of the main board's
#1/#2/#3. ERC and DRC are both completely clean.

Everything found was what those tools structurally cannot see, and it clustered into
three groups: **one fail-safe change worth making before the BOM goes out**, a handful
of **fab-package and mechanical items**, and a **documentation gap** — the board had
been hand-routed and moved to 4 layers on 2026-08-02 while `PCB_NOTES.md` / `README.md`
still described the 2-layer unrouted board. All three are closed as of v0.2.

---

# ✅ Actioned as v0.2 — 2026-08-07/08

**Verification after the pass:** `kicad-cli sch erc` **0 violations** (with
*"Global label only appears once"* re-enabled) · `kicad-cli pcb drc
--severity-all` **0 violations, 0 unconnected, 0 parity** · `gen/pcb_check.py`
**33/33** (re-baselined — see #6/#9/#15) · `stamp_bom.py` idempotent.

**Workflow findings (W1–W4), all closed.** `gen/pcb_build.py` has the
destructive-run guard and a banner saying it is historical; `PCB_NOTES.md` and
`README.md` were rewritten for the 4-layer routed board, including the
`build.py` → `stamp_bom.py` rule and the pcbnew + **F8** path that replaces the
`sync_pcb.py` W4 asked for.

**New, found during the pass:** a re-route sealed off a 0.3 mm² pocket of B.Cu
pour and a GND stub dead-ended in it — DRC saw it only as one *unconnected
item*, and only after a refill. `pcb_check.py` now tests every filled island
for a pad or via of its own net. Separately, `gen/pcb_fill.py` was silently
clobbering `sensor.kicad_pro` (and with it the ERC configuration) on every
run, because `board.Save()` rewrites that file; it now restores it, as
`pcb_canon.py` already did.

**Two findings did not survive verification** and are marked ❌ below: #2 (the
`B6B-ZR-3.4` "free BOM change" is made-to-order only) and #15 (the M2 washer
keepout does not overhang the corners).

**Still live below the status list:** #3, #4, #8 and #12, plus the rejected
1×07 proposal and the "checked and found correct" record.

## Status overview

**Legend** — ✅ **DONE** (v0.2, 2026-08-07) · ⏸ deferred (accepted risk) ·
✔ closed, no change needed · ❌ **rejected** (checked, the finding was wrong or
the fix is worse than the problem). **Struck through = closed**; anything not
struck through still has a write-up further down.

### 🔴 Fix before the BOM goes out

| # | Finding | Status |
|---|---|---|
| 1 | ~~0 Ω address straps: fitting both halves of either pair shorts the main board's +3V3 to GND~~ | ✅ **DONE** — straps are **10 k**; worst case 330 µA |

### 🟠 Will bite you

| # | Finding | Status |
|---|---|---|
| 2 | ~~`J1` = **B6B-ZR** (2.7 mm tail) on a **1.6 mm** board — JST specifies **B6B-ZR-3.4**~~ | ❌ **REJECTED** — `B6B-ZR-3.4` is made-to-order (MOQ 2,000, 16 wk). 2.7 mm still leaves 1.1 mm protruding = a normal TH joint. Deviation documented, J1/J7/J10/J11 all keep the stocked part |
| 3 | One through-hole part on an all-SMD double-reflow board forces a third process step | ⏸ **deferred** — the third operation is now written into the BOM note and README; price it before ordering. **The connector stays `B6B-ZR`** (see #4) |
| 4 | A reversed harness is destructive to the BNO085; the only guard is a sentence in the README | ⏸ **mitigated, connector unchanged** — warning block on the schematic sheet + README + J10's BOM line. **A 1×07 was proposed and rejected 2026-08-08: the connector is not changing** |
| 5 | ~~The BME688's handling rules are not in the fab package (Bosch's HSMI doc isn't in `datasheet/`)~~ | ✅ **DONE** — HSMI + packaging docs filed (rows 44/45); rules on the sheet, in README and `datasheet/README.md` |
| 6 | ~~Three load-bearing claims in `PCB_NOTES.md` are now false~~ | ✅ **DONE** — `PCB_NOTES.md` rewritten from measurements, `README.md` re-baselined |

### 🟡 Worth a pass

| # | Finding | Status |
|---|---|---|
| 7 | ~~21 mm of track now runs inside the BME688 thermal island; the avoidable part is C4/C7's GND stubs~~ | ✅ **DONE** — 21.0 → **18.2 mm**, GND share 8.8 → **6.0 mm**. (The suggested 1 mm move was impossible: U1 and Y1 are in the way) |
| 8 | ~~C4/C7 pads intrude 0.53 / 0.61 mm into the Ø5.2 vent aperture~~ | ⏸ **improved, residue accepted** — moved the 0.175 mm that U1/Y1 allow → 0.36 / 0.44 mm; restored *"nothing on the back behind U2"* |
| 9 | ~~Six courtyard pairs are below the documented 0.22 mm floor~~ | ✅ **DONE** — clearance now judged on **pad-to-pad copper** (≥ 0.60 mm everywhere); the six courtyard pairs are named and justified in `COURTYARD_TIGHT` |
| 10 | ~~Seven reference designators are hidden; four are replaced by free silk text 2–3.5 mm off the part~~ | ✅ **DONE** — five un-hidden and placed, four free texts deleted; only H1/H2 hidden, and asserted |
| 11 | ~~Three revision markers disagree, and the PCB has no title block at all~~ | ✅ **DONE** — `v0.2` in the PCB title block, the schematic title block and the silk; a check fails if they diverge |
| 12 | In2.Cu is void under U1's bottom pad row — In1 is the real reference plane, not F.Cu | ⏸ **deferred** — recorded as an accepted deviation; In1 is a solid 339 mm² plane. Fix only if the board is ever re-laid |
| 13 | ~~`ALS_INT` floats at the main board's MCP23017 GPB3 whenever the harness is unplugged~~ | ✅ **DONE** — `FIRMWARE.md` **R-BOARD-4** (`GPPU.3 = 1`) |
| 14 | ~~F.Silk outline + the `ALS` legend sit inside the Ø4.0 optical window~~ | ✅ **DONE** — legend moved; U3 on a project-local footprint fork with the silk trimmed |
| 15 | ~~H1/H2's fastener keepout overhangs the rounded board corners~~ | ❌ **REJECTED** — it does not. Measured against real Edge.Cuts geometry the keepout sits **0.53 / 0.52 mm inside** the board; the finding's shortcut under-reports |
| 16 | ~~Three `~*.lck` files are committed despite `.gitignore`~~ | ✅ **DONE** — `git rm --cached` |
| 17 | ~~ERC suppresses *"Global label only appears once"*~~ | ✅ **DONE** — back to `warning`; ERC still 0 |
| 18 | ~~`README.md` and `b_imu.py` both say `CLKSEL0` is strapped low "via a 0 Ω" — it is a bare wire to GND~~ | ✅ **DONE** — both comments corrected, no resistor added |
| 19 | ~~`kicad/REVIEW.md:731` lists the BME688 at **0x76**~~ | ✅ **DONE** — now **0x77** |

---

# ⏸ Still live

The four findings that were not closed. Everything else in the review is done,
rejected, or recorded above.

## 3. One through-hole part on an all-SMD, double-sided reflow board

`PCB_NOTES.md` says *"double-sided SMT reflow (PCBA) — no hand soldering"*, and
`README.md §Assembly` says the leadless sensors force fab assembly. But **J1 is
through-hole**, so the board actually needs **three** operations: reflow side 1, reflow
side 2, then selective-solder or hand-solder J1. `parts_db.py` already says so in J1's
note, which is good — but it is worth pricing before committing, because that third
operation is often quoted per-board, not per-joint.

**There is a drop-in family alternative.** The ZH series has SMT headers with brass
reinforcement (datasheet, *Header/SMT type/SM4 type*): **`B6B-ZR-SM4-TF`** — top entry,
6 circuits, same 1.5 mm pitch, **same ZHR-6 housing**, so the harness is unchanged. It
removes the only TH part on the board.

Trade-off worth stating plainly: an SMT connector has less pull-out strength than a TH
one. The SM4 variant exists precisely for that (the reinforcement tabs are soldered
down), and this connector is plugged once, inside a wooden box, on a strain-relieved
harness. If you want the mechanical margin instead, keep TH and just fix the tail
length (#2).

## 4. A reversed harness is destructive, and the only thing preventing it is prose

Both ends of the harness are ZHR-6 on identical A06ZR pre-crimped cable, and `J1`
mirrors `J7` 1:1 — so a cable loaded backwards plugs in perfectly. Trace what happens
(sensor pin ← main pin):

| Sensor J1 | receives main J7 | consequence |
|---|---|---|
| 1 `GND` | 6 `ALS_INT` | the board's ground reference is an MCP23017 input pin |
| 2 `+3V3` | 5 `SENSOR_INT` | the rail is fed **through R97's 10 k** — never comes up |
| 3/4 `SDA`/`SCL` | 4/3 `SCL`/`SDA` | swapped (harmless on its own) |
| 5 `SENSOR_INT` | 2 `+3V3` | **3.3 V onto the BNO085's `H_INTN` push-pull output while its VDDIO ≈ 0** |
| 6 `ALS_INT` | 1 `GND` | TSL2591 `INT` held low |

Row 5 is the damaging one. BNO08X abs-max, Figure 6-1: *"Voltage at any logic pin =
**VDDIO + 0.3 V**"*. With VDDIO near zero, 3.3 V on pin 14 is ~3 V over abs max into the
ESD clamp, current-limited only by R97. **Assume the BNO085 is destroyed.** At $13.57 it
is also the most expensive part on either board.

The **other** mis-mate is the one `kicad/REVIEW.md` #10 already tracks from the main
board's side: `J10` (knob) is the same connector 13.5 mm away with **+5 V on pin 2**.
From this board's side that is worse than a reversed cable — 5 V lands on `+3V3` and
takes out all three sensors at once (abs max: BNO085 VDDIO 3.63 V, TSL2591 3.8 V,
BME688 4.25 V).

**See the combined recommendation below** — one connector change closes both, and buys
something else on top.

> **2026-08-08: the connector change was rejected** (below). The hazard stands
> and is handled by marking: `SENSOR` / `KNOB` on the main board's B.Silk, the
> warning block beside J1 on the schematic sheet, `README.md`, and J10's BOM
> line. Label both harnesses to match the silk.

**8. C4 and C7 poke into the Ø5.2 vent aperture.** `User.Drawings` puts the vent circle
dead-centre on U2 at (4.50, 10.00), r = 2.60 — an improvement over the (4.3, 11.0) in
`PCB_NOTES.md`. But on the back:

| pad | nearest point to vent centre | verdict |
|---|---|---|
| `C4.2` (GND) | 2.07 mm | **inside by 0.53 mm** |
| `C7.2` (GND) | 1.99 mm | **inside by 0.61 mm** |

Consequence is modest — 0603s are ~0.5 mm tall and the vent still breathes — but it
means the enclosure vent cannot be a plain drilled hole with mesh pressed flat against
the back face, which is exactly the freedom `PCB_NOTES.md` was buying. Same fix as #7:
move both caps east.

**12. In2.Cu is void under U1's bottom pad row.** U1 sits on B.Cu, so its nearest
reference plane is In2 at 0.48 mm. GND fill under U1's 5.0 × 3.6 mm footprint area:

| layer | distance from U1 | fill |
|---|---|---|
| B.Cu (same layer) | — | 5.7 % |
| **In2.Cu** | 0.48 mm | **26.3 %** — completely void beneath the bottom pad row (pads 20–28) |
| In1.Cu | 0.96 mm | **67.1 %** — void beneath the host-interface pads (16–20) |
| F.Cu | 1.50 mm | 27.4 % |

The cause is `I2C_SDA`/`I2C_SCL` taking a long inner-layer detour: SDA is routed
**53.8 mm** against a 28.6 mm minimum spanning tree (**1.88×**), 20.5 mm of it on In2;
SCL is 40.9 mm against 27.3 mm (**1.50×**), 16.0 mm on In2. Every other signal net on
the board is between 0.97× and 1.45×.

**How much does this matter? Not much, and I want to be clear about that.** The
fastest edge on the board is a 400 kHz I²C rise, and the plane fragmentation is not
severe — In2 is one 289 mm² island plus two small ones (16.8 and 0.7 mm²), **every
island is stitched** (checked: no orphan copper anywhere on any layer), and In1 gives a
continuous 339 mm² plane one layer further out. Nothing here will misbehave.

What it *does* mean is that the reference plane CEVA asks for is one dielectric further
away than the docs claim, and that In2 was spent as a routing layer on a board that
didn't need one — F.Cu carries only 100 mm of track on 480 mm². If the board is ever
re-laid, pull SDA/SCL back onto F.Cu/B.Cu and take In2 back as a plane.


## ~~Recommendation — go to a **1×07** connector on the next revision~~ ❌ REJECTED 2026-08-08

> **Decision: the connector does not change. J1/J7/J10 stay `B6B-ZR` (ZH 1×06),
> and the harness stays the pre-crimped `A06ZR` ZHR-6 cable.** Not deferred, not
> "next spin" — closed.
>
> The three items below therefore keep their existing resolutions, which are
> good enough:
>
> | item | how it is handled instead |
> |---|---|
> | J7/J10 mis-mate (+5 V onto +3V3) | **Marking, already on the board.** `kicad/REVIEW.md` #10 closed with **`SENSOR` and `KNOB` on B.SilkS** beside the two headers (`637be57`) — the right cable is named at the point of use. Plus the hazard on the schematic sheet, in `README.md`, on J10's BOM line and in the root `README.md`. The two connectors are plugged once, at build time, inside a wooden box. |
> | reversed harness kills the BNO085 | Same — plus the warning block is now printed next to J1 on the sheet, where whoever assembles it is actually looking. |
> | **R-BOARD-3** — firmware cannot reset the BNO085 | **Permanently accepted.** The `R6`/`C9` POR covers cold start and a full rail collapse; the residual case is a rail dip that recovers inside ~1 ms, whose remedy is a power cycle. `FIRMWARE.md` already specifies degrading gracefully. |
>
> Cost avoided: a footprint swap on **two** boards, re-routing J7's fan-out on
> an already-routed 1884-segment main board, a new MCP23017 net and a new cable
> part — against three problems that are all either procedural or already
> accepted. The pin-7 argument was also weaker than it looked: the spare pin
> can be a **second GND** (end-symmetric, so a reversed cable is benign) **or**
> an `NRST` host line, not both.

**Why it was worth considering, for the record.** `R-BOARD-3` is the one item
that is otherwise permanently stuck: `NRST` has only the `R6`/`C9` 10 k/100 nF
power-on reset and `TP1`. That RC is fine at cold start (τ = 1 ms, and t_nrst
min is 10 ns per Fig. 6-8), and the BNO085's own POR at VDDIO 0.99 V covers a
full rail collapse — but a rail dip that recovers inside ~1 ms leaves C9
charged, so the part can come back half-reset with **no way for firmware to
clear it**, and the only remedy is power-cycling the whole clock. The main
board does have spare expander pins (`kicad/REVIEW.md` lists GPA4-6 free), so
if it is ever re-spun for another reason, this is the case to re-read.

---


# ✅ Checked and found correct

This board's schematic held up to everything I checked it against. Recorded so it does
not get re-checked later. *(Numbers marked **v0.2** were re-measured after the
2026-08-07 pass; everything else was unaffected by it.)*

- **BNO085 vs CEVA's I²C reference design (Fig. 1-11, notes 1–8) — all eight honoured:**
  1. `H_INTN` → `SENSOR_INT` → a host GPIO (the *wake-capable* caveat is already
     documented in `README.md`: IO42 is not an RTC GPIO, so light-sleep wake only).
  2. `NRST` driven by a board reset (the R6/C9 POR) — permitted by the note, which
     allows "the application processor **or** the board reset".
  3/4. `BOOTN` pulled high through **10 k** (`R7`) — the exact value the note names —
     with `TP2` for DFU entry.
  5. `PS1` and `PS0` both **tied to ground** → I²C (Fig. 1-5).
  6. `SA0` strapped → **0x4A** (Fig. 1-12: `1001010` + SA0).
  7. Bus pull-ups **3.2 kΩ effective**, inside the note's stated 2–4 kΩ window.
  8. `ENV_SCL`/`ENV_SDA` **pulled up though unused** (`R8`/`R9`) — the note's exact
     requirement, because SH-2 probes that bus at every reset. Correctly **not** tied to
     the host bus, where the BNO085 would be master.
- **BNO085 straps and support** — `CLKSEL0 = 0` selects the crystal (Fig. 1-8, *"0 or
  unconnected"*); `CAP` pin 9 gets **100 nF to GND**, the pin table's literal wording;
  `H_CSN` pulled to VDDIO is a safe deviation (unused in I²C, not sampled at reset,
  and `CSN` high is the inactive level in every mode).
- **BNO085 power sequencing** — §6.3 requires VDD to reach level *before or with* VDDIO.
  One shared 3.3 V rail satisfies that by construction. VDD 2.4–3.6 V and VDDIO
  1.7–3.6 V both comfortably met; abs max VDDIO 3.63 V vs the main board's 3.32 V.
- **32.768 kHz crystal loading is right.** CEVA asks for *"50 ppm with 12.5 pF capacitor
  loading"*; the ABS07 is **12.5 pF CL, ±20 ppm**, and C7/C8 = 22 pF give
  C_eff = 11 pF + 1.5–3 pF stray ≈ **12.5–14 pF**. Layout is tight where it counts:
  U1.27→Y1.2 **2.20 mm**, U1.26→Y1.1 **2.61 mm**, Y1→C7 **2.10 mm** (v0.2, was
  2.28), Y1→C8 2.20 mm, no
  crossover (Y1.1→pin 26, Y1.2→pin 27), both nets entirely on B.Cu with no vias, and
  **In2 is 100 % filled under the crystal** (In1 92 %). This is the best-referenced net
  on the board.
- **BME688 vs §7.2 connection diagram (a)** — `CSB` **hard-tied to VDDIO** (§6.1: pull
  it low once and the part latches into SPI until the next power-on reset); `SDO`
  strapped, never floating (§6.2); **100 nF on each supply** (C10/C11), the datasheet's
  own recommended value, at 2.36 / 2.57 mm.
- **TSL2591** — `TSL25911FN` is the correct ordering code for a **VDD-referenced I²C
  bus** (the TSL25913FN is the 1.8 V-bus part); **1 µF low-ESR at VDD 2.59 mm away**,
  which is the one hard placement rule the datasheet gives; `INT` open-drain with the
  **10 k** the Application Information suggests; pin 4 `NC — do not connect` left on its
  own unconnected net.
- **I²C bus** — 4.7 k (main) ∥ 10 k (here) = **3.20 kΩ**. At an estimated 40–100 pF of
  bus capacitance (200 mm harness + this board's own microstrip, ~3.5 pF on SDA, +
  device pins), t_r = 0.847·R·C ≈ **160–270 ns**, inside Fast Mode's 300 ns; sink
  current 1.03 mA against ≥ 3 mA of drive on every device. Depopulating R1/R2 for
  100 kHz operation, as `b_host.py` suggests, also works.
- **No address collisions** — 0x4A (BNO085) / 0x77 (BME688) / 0x29 (TSL2591) vs the main
  board's 0x20 (MCP23017) and 0x62-0x63 (TAS5760M).
- **`SENSOR_INT` drive** — `H_INTN` is push-pull (Fig. 6-3 gives V_OH/V_OL, not an
  open-drain spec), so the main board's R97 10 k is redundant but harmless: 0.33 mA
  while asserted. Already recorded in `FIRMWARE.md` §6.5.1.
- **Standing current is negligible.** No strap resistor carries current (each strap
  ties a CMOS input to a rail — **10 k since v0.2**, and it still carries nothing);
  the pull-ups only conduct when their line is pulled low. In the tap-only configuration `FIRMWARE.md` specifies, U1 draws
  **0.28 mA** (Fig. 6-17) — the whole board is well under 1 mA average, with the
  BME688's 18 mA heater peak (Table 2) covered by C1's 10 µF at the connector.
- **PCB electrical margins** — 0.25 mm × 35 µm ≈ 0.9 A of capacity against a ~35 mA
  peak load. All 110 mm of `+3V3` copper in series would be 216 mΩ, so the drop at the
  BME688's 18 mA heater peak is **under 3.9 mV** whatever the actual path. A power net
  class would be pointless here (unlike the main board's #4).
- **Every GND zone island is stitched** — 21 GND vias and 23 GND pads across 17 filled
  islands on four layers, and **not one island lacks a via or pad**. No orphaned copper.
- **Schematic ↔ PCB parity is exact** — `kicad-cli pcb drc` reports 0 parity issues, and
  `pcb_check.py` independently matches **all 100 schematic pins** to a pad carrying the
  right net, with no netless copper pad.
- **BOM is assembly-ready** — all 29 BOM parts carry MPN / Manufacturer / Package /
  Description; board fields match the schematic's; the DNP set (`R4`, `R11`) and the
  off-BOM set (`TP1`, `TP2`) agree between both files, with `in_pos_files no` on the
  test pads so *Update PCB from Schematic* cannot put them back.
- **3D models resolve for all 29 real parts** (the four without one are the two NPTH
  holes and the two test pads), including J1's `OVERRIDES` entry, whose stock model is
  absent from the KiCad 10 install. *(v0.2: U3 no longer needs an override — its
  footprint fork `sensor:AMS_TSL25911FN_ALSWindow` carries the model path itself.)*
- **U1's land pattern** — the vendor (SnapEDA) land in `sensor.pretty`, measured
  **5.2000 × 3.8000 mm** against BNO08X Fig. 7-2, with SnapEDA's 0.102 mm per-pad mask
  margin correctly stripped (at 0.5 mm pitch it would have left a 0.046 mm mask web,
  below any fab's dam minimum) and the pin-1 dot grown to Ø0.42.
- **Placement intent that survived the hand pass** — U2 is 8.5 mm from U1 and 22.8 mm
  from J1's PVC harness; the vent and window circles are centred **exactly** on U2 and
  U3; nothing but U3's own pads is inside the optical window — **on copper or, since
  v0.2, on silk**; U1 is 0.04 mm off the line between the two screws; both mounting
  holes are NPTH, keeping the ground single-point through J1 pin 1.
