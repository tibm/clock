# Sensor board — PCB layout (**v0.2**, hand-routed, 2026-08-07)

`sensor.kicad_pcb` — **30 × 16 mm, 4 layers, fully routed**.

> ⚠ **This board is hand-owned. `gen/pcb_build.py` no longer describes it.**
> That script produced the 2026-07-30 *2-layer, unrouted* placement; the hand
> pass on 2026-08-02 (`b3fc33e`, `a020218`) went to 4 layers, moved parts and
> routed the whole board, and the v0.2 pass on 2026-08-07 edited it again.
> `pcb_build.py` would throw all of that away, so **it refuses to run** unless
> you set `PCB_BUILD_WIPE_ROUTING=1`. Make placement and routing changes in
> **pcbnew**, and pull schematic edits across with **Update PCB from Schematic
> (F8)**, which preserves routing.
>
> What *is* still scripted, and safe to re-run at any time:
>
> ```
> cd gen
> python3 stamp_bom.py                      # BOM fields + DNP flags, both files
> KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9
> $KPY pcb_fill.py                          # refill the four GND pours
> $KPY pcb_check.py                         # independent re-check of the saved file
> $KPY pcb_canon.py                         # re-serialize through pcbnew's writer
> /Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli pcb drc --severity-all ../sensor.kicad_pcb
> ```

## Board

| | |
|---|---|
| Size | **30 × 16 mm** (4.8 cm²), rounded corners r = 1 mm |
| Stack | **4 layers** (F.Cu / In1.Cu / In2.Cu / B.Cu), 1.6 mm FR4, GND pour on **all four**, all filled |
| Fill | F 269 · In1 **339** · In2 307 · B 177 mm² |
| Routing | **193 segments, all 0.25 mm · 30 vias (0.6/0.3), 21 of them GND · 26 nets · 330 mm of copper** |
| Rules | clearance **0.22 mm**, track 0.25, via 0.6/0.3, min drill 0.3, copper→edge 0.5 |
| Mounting | 2 × **M2 NPTH** (Ø2.2) at (3, 3) and (27, 13), **26.0 mm apart** |
| Parts | 31 + 2 mounting holes; 8 on the front, 23 on the back |
| Identity | PCB title block **rev v0.2, 2026-08-07** · F.Silk `SENSE v0.2` · same rev in `sensor.kicad_sch` |
| Assembly | double-sided SMT reflow (PCBA), **plus a third operation for J1** — the one TH part |

**Four layers, not two.** The original placement pass argued for two, on the
grounds that the only fast signal is 400 kHz I²C and that a split ground would
work against the board's one thermal requirement. The hand pass went to four
anyway, and it was the right call: In1 is a **single 339 mm² unbroken plane**,
which is the reference the BNO085's layout guidance asks for and which two
layers could not give it once the routing landed. The thermal argument was
never about layer count — the gas sensor is decoupled by a **rule area that
excludes the pour on all four layers**, so the extra planes cost it nothing.
1.6 mm rather than 0.8/1.0 for stiffness: the IMU has to read the enclosure's
gravity vector, not board flex.

## The six requirements → what was built

| # | Requirement | Result |
|---|---|---|
| 1 | Part clearance | **Every pad-to-pad copper gap ≥ 0.60 mm**, against a 0.22 mm design rule — that is the number that decides bridging and rework room. Six *courtyard* pairs sit under the 0.22 mm courtyard floor; all six are named, measured and accepted in `pcb_check.py`'s `COURTYARD_TIGHT` (a KiCad 0603 courtyard is the pad box + 0.28 mm, so two 0603s 0.65 mm apart in copper report a 0.09 mm "gap") |
| 2 | 2 mounting holes | H1 (3.0, 3.0) and H2 (27.0, 13.0), M2 Ø2.2 **non-plated**, 26.0 mm apart. Hole wall **1.90 mm** from the board edge; the Ø4.95 mm screw-head/washer keepout stays **0.53 / 0.52 mm inside** the outline, and no part sits in either |
| 3 | Light + environment sensors on the front | **F.Cu = U3 (TSL2591) + U2 (BME688) and nothing but their own local passives** (C10/C11/R10/R11 for U2, C12/R12 for U3) — 8 parts |
| 4 | J1 on the back | J1 (JST ZH B6B-ZR, vertical) on B.Cu at the right end; cable exits straight off the back into the cube |
| 5 | No interference between the sensors | see *Sensing* below — and **nothing on the back sits behind U2** (re-verified, it is what the v0.2 C4/C7 nudge restored) |
| 6 | Small | 30 × 16 mm. The back is the constrained side (23 parts including the 11.6 × 4.6 mm connector courtyard); the front stays sparse so the two apertures are clean |

## Sensing — why each sensor sits where it sits

**U2 · BME688 (gas/VOC + RH + T) — left, front, in the vent.**

- **8.5 mm from U1**, the only self-heating part on the board — and in the
  tap-only configuration the firmware actually uses, U1 draws **0.28 mA total**
  (Fig. 6-17), i.e. under 1 mW. The real thermal threat to this part is the
  enclosure (amp, LEDs, battery), not its board neighbours.
- **22.8 mm from J1** — as far as the board allows from the **PVC harness**.
  Plasticiser outgassing off PVC insulation is a genuine VOC-baseline
  offender, and it is the one contaminant this design actually invites (a
  152 mm cable ending 2 cm from a gas sensor).
- **Copper-pour keepout** (`U2_thermal_island`, 1.5, 7.5 → 7.5, 12.5 mm) on
  **all four layers**, per the Bosch BME680/688 design handbook's "reduce the
  copper area around the sensor": verified **0 % pour fill inside it on every
  layer**, so board heat has to cross FR4 rather than 35 µm of copper to reach
  it. Tracks are still allowed through — U2's five signals have to get in —
  and they are budgeted: **18.2 mm of track = 4.6 mm² in a 30 mm² island
  (15 %)**, of which 11.5 mm is U2's own signals and supply. The v0.2 pass cut
  the GND share from 8.8 mm to 6.0 mm by replacing a daisy-chain that ran down
  the inside of the island with the shortest exits that reach the pour.
- **Nothing on the back behind it** (re-checked by `pcb_check.py`).
- Its own two 100 nF (C10/C11) sit 2.7 / 2.4 mm away on the supply-pin edge,
  and the address strap R10/R11 on the SDO edge — nothing crosses the vent.

**U3 · TSL2591 (ambient light) — top edge, front, 9.5 mm from U2.**

- The **Ø4.0 mm window** on `User.Drawings` is centred exactly on the part,
  and **nothing but U3's own pads is inside it — on copper *or* silk.** The
  stock `OptoDevice:AMS_TSL25911FN` prints its package outline 1.31 mm from
  the photodiode; white silk that close, under the window, is a diffuse
  reflector. U3 therefore uses a project-local fork,
  **`sensor:AMS_TSL25911FN_ALSWindow`** (identical pads / courtyard / F.Fab,
  no F.Silk outline, pin-1 marker moved outboard) — forked rather than
  hand-edited on the board so *Update Footprints from Library* cannot put the
  silk back.
- The only same-side neighbours (C12, R12) are 0.5 mm-tall 0603s — nothing
  shadows the window.
- Far from J1's through-hole pads, which poke through to the front side.
- Separate from the vent, so the enclosure gets **one clear window and one
  mesh/vent**, not one shared hole.
- C12 (1 µF low-ESR) is **2.59 mm** from VDD — the one hard placement rule the
  TSL2591 datasheet gives.

**U1 · BNO085 (9-DOF) — centre, back, with the connector.**

- **On the line between the two M2 screws — 0.038 mm off it**, over a 26.0 mm
  span, where the board is stiffest. An IMU on a flexing corner reads its own
  board.
- **The 32.768 kHz loop is the best-referenced net on the board.**
  Y1.2→U1.27 **2.20 mm**, Y1.1→U1.26 **2.61 mm**, Y1→C7 2.10, Y1→C8 2.20, no
  crossover, both nets entirely on B.Cu with **no vias**, and In2 is 100 %
  filled under the crystal. 12.5 pF CL, ±20 ppm, high-impedance — it earns the
  prime real estate.
- Supply decoupling C3/C4 (100 nF) at **1.72 / 2.13 mm** from VDD/VDDIO, C5
  (1 µF) local bulk, all on the opposite side from the digital pins; C6 on CAP
  at 2.41 mm.
- Host side (SCL/SDA/H_INTN/SA0/CSN/ENV) faces J1; the 10 k/100 nF power-on
  reset sits above, with the two DFU test pads (TP1 `NRST`, TP2 `BOOTN`)
  reachable with a probe while the board is out of the case.
- **Magnetometer note: use nylon or brass M2 hardware.** A steel screw 8 mm
  from a magnetometer is a permanent hard-iron offset.

**Why the mounting holes are non-plated.** The main board is bolted to the
aluminium front plate; if this board's ground plane were bolted to the same
metal, the harness's single GND wire plus the chassis would form a loop around
the whole clock, and every return current in it would cross this board's plane
under the I²C and the ALS. Isolated holes keep the sensor board's ground
single-point — it comes in on J1 pin 1 and leaves nowhere.

## Floor plan (world mm, both drawn as seen from the FRONT)

```
FRONT (F.Cu) — the sensing face, 8 parts                    BACK (B.Cu) — 23 parts
 x 0                  15                 30                 x 0                  15                 30
  +----------------------------------------+                 +----------------------------------------+
  | (H1)   R12   [U3]    C12    SENSE v0.2 | y 1..4           |  TP2  TP1  C6   C9   R8  R6    C2   C1 | y 1..3
  |         ^ALS window^                    |                 |   R7 |                              |  |
  |   ENVIR       ALS                       | y 6             |   C3 |    [   U1   ]   |R5|  [==J1==]| | y 5..9
  | [ U2 ]      R10                         | y 9..10         |   C4 |    BNO085       |  |  1GND..6 | |
  |  ^vent^          R11                    | y 10..13        |   C7    Y1(32k)   C8               |  | y 10.7
  |  C11  C10                               | y 13.5          |   C5    R3   R1    R2   TO J7  (H2) |  | y 13..14
  |         I sense like a human            | y 15            |              R4   R9                |  | y 14.5
  +----------------------------------------+                 +----------------------------------------+
```

H1 (3.0, 3.0) and H2 (27.0, 13.0) are through the board and appear on both.
`User.Drawings` carries the two enclosure apertures: **Ø5.2 vent** centred
exactly on U2 at (4.5, 10.0) and **Ø4.0 window** centred exactly on U3 at
(11.5, 3.6).

## Routing (as built)

- **U1's LGA-28 is a single perimeter ring** — every pad escapes *outward*, so
  nothing has to squeeze between two 0.5 mm-pitch pads.
- **In1.Cu is the reference plane**: one unbroken 339 mm² island. In2 was
  partly spent as a routing layer (SDA takes 53.8 mm against a 28.6 mm minimum
  spanning tree, SCL 40.9 against 27.3), so the pour under U1 is 72 % on In1
  and 38 % on In2. Nothing on this board misbehaves because of it — the
  fastest edge is a 400 kHz I²C rise and **every island on every layer is
  stitched** (checked: no orphan copper anywhere) — but see *Accepted
  deviations*.
- `GND` is the pour on all four layers with **solid (not thermal-relief) pad
  connections**: this board is reflowed, never hand-soldered, so relief spokes
  would only add impedance, and several GND pads sit in channels the pour can
  reach from one side only.
- **Electrical margin is enormous.** 0.25 mm × 35 µm ≈ 0.9 A of capacity
  against a ~35 mA peak load; all 109 mm of `+3V3` copper in series would be
  216 mΩ, so the drop at the BME688's 18 mA heater peak is **under 3.9 mV**
  whatever the path. A power net class would be pointless here.

## Verification (2026-08-07, all reproducible)

- `kicad-cli pcb drc --severity-all` → **0 violations, 0 unconnected items, 0
  schematic-parity issues.**
- `gen/pcb_check.py` (independent — loads the saved file, re-exports the
  netlist, re-derives the outline from Edge.Cuts) → **33/33**, including all
  100 schematic pins matched to a pad carrying the right net, no netless
  copper pad, all 17 filled zone islands stitched, no F.Silk in the optical
  window, and PCB title block == schematic title block == silk revision.
- `kicad-cli sch erc` → **0 violations**, with *"Global label only appears
  once"* re-enabled (it had been suppressed).
- `gen/pcb_fill.py` reproduces the GUI's fill and now restores
  `sensor.kicad_pro` afterwards — `board.Save()` rewrites that file from
  pcbnew's model and would otherwise silently drop the ERC configuration.

## Accepted deviations (measured, not waived)

| | |
|---|---|
| **J1 is `B6B-ZR`, the 2.7 mm tail** | JST specifies 2.7 mm for 0.6–1.2 mm boards and **3.4 mm for 1.6 mm**, which is this board. `B6B-ZR-3.4(LF)(SN)` is Active but **not stocked** (DigiKey 455-B6B-ZR-3.4-ND: made to order, MOQ 2,000, 16-week lead) against 9,478 pcs of the 2.7 mm part at $0.24. A 2.7 mm post through 1.6 mm still leaves **1.1 mm protruding** — a normal, fully-wetted TH joint — and the wafer seats either way. Same call on the main board's J7/J10/J11. |
| **C4 and C7 poke 0.36 / 0.44 mm into the Ø5.2 vent circle** (on the back) | v0.2 moved both **+0.175 mm east**, which is all U1's courtyard (0.245 mm left) and Y1's (0.595 mm) allow — a 1 mm move as first suggested is geometrically impossible. That restored *"nothing on the back behind U2"* and bought 0.08 / 0.17 mm of vent margin. The residue means the enclosure vent wants to be an **open aperture in front of the board**, not a through-feature with a gasket pressed flat on the back face. |
| **In2.Cu is spent partly on routing** | SDA/SCL take an inner-layer detour (1.88× / 1.50× their minimum spanning tree), so In2 is 38 % filled under U1 rather than solid. In1 carries the continuous plane 0.48 mm further out. If the board is ever re-laid, pull SDA/SCL back onto F.Cu/B.Cu and take In2 back as a plane. |
| **One TH part on an all-SMD board** | J1 forces a third operation after both reflows. `B6B-ZR-SM4-TF` (same ZHR-6 housing, SMT with brass reinforcement) would remove it, at the cost of pull-out strength. Price the third operation before committing — it is usually quoted per board, not per joint. |
| **6-way connector — final** | A 1×07 would have made the J7/J10 mis-mate physically impossible *and* given `NRST` a host line (`FIRMWARE.md` R-BOARD-3). **Proposed and rejected 2026-08-08**: it costs a footprint swap on two boards plus re-routing J7's fan-out on an already-routed main board, against three problems that are all procedural or already accepted. J1 stays `B6B-ZR` (ZH 1×06) on the `A06ZR` harness. The guard is the warning block on the schematic sheet plus labelled cables; R-BOARD-3 is permanently accepted. See `REVIEW.md`. |

## Fixed while laying this out

- **`sensor.pretty/CEVA_BNO085…` 3D model**: the SnapEDA STEP is authored
  Y-up, so with `(rotate 0 0 0)` the body lay in the board plane and stuck
  1.9 mm **through** the PCB. Now `(rotate -90 0 0)` — it stands on the land,
  1.18 mm tall, which is what the enclosure work will measure against.
- **A re-route sealed off a 0.3 mm² pocket of B.Cu pour** and a GND stub
  dead-ended in it. DRC caught it only as one *unconnected item*, and only
  after a refill. `pcb_check.py` now tests every filled island for a pad or
  via of its own net.

## 3D models (2026-08-02)

All 29 real parts resolve a model file (the four without one are the two M2
mounting holes and the two test pads — nothing to draw), and
`kicad-cli pcb export step` loads every one: the assembly measures
**Z −4.585 … +2.615**, i.e. J1's wafer stands 4.5 mm off the back and its pins
clear the front by 0.93 mm.

Two footprints pointed at models that are **not in the KiCad 10 install**, so
they rendered as bare pads:

| Ref | Stock model referenced | Now |
|---|---|---|
| U3 TSL2591 | `OptoDevice.3dshapes/DFN-6_2x2.4mm_P0.65mm.step` (absent) | ams `SON-06-FN` package STEP as `3d/AMS_TSL25911FN.step`, offset/rotate 0 — it is authored on the seating plane with the pin-1 dot over the −x/+y pad, which is this footprint's pad 1. Since v0.2 the fork `sensor:AMS_TSL25911FN_ALSWindow` carries that path directly |
| J1 JST ZH | `Connector_JST.3dshapes/JST_ZH_B6B-ZR_…step` (the library has no ZH models at all) | `3d/JST_ZH_B6B-ZR_1x06_P1.50mm_Vertical.step`, generated by `../kicad/gen/mk3d.py` from `datasheet/connector_jst_zh.pdf` |

Both are wired up through the shared `../kicad/gen/models3d.py` `OVERRIDES`
table. To patch them into an already-laid-out board without regenerating it:

```
python3 ../kicad/gen/models3d.py sensor.kicad_pcb
```

## Next steps (not done here)

1. **Order it assembled** — all three sensors are leadless. Confirm 0.22 mm
   clearance / 0.3 mm drill with the fab (well inside every standard tier),
   and put the **BME688 handling rules** in the fab package:
   `../datasheet/sensor_env_bme68x_hsmi.pdf` — MSL 1, 260 °C peak for 20–40 s,
   ≤ 3 reflows, **≥ 50 µm solder height after reflow** (a stencil-thickness
   decision), vent covered for any board wash, **no siloxanes anywhere near
   it**.
2. **Enclosure**: cut the two apertures against `User.Drawings` (Ø5.2 vent over
   U2, Ø4.0 window over U3) and the silk `ENVIR` / `ALS` markers; keep the
   vent away from the amp/LED/battery heat and the ALS off-axis from the dial
   LEDs; **nylon or brass M2 screws only**.
3. **Firmware**: the BNO085's axes are fixed by the package. With the board
   read from the front (silk upright), U1's pin-1 corner is bottom-right on
   the back side; check the axis mapping against the datasheet's Fig. 6-6
   frame once the mounting orientation in the cube is decided. Also
   **R-BOARD-4**: enable the MCP23017's pull-up on GPB3 (`GPPU.3 = 1`).
4. **Build the harnesses and label them.** J7 and J10 are the same ZH 1×06
   header 13.5 mm apart and J10 carries +5 V on pin 2, so the labels *are* the
   keying — the 1×07 that would have made the mis-mate impossible was
   considered and **rejected** (`REVIEW.md`). The main board already silkscreens
   `SENSOR` and `KNOB` beside the two headers; match the cable labels to those.
