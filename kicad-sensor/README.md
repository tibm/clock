# Wooden Smart Clock — sensor board (KiCad)

Small SMT daughterboard carrying the three off-board sensors, plugged into
**main-board J7** on a 6-way JST ZH harness. Open `sensor.kicad_pro`.

**The schematic is generated** by the Python in `gen/` (see *Regenerating*) —
edit the block files, not the `.kicad_sch`. **The PCB is not**: it has been
hand-placed and hand-routed since 2026-08-02, so `gen/pcb_build.py` refuses to
run and board changes are made in pcbnew + *Update PCB from Schematic* (F8).

**PCB: 30 × 16 mm, 4 layers, 1.6 mm — fully routed, `SENSE v0.2`
(2026-08-07).** Front face = the two sensors that must see the outside world
(TSL2591 window + BME688 vent); back face = the harness connector and the IMU.
Full rationale, floor plan, verification and the list of accepted deviations
in [`PCB_NOTES.md`](PCB_NOTES.md); the design review that drove v0.2 is in
[`REVIEW.md`](REVIEW.md).

This is "option **2b**" of [`../datasheet/README.md`](../datasheet/README.md) §15:
the same sensor set as the STEMMA-QT bring-up chain, on one board, at the same
I²C addresses, so firmware written against the breakouts runs unchanged.

| Ref | Part | Package | I²C | Datasheet |
|---|---|---|---|---|
| U1 | **BNO085** (CEVA/Hillcrest 9-axis fusion IMU, SH-2 on-chip) | LGA-28 5.2×3.8 *(land in `sensor.pretty`)* | **0x4A** (R3) / 0x4B (R4) | `../datasheet/sensor_imu_bno085.pdf` |
| U2 | **BME688** (gas/VOC + RH + pressure + temp) | LGA-8 3×3 | **0x77** (R10) / 0x76 (R11) | `../datasheet/sensor_env_bme688.pdf` + `…_bme68x_hsmi.pdf` |
| U3 | **TSL2591** (`TSL25911FN`, 188 µlx–88 klx ALS) | DFN-6 2×2 *(land in `sensor.pretty`)* | **0x29** (fixed) | `../datasheet/sensor_light_tsl2591.pdf` |
| Y1 | ABS07-32.768KHZ-T (BNO085 clock) | 3.2×1.5 SMD | — | `../datasheet/xtal_32k_abs07.pdf` |
| J1 | JST ZH **B6B-ZR** 1×06 | TH vertical | — | `../datasheet/connector_jst_zh.pdf` |

**Address straps are 10 kΩ, not 0 Ω** (v0.2). Exactly one of each pair is
fitted; the alternate carries a real `DNP` flag. Fitting both halves is the
assembly slip this layout invites, and through 0 Ω it would short the **main
board's** +3V3 rail to GND — on a daughterboard with no LED and no silk hint,
which is hours of debugging. Through 10 k the same slip costs 330 µA, the
clock still boots, and the part answers at a wrong-but-visible address.

No clash with the main board's own I²C devices (MCP23017 0x20, TAS5760M
0x62/0x63).

## J1 pinout — mirrors main-board J7 **1:1**

| Pin | Net | Main board (J7) | Sensor board |
|---|---|---|---|
| 1 | `GND` | GND | GND |
| 2 | `+3V3` | +3V3 | rails in — no regulator on this board |
| 3 | `I2C_SDA` | shared bus (4.7 k PU = R95) | U1.20 · U2.3 · U3.6 (+ R1 10 k) |
| 4 | `I2C_SCL` | shared bus (4.7 k PU = R96) | U1.19 · U2.4 · U3.1 (+ R2 10 k) |
| 5 | `SENSOR_INT` | MCU **IO42** IRQ, 10 k PU = R97 | **U1.14 `H_INTN` only** (push-pull, active low) |
| 6 | `ALS_INT` | **MCP23017 GPB3** (U13.4) | **U3.2 `INT`** (open drain, R12 10 k PU here — the only one on the net) |

Pin 6 was a spare wire when this board was designed; `kicad/REVIEW.md` #11
landed it on expander **GPB3**, so the ALS interrupt is live. Because the
pull-up lives only here, firmware must also set **`GPPU.3 = 1`** so GPB3 does
not float when this board is unplugged — `FIRMWARE.md` **R-BOARD-4**.

### ⚠ Harness polarity is a destructive fault

Both ends are ZHR-6 on the same pre-crimped cable and J1 mirrors J7 1:1, so a
cable loaded **backwards plugs in perfectly** — and J1.5 then receives J7.2
**+3V3 while this board's VDDIO is 0 V**. BNO08X abs max (Fig. 6-1) is
*"VDDIO + 0.3 V at any logic pin"*, series-limited only by the main board's
R97 10 k: **assume U1 ($13.57, the most expensive part on either board) is
destroyed.**

Worse, the main board's **J10** (knob) is the *same* ZH 1×06 header **13.5 mm
from J7**, with **+5 V on pin 2** — that mis-mate puts 5 V on `+3V3` and takes
out all three sensors at once (abs max: BNO085 VDDIO 3.63 V, TSL2591 3.8 V,
BME688 4.25 V).

Neither is keyed out today. **Label both harnesses and check the cable before
first power-up.** The permanent fix is a **1×07** connector on the next spin
(a 6-way ZHR housing cannot enter a 7-way header, and the spare pin gives
`NRST` a host line) — see [`REVIEW.md`](REVIEW.md). The warning is also
printed on the schematic sheet, next to J1.

## Design notes (all traceable to a datasheet line)

**BNO085 (`gen/b_imu.py`)** — follows the CEVA I²C reference design
(Fig. 1-11 and its notes 1-8):
- `PS1 = PS0 = 0` → I²C; `BOOTN` 10 k pull-up (R7) with test pad TP2 — pull it
  low at reset for the DFU bootloader.
- `CAP` (pin 9) → 100 nF to GND; VDD and VDDIO each get their own 100 nF
  (C3/C4) plus a 1 µF local bulk (C5) the reference omits — this board hangs
  off a long harness.
- **VDD and VDDIO share the 3V3 rail.** §6.3 demands VDD reach level *before
  or with* VDDIO; one rail satisfies that by construction.
- **32.768 kHz crystal** (Y1 + C7/C8 22 pF) with `CLKSEL0` **tied straight to
  GND — a bare wire, no strap resistor** (Fig. 1-8: *"0 or unconnected"*
  selects the crystal). The performance table (Fig. 6-14) is specified *only*
  for an external clock or crystal, so the internal RC is not used. Same ABS07
  part as the main board's RTC crystal (12.5 pF CL, ±20 ppm; CEVA asks
  50 ppm/12.5 pF).
- **`ENV_SCL`/`ENV_SDA` (pins 15/16) are pulled up although unused** — note 8:
  the SH-2 firmware probes that secondary bus at every reset. They are *not*
  tied to the host bus; on that bus the BNO085 is the master.
- **`NRST`**: J7 has no spare host line, so R6/C9 form a 10 k/100 nF power-on
  reset (TP1 to drive it by hand). It also guarantees the reset-sampled straps
  (BOOTN, PS1, PS0, CLKSEL0) are settled before release.
- **Deviation:** pin 18 `H_CSN` is unused in I²C mode and Fig. 1-11 leaves it
  open; R5 pulls it to VDDIO instead, so no CMOS input floats on a battery
  product. Harmless in every mode — `H_CSN` is not sampled at reset.

**BME688 (`gen/b_env.py`)** — §7.2 connection diagram (a):
- **`CSB` hard-tied to VDDIO.** §6.1: pull CSB low even once and the part
  latches into SPI until the next power-on reset and never answers I²C again.
- `SDO` must not float (§6.2). R10 (**10 k**) fitted → 0x77, matching the
  Adafruit 5046 breakout used for bring-up — which also uses 10 k; R11 (DNP)
  → 0x76, Bosch's own default. `SDO` is address-select only in I²C mode
  (Table 26) and is never driven, so a resistive strap is unambiguous.
- 100 nF on each supply (C10/C11).
- **Handling is the risk on this part, not wiring** — see *Assembly* below.

**TSL2591 (`gen/b_als.py`)** — "Application Information":
- **1 µF low-ESR at VDD** (C12), placed as close as possible to the pin.
- `INT` is open-drain → R12 10 k. Pin 4 is "NC — do not connect".

**Board level (`gen/b_host.py`)** — C1 10 µF + C2 100 nF at the connector
absorb the BME688 heater bursts (~18 mA peak) and the BNO085 turn-on so they
do not ride back up the single power wire. R1/R2 10 k parallel the main
board's 4.7 k to ≈3.2 k, inside the 2–4 kΩ window CEVA asks for (note 7) and
fast enough for 400 kHz over the harness; depopulate both to run on the 4.7 k
alone at 100 kHz. Two PWR_FLAGs declare the rails to ERC — nothing on this
board drives them.

## ⚠ Firmware constraint — do not poll the BNO085

Poll a BNO085 that has no data ready and it **stretches SCL until it does**
(§1.2.2.1). On this shared bus that stalls the BME688, the TSL2591 *and* the
main board's MCP23017 (knob, charger status, amp shutdown). Read it **only
after `SENSOR_INT` asserts**. `SENSOR_INT` carries nothing else, which is why
the TSL2591's interrupt went to the spare pin 6 instead.

Second, related, main-board item: CEVA says `H_INTN` "should be tied to a GPIO
**with wake capability**". `SENSOR_INT` lands on **IO42** (`../esp32.md`), which
is *not* an ESP32-S3 RTC GPIO (only IO0–IO21 are), so it can wake light sleep
but **not** `ext0`/`ext1` deep sleep. Fine if the clock never deep-sleeps with
tap-to-wake armed; otherwise move `SENSOR_INT` to an RTC-capable pin on the
next main-board revision. Not changed here — this board only presents the
signal on J1.5.

## Assembly

**All three sensors are leadless** (LGA-28 / LGA-8 / DFN-6) — this board
**cannot** be hand-soldered with an iron. Order it **fab-assembled**, or
reflow it yourself with a stencil + hotplate. The main board's
hand-solderable-only rule scopes the main PCB; this daughterboard is the
escape hatch that rule always assumed (`../datasheet/README.md` §15, path 2b).

**J1 is the only through-hole part**, so the board actually needs **three**
operations: reflow side 1, reflow side 2, then selective- or hand-solder J1.
Price that before committing — it is usually quoted per board, not per joint.
`B6B-ZR-SM4-TF` (same ZHR-6 housing, SMT with brass reinforcement) would
remove it at the cost of pull-out strength.

### ⚠ BME688 (U2) — put these in the fab package

A metal-oxide gas hotplate does not *fail* when mishandled; it reads a wrong
VOC baseline for ever, and a correctly-executed routine assembly process is
enough to do it. Datasheet §7.7 defers every rule to a separate **HSMI**
document, and Bosch publishes no BME688-specific one (checked 2026-08-07), so
the BME680 HSMI is filed as nearest-applicable — same LGA-8 3×3 package, same
Ø0.35 mm lid vent, same MOX element: **`../datasheet/sensor_env_bme68x_hsmi.pdf`**
(+ `…_bme68x_packaging.pdf`).

| Rule | |
|---|---|
| **MSL 1** | no bake needed |
| **Peak reflow 260 °C, 20–40 s**, **≤ 3 cycles** | this board is double-sided, so U2 already sees two |
| **≥ 50 µm solder height after reflow** | a **stencil-thickness** decision, not a layout one — it is what mechanically decouples the die from the board |
| **Cover the vent** with a silicone-free layer for any cleaning / board wash | or specify no-clean and no wash |
| **No siloxanes** — coatings, adhesives, gloves, packaging | permanent VOC poisoning |
| No conformal coat, no underfill, no ultrasonic welding, nothing sharp in the vent, no rear-side handling | |

## Custom footprints (`sensor.pretty`, fp-lib-table nickname `sensor`)

| Footprint | Part | Source |
|---|---|---|
| `AMS_TSL25911FN_ALSWindow` | U3 | **Fork of stock `OptoDevice:AMS_TSL25911FN`** (v0.2). Identical pads, courtyard, F.Fab and Cmts.User; the **F.Silk package outline is removed** and the pin-1 marker moved outboard. The stock silk crosses the enclosure's Ø4.0 mm optical window 1.31 mm from the photodiode, and white silk that close under a window is a diffuse reflector. Forked rather than hand-edited on the board so *Update Footprints from Library* cannot put it back — and so `kicad-cli pcb drc` stays at 0 (a hand-edited instance reports `lib_footprint_mismatch` for ever). Its `(model …)` points straight at `3d/AMS_TSL25911FN.step`, the override the stock footprint needed anyway. |
| `CEVA_BNO085_LGA-28_5.2x3.8mm_P0.5mm` | U1 | **Vendor (SnapEDA) land, verified against BNO08X datasheet Fig. 7-2** — measured land extents **5.2000 × 3.8000 mm**, i.e. exactly the drawing. Pad centres ±1.5625 (10-pad rows) and ±2.3125 (4-pad ends), pads 0.25 × 0.675 / 0.575 × 0.25. Cleaned on import: SnapEDA's per-pad `solder_mask_margin 0.102` **removed** (at 0.5 mm pitch it left a 0.046 mm mask web — below any fab's minimum dam, so the mask would have silently ganged; board default gives 0.25 mm), pin-1 silk dot grown from Ø0.2 to Ø0.42 (was under the 0.15 mm silk minimum), `3d/BNO085.step` linked, renamed to house style, resaved through `kicad-cli fp upgrade`. Pad numbering/arrangement cross-checked against the drawing and against KiCad's stock generic pattern — identical. |

KiCad's stock `Package_LGA:LGA-28_5.2x3.8mm_P0.5mm` is a **generic IPC** land: same
numbering and arrangement, but ~0.10 mm (row pads) / 0.125 mm (end pads) of extra toe
per side → 4.00 × 5.45 instead of 3.80 × 5.20. Not wrong, just unverified against this
part; the vendor land above is used instead so nothing needs checking before fab.

## Custom symbol (`gen/sensor_custom.kicad_sym`)

`BNO085` — KiCad has no BNO08x symbol (`Sensor_Motion:BNO055` is the same
*package* with a completely different pinout). Pins are grouped by function:
host I²C and the strapped pins on the left, clock / CAP / ENV bus on the
right, supplies top, grounds bottom, and the nine `RESV_NC` pads kept visible
so every pad is accounted for. The other two sensors use stock symbols
verbatim — `Sensor:BME680` with the value overridden to **BME688** (identical
pin table per BME688 Table 26 and identical LGA-8 3×3 P0.8 clockwise package)
and `Sensor_Optical:TSL25911FN`.

## Before fab

1. ~~LGA-28 land pattern~~ — **closed 2026-07-29**: U1 now uses the vendor land in
   `sensor.pretty`, measured against datasheet Fig. 7-2 (see above). The BME688
   (`Bosch_LGA-8_3x3mm_P0.8mm_ClockwisePinNumbering`) and TSL2591 (stock
   `OptoDevice:AMS_TSL25911FN`, forked here only to trim its silk) footprints
   are part-specific KiCad libs generated from those datasheets, so they need
   no separate check.
2. **Harness polarity** — 1:1, not reversed. Label both cables; J10 on the
   main board is the same connector 13.5 mm away (above).
3. ~~Placement~~ — **closed 2026-07-30**, revised 2026-08-07, see
   [`PCB_NOTES.md`](PCB_NOTES.md): U2 has the far corner, a 4-layer pour
   keepout, a clear back side and a Ø5.2 mm vent marked on `User.Drawings`; U3
   has a Ø4.0 mm window with nothing but its own pads inside it, on copper or
   silk; U1 sits 0.038 mm off the line between the two M2 screws. Still on the
   enclosure side: keep the vent away from the amp/LED/battery heat and the
   ALS off-axis from the dial LEDs, and **use nylon or brass M2 screws** —
   steel next to the magnetometer is a permanent hard-iron offset.
4. ~~Route it~~ — **closed 2026-08-02**: 193 segments, 30 vias, 4 GND pours.
5. **BME688 handling** — the HSMI rules above go in the fab package with the
   Gerbers, and the ≥ 50 µm solder height is a stencil decision the assembler
   has to agree to *before* the run.

## Validation (2026-08-07, v0.2)

- `kicad-cli sch erc sensor.kicad_sch` → **0 violations**, and *"Global label
  only appears once in the schematic"* is **no longer suppressed** — on a
  single-sheet design where every inter-block connection is a global label,
  that is the check that catches a typo'd label name.
- `kicad-cli pcb drc --severity-all` → **0 violations, 0 unconnected items, 0
  schematic-parity issues.**
- `gen/pcb_check.py`, which re-checks the *saved* file independently (loads the
  board, re-exports the netlist, re-derives the outline from Edge.Cuts) →
  **33/33**. It was re-baselined for v0.2: it now judges part clearance on
  **pad-to-pad copper** (≥ 0.60 mm everywhere) rather than on courtyards,
  checks mounting holes at the **hole wall** rather than at the screw-head
  keepout, and adds three checks — every filled zone island is stitched, no
  F.Silk inside the optical window, and **PCB title block == schematic title
  block == the `SENSE v0.2` silk legend**. Details in
  [`PCB_NOTES.md`](PCB_NOTES.md).
- BOM: all 29 BOM parts carry MPN/Manufacturer/Package/Description, board
  fields identical to the schematic's, and the DNP / off-BOM sets agree
  between the two files. `gen/stamp_bom.py` re-run is a no-op (idempotent) and
  both files round-trip byte-identically through KiCad's own writers.
- `gen/build.py` lint: no dangling wires, no body overlaps, no wires through
  symbols; junctions placed by eeschema's own rules so a GUI re-save is a
  no-op. The sheet is normalised through `kicad-cli sch upgrade`, so opening
  and saving in eeschema produces zero diff.
- Netlist spot-checked pin-by-pin against the three datasheets (every
  `RESV_NC` and the TSL2591 `NC` land on their own unconnected nets).

## BOM part data (`gen/parts_db.py` + `gen/stamp_bom.py`, 2026-08-01)

The generators only emit Value + Footprint, which is not a BOM: this board is
**ordered fab-assembled**, so every line has to be unambiguous to an assembly
house (`MPN` + `Package`) and the parts that must *not* be fitted have to say
so in a field, not in a value string.

```
cd gen && python3 stamp_bom.py          # --dry-run to preview
```

splices **MPN / Manufacturer / Package / Description / Notes** into *both*
`sensor.kicad_sch` (eeschema + `kicad-cli` BOMs) and `sensor.kicad_pcb` (the
PCBWay plug-in reads *footprint* fields) — in place, no regeneration, so hand
DRC fixes survive — then sets two kinds of flag and hands both files back to
KiCad's own serializers (`kicad-cli sch upgrade` + `gen/pcb_canon.py`) so they
stay byte-identical to a GUI save. Idempotent; fails loudly on a part that is
missing from the db. **Re-run it after every `build.py` rebuild** — the
schematic generator writes its file from scratch and drops the fields.

- **DNP** (`parts_db.DNP`) → `(dnp yes)` on the symbol + the `dnp` footprint
  attribute. **R4 and R11 are the alternate I²C-address straps**: only their
  *value strings* said "(DNP)", so a BOM taken before 2026-08-01 told the
  assembler to fit them — and R3+R4 or R10+R11 fitted together tied +3V3 to
  GND through 0 Ω. They now export with KiCad's `DNP` column set **and** the
  straps are **10 k** (v0.2), so the flag is the guard and the value is the
  fail-safe under it: the same slip now costs 330 µA instead of the main
  board's 3.3 V rail.
- **Not a part** (`parts_db.EXCLUDE_FROM_BOM`) → `(in_bom no)` +
  `(in_pos_files no)` on TP1/TP2, matching the `exclude_from_bom` their
  footprints already carried, so the schematic-side and PCB-side BOMs agree
  (and "Update PCB from Schematic" can't put the test pads back).

Part data: the three sensors + the crystal + J1 are local to
`gen/parts_db.py`; the **generic passives are imported from the main board's
`../../kicad/gen/parts_db.py`**, so both boards order the same physical 0603 /
0805 parts. Per-reference data never falls through to the main board's table —
J1/U1/U2/U3/Y1 exist there too and mean different parts. DigiKey status/price
re-verified 2026-08-01 (BNO085 1888-1006-1-ND $13.57 · BME688 828-BME688CT-ND
$8.99 — `../datasheet/README.md` §12's ~$5 is the reel price · TSL25911FN
TSL25911FNTR-ND $1.74; all Active) and J1 re-checked 2026-08-07
(455-B6B-ZR-ND, $0.24, 9,478 pcs).

**J1 tail length — checked and deliberately not changed.** JST specifies the
2.7 mm soldering length for 0.6–1.2 mm boards and **3.4 mm for 1.6 mm**, which
is this board. But `B6B-ZR-3.4(LF)(SN)` is Active and **not stocked**
(455-B6B-ZR-3.4-ND: made to order, MOQ 2,000, 16-week lead), against 9,478 pcs
of the 2.7 mm part at MOQ 1. A 2.7 mm post through 1.6 mm still leaves 1.1 mm
protruding — a normal, fully-wetted TH joint — and the wafer seats on the
board either way, so the deviation is accepted and recorded in `parts_db.py`
and [`PCB_NOTES.md`](PCB_NOTES.md). The same call applies to the main board's
J7/J10 (and J11, `B2B-ZR`), which are the other ends of the same harnesses.

Export (13 lines, 29 pieces; group by `Notes` too, or the two DNP straps merge
into one line and take one another's note):

```
kicad-cli sch export bom \
  --fields 'Reference,Value,Package,MPN,Manufacturer,Description,Footprint,${QUANTITY},${DNP},Notes' \
  --labels 'Refs,Value,Package,MPN,Manufacturer,Description,Footprint,Qty,DNP,Notes' \
  --group-by 'MPN,Value,Notes' --sort-field Reference \
  -o sensor_bom.csv sensor.kicad_sch
```

## Regenerating

```
cd gen && python3 mksym.py && python3 build.py
```

`gen/` reuses the main board's generator (`../../kicad/gen`: `sch2.py`,
`schlib.py`, `sexp.py`, `project2.py`) via a `sys.path` bootstrap at the top of
`mksym.py` / `build.py`, so both boards share one KiCad-format writer, one lint
pass and one junction algorithm. Local files:

| File | Role |
|---|---|
| `gen/build.py` | assembles the A3 sheet, runs lint + `kicad-cli sch upgrade` |
| `gen/mksym.py` | writes `sensor_custom.kicad_sym` (BNO085) |
| `gen/project.py` | `.kicad_pro` + lib tables (symbol + footprint nickname `sensor`) |
| `sensor.pretty/` · `3d/` | the two custom land patterns + their STEPs (hand-maintained, not generated) |
| `gen/b_host.py` | J1, rail entry, bus pull-ups, PWR_FLAGs |
| `gen/b_imu.py` | BNO085 + crystal + config straps |
| `gen/b_env.py` | BME688 |
| `gen/b_als.py` | TSL2591 |
| `gen/pcb_build.py` | ⚠ **historical** — the 2026-07-30 2-layer placement. **Refuses to run**; the board is hand-owned (see [`PCB_NOTES.md`](PCB_NOTES.md)) |
| `gen/pcb_check.py` | independent re-check of the saved `.kicad_pcb` (geometry, nets, zones, silk, identity, BOM) |
| `gen/pcb_fill.py` | fills the GND pours (own process — the filler segfaults in the builder's) |
| `gen/pcb_canon.py` | re-saves the `.kicad_pcb` through pcbnew's writer (own process) |
| `gen/parts_db.py` · `gen/stamp_bom.py` | BOM part data + the stamper (above) — **re-run after every `build.py`** |

The PCB scripts run under **KiCad's bundled python3.9** (they need `pcbnew`),
not system python:

```
cd gen
KPY=/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9
python3 stamp_bom.py     # ALWAYS after build.py: it drops every BOM field and flag
$KPY pcb_fill.py         # after anything that moves copper
$KPY pcb_check.py
```

**A schematic change now has to be carried across by hand**, because
`pcb_build.py` is retired (it would overwrite the routing):

1. edit the block file, `python3 build.py`, `python3 stamp_bom.py`;
2. open `sensor.kicad_pcb` in pcbnew and run **Update PCB from Schematic
   (F8)** — it preserves tracks, zones and hand placement;
3. re-fill, then `pcb_check.py` + `kicad-cli pcb drc`.

There is no `cosmetics.py`/`harvest.py` here — hand-tuned positions in
eeschema would be lost on the next build, so make *schematic* layout changes
in the block files.
