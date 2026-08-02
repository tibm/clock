# Wooden Smart Clock — sensor board (KiCad)

Small SMT daughterboard carrying the three off-board sensors, plugged into
**main-board J7** on a 6-way JST ZH harness. Open `sensor.kicad_pro`; both the
sheet and the board are **generated** by the Python in `gen/` (see
*Regenerating*) — edit the block files, not the `.kicad_sch`/`.kicad_pcb`.

**PCB: 30 × 16 mm, 2 layers, placed 2026-07-30 — not routed.** Front face =
the two sensors that must see the outside world (TSL2591 window + BME688
vent); back face = the harness connector and the IMU. Full rationale,
floor plan and verification in [`PCB_NOTES.md`](PCB_NOTES.md).

This is "option **2b**" of [`../datasheet/README.md`](../datasheet/README.md) §15:
the same sensor set as the STEMMA-QT bring-up chain, on one board, at the same
I²C addresses, so firmware written against the breakouts runs unchanged.

| Ref | Part | Package | I²C | Datasheet |
|---|---|---|---|---|
| U1 | **BNO085** (CEVA/Hillcrest 9-axis fusion IMU, SH-2 on-chip) | LGA-28 5.2×3.8 *(land in `sensor.pretty`)* | **0x4A** (R3) / 0x4B (R4) | `../datasheet/sensor_imu_bno085.pdf` |
| U2 | **BME688** (gas/VOC + RH + pressure + temp) | LGA-8 3×3 | **0x77** (R10) / 0x76 (R11) | `../datasheet/sensor_env_bme688.pdf` |
| U3 | **TSL2591** (`TSL25911FN`, 188 µlx–88 klx ALS) | DFN-6 2×2 | **0x29** (fixed) | `../datasheet/sensor_light_tsl2591.pdf` |
| Y1 | ABS07-32.768KHZ-T (BNO085 clock) | 3.2×1.5 SMD | — | `../datasheet/xtal_32k_abs07.pdf` |
| J1 | JST ZH **B6B-ZR** 1×06 | TH vertical | — | `../datasheet/connector_jst_zh.pdf` |

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
| 6 | `ALS_INT` | **no-connect today** (spare wire) | **U3.2 `INT`** (open drain, R12 10 k PU here) |

⚠ **The harness must be straight-through** (pin *n* ↔ pin *n*). A reversed
pre-crimped ZH cable would put +3V3 on GND — check the cable before first
power-up.

Pin 6 is deliberately populated even though the main board leaves J7.6
unconnected: the wire and the pull-up already exist, so picking the ALS
interrupt up later costs one main-board net, not a new harness.

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
- **32.768 kHz crystal** (Y1 + C7/C8 22 pF) with `CLKSEL0` strapped low via a
  0 Ω. The performance table (Fig. 6-14) is specified *only* for an external
  clock or crystal, so the internal RC is not used. Same ABS07 part as the
  main board's RTC crystal (12.5 pF CL, ±20 ppm; CEVA asks 50 ppm/12.5 pF).
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
- `SDO` must not float (§6.2). R10 fitted → 0x77, matching the Adafruit 5046
  breakout used for bring-up; R11 (DNP) → 0x76, Bosch's own default.
- 100 nF on each supply (C10/C11).

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

## Custom footprint (`sensor.pretty`, fp-lib-table nickname `sensor`)

| Footprint | Part | Source |
|---|---|---|
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
   (`Bosch_LGA-8_3x3mm_P0.8mm_ClockwisePinNumbering`) and TSL2591
   (`OptoDevice:AMS_TSL25911FN`) stock footprints are part-specific KiCad libs
   generated from those datasheets, so they need no separate check.
2. **Harness polarity** — 1:1, not reversed (above).
3. ~~Placement~~ — **closed 2026-07-30**, see [`PCB_NOTES.md`](PCB_NOTES.md):
   U2 has the far corner, a pour keepout, a clear back side and a Ø5.2 mm
   vent marked on `User.Drawings`; U3 has a Ø4.0 mm window with 0.64 mm of
   clear courtyard and only 0.5 mm-tall neighbours; U1 sits on the line
   between the two M2 screws. Still on the enclosure side: keep the vent away
   from the amp/LED/battery heat and the ALS off-axis from the dial LEDs, do
   not conformal-coat U2, and **use nylon or brass M2 screws** — steel next
   to the magnetometer is a permanent hard-iron offset.
4. **Route it** — the board carries placement + pours + nets, no traces.

## Validation

- `kicad-cli sch erc sensor.kicad_sch` → **0 violations**.
- PCB: `gen/pcb_build.py` QA (0 courtyard errors at the 0.22 mm floor, 0 edge
  violations, 0 missed pad→net lookups, 14 pin-transform assertions) +
  `gen/pcb_check.py` re-checking the *saved* file independently (16/16) +
  `kicad-cli pcb drc` (0 errors; 3 cosmetic silk warnings, 62 unconnected =
  the unrouted ratsnest). Details in [`PCB_NOTES.md`](PCB_NOTES.md).
  ⚠ **Stale since the manual routing pass** (board is now 4 layers / 4 pours,
  parts moved): on the current file 4 of those 16 fail — C1↔J1 courtyards
  touch (0.000 mm), H1/H2 are 0.02 mm inside the 0.35 mm edge rule, C4/C7 now
  sit on the back behind U2, and the "2 GND pours" assumption. The checks
  encode the *generated* placement, so decide per item whether the check or
  the board should move.
- BOM (2026-08-01): `gen/pcb_check.py` BOM block **5/5** — all 29 BOM parts
  carry MPN/Manufacturer/Package/Description, board fields identical to the
  schematic's, and the DNP / off-BOM sets agree between the two files.
  `gen/stamp_bom.py` re-run is a no-op (idempotent), ERC still 0 violations,
  and both files still round-trip byte-identically through KiCad's own
  writers.
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
missing from the db. **Re-run it after every `build.py` / `pcb_build.py`
rebuild** — both generators write their file from scratch and drop the fields.

- **DNP** (`parts_db.DNP`) → `(dnp yes)` on the symbol + the `dnp` footprint
  attribute. **R4 and R11 are the alternate I²C-address straps**: only their
  *value strings* said "(DNP)", so a BOM taken before 2026-08-01 told the
  assembler to fit them — and R3+R4 or R10+R11 fitted together ties +3V3 to
  GND through 0 Ω. They now export with KiCad's `DNP` column set.
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
TSL25911FNTR-ND $1.74; all Active).

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
| `sensor.pretty/` · `3d/` | the one custom land pattern + its STEP (hand-maintained, not generated) |
| `gen/b_host.py` | J1, rail entry, bus pull-ups, PWR_FLAGs |
| `gen/b_imu.py` | BNO085 + crystal + config straps |
| `gen/b_env.py` | BME688 |
| `gen/b_als.py` | TSL2591 |
| `gen/pcb_build.py` | **the PCB**: placement, outline, holes, pours, nets, silk + QA |
| `gen/pcb_check.py` | independent re-check of the saved `.kicad_pcb` (geometry, nets, BOM) |
| `gen/pcb_fill.py` | fills the GND pours (own process — the filler segfaults in the builder's) |
| `gen/pcb_canon.py` | re-saves the `.kicad_pcb` through pcbnew's writer (own process) |
| `gen/parts_db.py` · `gen/stamp_bom.py` | BOM part data + the stamper (above) — **re-run after every rebuild** |

The PCB scripts run under **KiCad's bundled python3.9** (they need `pcbnew`),
not system python:

```
cd gen
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_build.py
python3 stamp_bom.py     # ALWAYS: a rebuild drops every BOM field and flag
/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_check.py
```

There is no `cosmetics.py`/`harvest.py` here — hand-tuned positions in
eeschema would be lost on the next build, so make layout changes in the block
files.
