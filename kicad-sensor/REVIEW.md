# Design review — `sensor.kicad_sch` / `sensor.kicad_pcb`

**Date:** 2026-08-05 · **Reviewed at commit:** `88caebf` ·
`sensor.kicad_sch` md5 `e1f55fe8961e223662f11b4b927c64f0` ·
`sensor.kicad_pcb` md5 `b02be8381c61b9cc05c338ffa9f0fa76`

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

Everything below is what those tools structurally cannot see, and it clusters into
three groups: **one fail-safe change worth making before the BOM goes out**, a handful
of **fab-package and mechanical items**, and a **documentation gap** — the board was
hand-routed and moved to 4 layers on 2026-08-02, and `PCB_NOTES.md` / `README.md`
still describe the 2-layer unrouted board that no longer exists.

## Verification baseline

```
kicad-cli sch erc --severity-all sensor.kicad_sch  → 0 violations
kicad-cli pcb drc --severity-all sensor.kicad_pcb  → 0 violations, 0 unconnected,
                                                     0 schematic-parity issues
gen/pcb_check.py                                   → 17 pass, 4 fail (all stale checks — see W2)
```

DRC's own ignored set is KiCad's default (`missing_courtyard`,
`track_not_centered_on_via`, `tuning_profile_track_geometries`,
`footprint_filters_mismatch`, `footprint_type_mismatch`) — none of them is masking
anything on this board; every footprint has a courtyard and J1 is correctly attributed
`through_hole`.

Findings were derived from the exported netlist, a `pcbnew` parse of the board (pads,
tracks, vias, zone fills, courtyards), and the vendor datasheets.

**Board as built:** 30 × 16 mm · **4 layers** (F/In1/In2/B), 1.6 mm · 33 footprints ·
194 track segments, all 0.25 mm · 30 through vias (0.6/0.3), 21 of them GND ·
4 GND pours + 1 rule area · 26 nets.

---

## ⚠ Workflow — read before touching anything

| # | Item | |
|---|---|---|
| **W1** | **`gen/pcb_build.py` has no destructive-run guard.** It calls `pcbnew.CreateEmptyBoard()` and overwrites `sensor.kicad_pcb`. The board now holds **194 tracks, 30 vias, a 4-layer stackup and a hand-tuned placement** the generator never produced (commits `b3fc33e`, `a020218`). **Running it destroys all of that.** The main board got exactly this guard in Phase 0 (`kicad/gen/pcb_build.py:982`); this project never did. | 🔴 |
| **W2** | **The docs describe a board that no longer exists.** `PCB_NOTES.md` and `README.md` say *2 layers*, *"no traces are routed"*, and give a floor plan and hole positions that have all changed. `README.md` already carries a ⚠ admitting the checks are stale — but the *design rationale* around them is stale too (see #6). | 🟠 |
| **W3** | **Schematic is still generator-driven**: edit `gen/b_*.py`, then **`build.py` followed by `stamp_bom.py`**. `build.py` alone drops every MPN/Manufacturer/Package/Notes field **and both DNP flags** — which is exactly the mechanism behind finding #1. `build.py` also rewrites `sensor.kicad_pro` wholesale via `project.write_project()`. | 🟠 |
| **W4** | **There is no `sync_pcb.py` for this project.** The main board's `kicad/gen/sync_pcb.py` (headless *Update PCB from Schematic*) is generic enough to be re-pointed here if the schematic ever changes again — otherwise the only path is F8 in pcbnew. | 🟡 |

---

## Status overview

**Legend** — ⏳ open · ⏸ deferred (accepted risk) · ✔ accepted (no change)

### 🔴 Fix before the BOM goes out

| # | Finding | Cost |
|---|---|---|
| 1 | 0 Ω address straps: fitting both halves of either pair shorts the main board's +3V3 to GND | 2 BOM values |

### 🟠 Will bite you

| # | Finding | Where |
|---|---|---|
| 2 | `J1` = **B6B-ZR** (2.7 mm tail) on a **1.6 mm** board — JST specifies **B6B-ZR-3.4** | BOM (**and the main board's J7**) |
| 3 | One through-hole part on an all-SMD double-reflow board forces a third process step | assembly / BOM |
| 4 | A reversed harness is destructive to the BNO085; the only guard is a sentence in the README | cross-board |
| 5 | The BME688's handling rules are not in the fab package (Bosch's HSMI doc isn't in `datasheet/`) | assembly notes |
| 6 | Three load-bearing claims in `PCB_NOTES.md` are now false | docs |

### 🟡 Worth a pass

| # | Finding |
|---|---|
| 7 | 21 mm of track now runs inside the BME688 thermal island; the avoidable part is C4/C7's GND stubs |
| 8 | C4/C7 pads intrude 0.53 / 0.61 mm into the Ø5.2 vent aperture |
| 9 | Six courtyard pairs are below the documented 0.22 mm floor |
| 10 | Seven reference designators are hidden; four are replaced by free silk text 2–3.5 mm off the part |
| 11 | Three revision markers disagree, and the PCB has no title block at all |
| 12 | In2.Cu is void under U1's bottom pad row — In1 is the real reference plane, not F.Cu |
| 13 | `ALS_INT` floats at the main board's MCP23017 GPB3 whenever the harness is unplugged |
| 14 | F.Silk outline + the `ALS` legend sit inside the Ø4.0 optical window |
| 15 | H1/H2's fastener keepout overhangs the rounded board corners |
| 16 | Three `~*.lck` files are committed despite `.gitignore` |
| 17 | ERC suppresses *"Global label only appears once"* |
| 18 | `README.md` and `b_imu.py` both say `CLKSEL0` is strapped low "via a 0 Ω" — it is a bare wire to GND |
| 19 | `kicad/REVIEW.md:731` lists the BME688 at **0x76**; the board and every other doc say **0x77** |

---

# 🔴 Stop — fix before fab

## 1. The 0 Ω address straps turn one assembly slip into a dead short on the main board

**The circuit as drawn is correct.** This is about what happens when it is built wrong.

| Pair | Fitted | Alternate (DNP) | Both fitted ⇒ |
|---|---|---|---|
| BNO085 `SA0` | `R3` **0 Ω** → GND (0x4A) | `R4` **0 Ω** → +3V3 (0x4B) | **+3V3 shorted to GND through 0 Ω** |
| BME688 `SDO` | `R10` **0 Ω** → +3V3 (0x77) | `R11` **0 Ω** → GND (0x76) | **+3V3 shorted to GND through 0 Ω** |

**Why this is not hypothetical.** `README.md` records that it already nearly happened:
before 2026-08-01 the DNP state lived *only in the value strings*, so **a BOM exported
then told the assembler to fit all four**. It is fixed now — but the only thing
standing between the current files and that outcome is a pair of `(dnp yes)` flags
written by `stamp_bom.py`, and **both generators are documented to erase them**
(`README.md`: *"Re-run it after every `build.py` / `pcb_build.py` rebuild — both
generators write their file from scratch and drop the fields"*). That is a fragile
guard for a failure this expensive.

**Blast radius.** The short is not local: `+3V3` and `GND` arrive on J1 from the main
board, so a mis-built daughterboard shorts the **main board's 3.3 V rail**. The
TLV62569 goes into current limit / hiccup, the whole clock fails to boot, and the fault
is on a board with no LEDs, no silk hint, and no obvious reason to suspect it. Expect
hours of debugging.

**Fix — change both pairs from 0 Ω to 10 kΩ.** Worst case then becomes **330 µA**
instead of a short, and the board still boots and still enumerates so you can *see* the
wrong address on the bus. Both pins take a resistive strap without complaint:

- BNO085 `SA0` (pin 17) is a CMOS input sampled at reset (§1.2.2.1) — 10 k gives
  ≥ 3.29 V / ≤ 1 mV against a V_IH of 0.55·VDDIO = 1.82 V.
- BME688 `SDO` (pin 5) is **address-select only in I²C mode** — Table 26 lists it as
  *"GND for default address"* under the I²C column and it is never driven. 10 k is
  what the Adafruit 5046 breakout this design copies uses.

Value change only: same lands, same footprints, no PCB impact. Do it in
`gen/b_imu.py` (`pu("R4", …, "0R (DNP)")` / `pd("R3", …, "0R")`) and `gen/b_env.py`
(`R10`/`R11`), then re-run `build.py` **and** `stamp_bom.py`.

---

# 🟠 Will bite you

## 2. `J1` is the wrong tail length for a 1.6 mm board — and so is the main board's `J7`

`datasheet/connector_jst_zh.pdf`, header/TH type notes:

> "2.7 mm soldering length is suitable for **0.6 mm to 1.2 mm** board thickness.
> **3.4 mm** soldering length is suitable for **1.6 mm** board thickness."

| | |
|---|---|
| Board thickness | **1.6 mm** (stackup: 4 × 0.035 Cu + 3 × 0.48 FR4 + masks = 1.60) |
| `gen/parts_db.py` MPN | `B6B-ZR(LF)(SN)` — the **2.7 mm** tail |
| Correct part | **`B6B-ZR-3.4(LF)(SN)`** |

Same footprint, same 1.5 mm pitch, same ZHR-6 mating housing — **a one-word BOM
change**. A 2.7 mm tail in a 1.6 mm board leaves 1.1 mm protruding, which will solder,
but the fillet is thin and the wafer's seating is what JST is specifying against; on a
connector that takes a cable tug inside the cube, take the free upgrade.

`PCB_NOTES.md` already flags this ("Footprint-compatible, BOM-line change only") but it
was never actioned, and **the main board has the identical mistake**: `kicad/gen/parts_db.py`
line 184 gives J7 as `B6B-ZR(LF)(SN)` on a board that is also 1.6 mm. It is not in
`kicad/REVIEW.md` — worth adding there.

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

## 5. The BME688's handling rules are not in the fab package

The board is ordered **fab-assembled**, and the gas sensor is the one part on it that a
routine, correctly-executed assembly process can permanently ruin — a metal-oxide
hotplate that has been washed, fluxed over, or exposed to silicone does not fail, it
just reads a wrong VOC baseline forever.

`sensor_env_bme688.pdf` §7.7 does not contain the rules; it **defers to a separate
document**:

> "This **HSMI**-document provides all the necessary instructions to handle, solder and
> mount the environmental sensor BME688. Following the reported guidelines is very
> important to prevent the damage of the sensor and the resultant loss of warranty."

That document is **not in `datasheet/`**, and the fab package carries nothing beyond
`parts_db.py`'s *"do not conformal-coat, keep the vent clear"*.

What the datasheet itself does pin down and should go in the assembly note:
**MSL 1** (no bake needed), **peak reflow 260 °C** (§7.6), and **≥ 50 µm minimum solder
height after reflow** — the last one is a real constraint, specified *"for good
mechanical decoupling between the sensor device and the PCB"*, and it is a stencil
thickness decision, not a layout one.

**Action:** pull Bosch's HSMI into `datasheet/`, and add an explicit assembly note
covering cleaning process, flux residue, and siloxane exposure before ordering.

## 6. `PCB_NOTES.md` describes a board that no longer exists

The hand pass on 2026-08-02 (`b3fc33e`, `a020218`) went to 4 layers, routed the board,
and moved parts. `README.md` flags the *checks* as stale; the **rationale** is stale
too, and three of its claims are now measurably false:

| `PCB_NOTES.md` says | Measured today |
|---|---|
| *"**Two layers**, not four: the only fast signal is 400 kHz I²C, and a split ground would work against the one thermal requirement"* | **4 layers.** In1/In2 are GND pours, and In1 is genuinely solid (339 mm², one island) — this was an **improvement**, not a mistake. The User.Drawings note already says *"30 x 16 mm, 4 layer"*; only the markdown lags. |
| *"the **F.Cu pour is essentially solid under U1** (341 mm² filled), giving the IMU the reference plane CEVA asks for"* | F.Cu is **268 mm² in 5 islands** and only **27 %** filled under U1. See #12. |
| *"**Nothing on the back behind** [U2]… so the enclosure vent can be a plain hole"* | **C4 and C7 are behind it**, and both intrude into the vent circle. See #7, #8. |
| *"tightest pair 0.25 mm… **0 violations** at the 0.22 mm floor"* | **six** pairs below 0.22 mm, two at 0.000. See #9. |
| *"H1 (3.1, 3.1) and H2 (26.5, 12.6), **25.3 mm apart**"* | H1 (3.00, 3.00), H2 (27.00, 13.00), **26.0 mm apart**. |
| *"[U2] **9.1 mm from U1**… **23 mm from J1**"* | 8.5 mm and 22.8 mm — the intent survives, the numbers moved. |
| *"U1 sits **on the line between the two M2 screws** (1.2 mm off it)"* | Still true, and now **better**: 0.04 mm off the line. |
| *"3 silkscreen-clearance warnings"* | **0** — the hand pass cleared them, by hiding seven references (#10). |

Also `README.md`'s *"PCB: 30 × 16 mm, 2 layers, placed 2026-07-30 — **not routed**"* and
its *"**4. Route it** — the board carries placement + pours + nets, no traces"* are both
wrong now, as is `PCB_NOTES.md` §*Next steps* item 1.

---

# 🟡 Worth a pass

**7. 21 mm of track now runs inside the BME688 thermal island.** The
`U2_thermal_island` rule area (1.5, 7.5 → 7.5, 12.5) correctly blocks the **pour** on
all four layers — verified, **0 % fill on every layer** inside it. But it explicitly
allows tracks, so DRC is silent while this accumulated:

| net | layer | length inside the island |
|---|---|---|
| `+3V3` | F.Cu | 5.47 mm |
| `GND` | B.Cu | 4.74 mm |
| `GND` | F.Cu | 4.02 mm |
| `Net-(U2-SDO)` | F.Cu | 1.80 mm |
| `I2C_SCL` | F.Cu | 1.54 mm |
| `I2C_SDA` | F.Cu | 1.31 mm |
| `I2C_SDA` | In2.Cu | 2.11 mm |
| **total** | | **20.99 mm** = 5.25 mm² of copper in a 30 mm² island (**17.5 %**) |

About 11 mm of that is U2's own five signals plus its supply and is unavoidable. **The
~8.8 mm of `GND` is not** — it exists only to reach C4's and C7's ground pads, which
were moved *into* the island by the hand pass. Move those two caps ~1 mm east (out past
x = 7.5) and the GND stubs go with them.

Honest magnitude: the island's stated purpose is to make board heat cross FR4 rather
than 35 µm of copper to reach the gas sensor, and the only heat source on this board is
U1 at **0.28 mA total in the tap-only configuration firmware actually uses**
(Fig. 6-17: 0.13 mA VDDIO + 0.15 mA VDD) — under 1 mW. So the thermal stakes here are
small; the real BME688 threat is the enclosure (amp, LEDs, battery). Treat this as
hygiene, and as protecting the *documented* intent rather than a measurable degree.

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

**9. Six courtyard pairs are below the documented 0.22 mm floor.** Requirement #1 in
`PCB_NOTES.md` was *"Part clearance ≥ 0.22 mm — **0 violations**"*:

| pair | courtyard gap | actual copper gap |
|---|---|---|
| `C1` ↔ `J1` | **0.000 mm** | **1.235 mm** (C1.2 ↔ J1.1) — fine |
| `H1` ↔ `TP2` | 0.000 mm | fastener keepout vs test pad, 0.57 mm of pad clearance — fine |
| `R9` ↔ `C8` | 0.040 mm | |
| `C9` ↔ `TP1` | 0.045 mm | |
| `R7` ↔ `C6` | 0.057 mm | |
| `R4` ↔ `R3` | 0.090 mm | |

I checked the two headline 0.000 pairs before reporting them and **neither is a real
problem** — C1's pad is 1.24 mm from J1's pin 1, which is enough room for an iron tip
when J1 is hand-soldered as the third operation. The four 0603-to-0603 pairs at
0.04–0.09 mm are the ones to look at: they are assemblable on a stencil, but they leave
no rework room and raise tombstone/bridge risk. Either open them up or lower the QA
floor in `pcb_check.py` deliberately — right now the check and the board simply
disagree.

**10. Seven reference designators are hidden.** `C11`, `H1`, `H2`, `U1`, `TP1`, `C8`,
`R1` have `ref_vis = False`. Four were replaced with hand-placed free text on the silk
layer, sitting some distance from what they name:

| free text | at | names | offset |
|---|---|---|---|
| `TP1` | (15.50, 1.50) B.Silk | TP1 pad at (12.00, 1.50) | **3.50 mm** |
| `U1` | (11.50, 4.50) B.Silk | U1 at (12.50, 7.00) | 2.69 mm (reads as "above the part" ✓) |
| `C8` | (18.50, 10.50) B.Silk | C8 at (16.23, 10.70) | 2.28 mm |
| `R1` | (20.50, 11.00) B.Silk | R1 at (20.00, 12.90) | 1.96 mm |

`TP1`'s label is 3.5 mm from its pad with C9 in between — genuinely ambiguous when
you're trying to hit `NRST` with a probe to enter DFU, which is the *only* reason that
pad exists. The rest are fine but drift. Note these are free text, not footprint
fields, so **`pcb_build.py` would not regenerate them** — they are hand-owned like the
routing.

**11. Three revision markers disagree, and the PCB has no title block.**

| source | says |
|---|---|
| `F.Silkscreen` | `SENSE v0.1` |
| `sensor.kicad_sch` title block | `rev "A"`, `date "2026-07-29"` |
| `PCB_NOTES.md` floor plan | `SENSOR v0.19` |
| `sensor.kicad_pcb` | **no title block at all** |

The silk is what ships on the physical board, and it currently reads `v0.1` — which
looks like `v0.19` truncated. Pick one scheme (the project's own `v0.19`, or a board
letter) and make the silk, the schematic title block and the docs agree; then give the
PCB a title block so the Gerber frame carries a name/rev/date. Same call as the main
board's *"Revision letter"* item — your numbering scheme, not mine to pick.

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

**13. `ALS_INT` floats at the main board when the harness is unplugged.** The pull-up
for the TSL2591's open-drain `INT` lives **only here** (`R12`, 10 k) — deliberate and
correct, since the main board had nothing on J7.6 when this was designed. But since
`kicad/REVIEW.md` #11 landed that pin on **MCP23017 GPB3**, the main board has a CMOS
input with no pull-up on it. Net `ALS_INT` has exactly two nodes: `J7.6` and `U13.4`.

With the sensor board unplugged (bench bring-up, service, a board built before its
daughterboard arrives), GPB3 floats: elevated supply current, and — if GPB3 is enabled
for interrupt-on-change — a stream of spurious `EXPANDER_INT` events hammering the
`board` task.

**Free fix, firmware side:** set `GPPU.3 = 1` to enable the MCP23017's internal ~100 k
pull-up on GPB3. Worth adding as a board requirement in `FIRMWARE.md` §6.5 alongside
R-BOARD-1/2/3.

**14. Silkscreen inside the Ø4.0 optical window.** The window is centred exactly on U3
at (11.50, 3.60). Only U3's own pads are inside it — good. But two F.Silk items are
also inside: U3's footprint outline lines at y = 2.23 and y = 4.85 (1.31 mm from
centre), and the `ALS` legend at (12.00, 1.50) whose top edge reaches ~1.65 mm from
centre. White silk is a strong diffuse reflector sitting ~1.3 mm from the photodiode
aperture, inside the enclosure window. The error it introduces is small and mostly
angle-dependent — but `PCB_NOTES.md` went to trouble over *"≥ 0.64 mm of clear
courtyard… nothing shadows a window"*, and this is the same concern from the other
direction. Cheap: move the `ALS` legend clear of the circle and trim the outline.

**15. The M2 fastener keepout overhangs the board corners.** `MountingHole_2.2mm_M2`'s
courtyard is a **Ø4.95 mm** circle — it models the screw head + washer, not the hole.
At H1 (3.00, 3.00) and H2 (27.00, 13.00) that circle reaches 0.525 mm past the straight
edges and **0.65 mm past the rounded corner arc** (distance from the hole centre to the
corner radius is 1.83 mm vs a 2.475 mm keepout). This is what `pcb_check.py`'s
`>= 0.35 mm inside the outline` rule is reporting.

Nothing electrical: the hole wall is 1.90 mm from the edge, both holes are NPTH, and no
part sits inside either keepout (nearest is TP2, 0.57 mm of pad clearance from H1). The
practical consequence is that an M2 washer will overhang the corner slightly. Worth a
deliberate decision — either pull both holes ~0.5 mm inboard, or accept it and relax
the check. As currently placed they sit further out than the generated (3.1, 3.1) /
(26.5, 12.6).

**16. Three `~*.lck` files are committed.** `.gitignore` has `*.lck`, but
`~sensor.kicad_pcb.lck`, `~sensor.kicad_pro.lck` and `~sensor.kicad_sch.lck` were added
before that rule and are still tracked (`git ls-files`). They contain
`{"hostname":"Tibos-MacBook-Air","username":"tibo"}` — anyone cloning the repo gets
"this file is already open by another user" prompts from KiCad. `git rm --cached` them.

**17. ERC suppresses *"Global label only appears once in the schematic"***
(`sensor-erc.rpt`, *Ignored checks*). On a single-sheet design where every inter-block
connection is a global label, that is the check that catches a typo'd label name.
**It hides nothing today** — I counted every global label and all ten appear ≥ 2 times
(`ALS_INT`, `BNO_CSN`, `BNO_ENV_SCL`, `BNO_ENV_SDA`, `SENSOR_INT` ×2; `BNO_BOOTN`,
`BNO_SA0` ×3; `BNO_NRST` ×4; `I2C_SCL`, `I2C_SDA` ×5) — but it is a latent trap for the
next edit. Re-enable it, or add a lint check in `build.py`.

**18. `CLKSEL0` is not strapped through a 0 Ω.** `README.md` ("*`CLKSEL0` strapped low
via a 0 Ω*") and `gen/b_imu.py`'s own docstring ("*CLKSEL0 is strapped low with a 0R
instead of a host GPIO*") both say there is a resistor. The code four lines later ties
pins 5, 6 and 10 straight to a GND rail symbol, and the netlist confirms it: `U1.10
CLKSEL0` sits directly on `GND` with no series part. The **wiring is correct** —
Fig. 1-8 gives *Crystal = CLKSEL0 "0 or unconnected"* — only the description is wrong.
Fix the two comments; do not add the resistor.

**19. `kicad/REVIEW.md:731` has the BME688 at the wrong address.** Its
*"no address collisions (0x20 / 0x6C / 0x76 / 0x29 / 0x4A)"* line says **0x76**. The
board straps `SDO` high through `R10` → **0x77**, and `esp32.md:156`, `FIRMWARE.md:629`
and this board's own README all say 0x77. No collision either way (nothing else is at
0x76 or 0x77), so it is a one-line documentation fix in the *main board's* review —
noted here because that is where it will mislead someone.

---

## Recommendation — go to a **1×07** connector on the next revision

Three separate open items collapse into one change, and it is worth doing them together
rather than one at a time:

| item | today | with a 1×07 |
|---|---|---|
| `kicad/REVIEW.md` **#10** — J7/J10 mis-mate destroys the sensor board (+5 V onto +3V3) | keying "still open" | **impossible** — a 6-way ZHR housing cannot enter a 7-way header |
| **#4** above — a reversed harness kills the BNO085 | a sentence in the README | still possible; but pin 7 as a **second GND** makes the pinout end-symmetric and turns reversal into a benign fault |
| `FIRMWARE.md` **R-BOARD-3** — firmware cannot reset the BNO085 | accepted, degrade gracefully | **`NRST` gets a host line** on the spare pin |

`R-BOARD-3` is the one that is otherwise permanently stuck. As built, `NRST` has only
the `R6`/`C9` 10 k/100 nF power-on reset and `TP1`. That RC is fine at cold start
(τ = 1 ms, and tnrst min is 10 ns per Fig. 6-8), and the BNO085's own POR at
VDDIO 0.99 V covers a full rail collapse — but a rail dip that recovers inside ~1 ms
leaves C9 charged, so the part may come back half-reset with **no way for firmware to
clear it**. Today the only remedy is power-cycling the whole clock.

The main board has spare expander pins for it (`kicad/REVIEW.md` lists **GPA4-6** free),
so the cost is: one wire, `B7B-ZR-3.4` both ends, `A07ZR` cable, one MCP23017 pin, and
a footprint swap on two boards. Do it at the same time as #2's tail-length fix, since
that is the same BOM line.

**Not urgent** — nothing here blocks building the current board and bringing it up. It
is the thing to fold into the next spin.

---

## Work order

| # | Change | Files | Impact |
|---|---|---|---|
| **W1** | Add the destructive-run guard from `kicad/gen/pcb_build.py:982` | `gen/pcb_build.py` | **do this first** |
| **1** | `R3`/`R4`/`R10`/`R11` 0 Ω → **10 k** | `gen/b_imu.py`, `gen/b_env.py`, then `build.py` + `stamp_bom.py` | value-only, no PCB impact |
| **2** | J1 MPN → `B6B-ZR-3.4(LF)(SN)`; same for main-board J7 | `gen/parts_db.py`, `kicad/gen/parts_db.py` | BOM line only |
| **5** | Fetch Bosch's HSMI into `datasheet/`; write the assembly note (MSL 1, 260 °C peak, ≥ 50 µm solder height, cleaning/siloxane rules) | `datasheet/`, `README.md` | before ordering |
| **13** | `GPPU.3 = 1` on the MCP23017 | `FIRMWARE.md` §6.5 | firmware, free |
| **7, 8** | Move `C4` and `C7` east past x = 7.5 | pcbnew (hand) | ~2 parts + their GND stubs |
| **10, 14** | Un-hide `TP1`'s label (or move the free text onto the pad); move the `ALS` legend out of the window | pcbnew (hand) | silk only |
| **11** | Reconcile silk / title block / docs revision; add a PCB title block | pcbnew + `gen/build.py` | before the fab package |
| **16** | `git rm --cached kicad-sensor/~*.lck` | repo | housekeeping |
| **17** | Re-enable the global-label ERC check | `sensor.kicad_pro` | free |
| **18, 6** | Correct the CLKSEL0 comments; rewrite `PCB_NOTES.md` and `README.md` for the 4-layer routed board; re-baseline the 4 stale `pcb_check.py` checks | docs, `gen/pcb_check.py` | |
| **9, 15** | Decide: open the four 0603 pairs and pull the holes inboard, **or** relax the QA floors deliberately | pcbnew / `gen/pcb_check.py` | your call |
| **12** | If the board is ever re-laid: SDA/SCL back onto F.Cu, take In2 back as a plane | pcbnew | optional |
| **3** | Price the third assembly operation; consider `B6B-ZR-SM4-TF` | BOM / footprint | optional |
| **4** | Next revision: 1×07 connector — see above | both boards | next spin |

---

# ✅ Checked and found correct

This board's schematic held up to everything I checked it against. Recorded so it does
not get re-checked later:

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
  U1.27→Y1.2 **2.20 mm**, U1.26→Y1.1 **2.61 mm**, Y1→C7 2.28 mm, Y1→C8 2.20 mm, no
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
- **Standing current is negligible.** No strap resistor carries current (both 0 Ω
  straps tie a CMOS input to a rail); the pull-ups only conduct when their line is
  pulled low. In the tap-only configuration `FIRMWARE.md` specifies, U1 draws
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
  holes and the two test pads), including the two `OVERRIDES` for U3 and J1 whose stock
  models are absent from the KiCad 10 install.
- **U1's land pattern** — the vendor (SnapEDA) land in `sensor.pretty`, measured
  **5.2000 × 3.8000 mm** against BNO08X Fig. 7-2, with SnapEDA's 0.102 mm per-pad mask
  margin correctly stripped (at 0.5 mm pitch it would have left a 0.046 mm mask web,
  below any fab's dam minimum) and the pin-1 dot grown to Ø0.42.
- **Placement intent that survived the hand pass** — U2 is 8.5 mm from U1 and 22.8 mm
  from J1's PVC harness; the vent and window circles are centred **exactly** on U2 and
  U3; nothing but U3's own pads is inside the optical window; U1 is 0.04 mm off the line
  between the two screws; both mounting holes are NPTH, keeping the ground single-point
  through J1 pin 1.
