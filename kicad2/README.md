# Wooden Smart Clock — KiCad schematic (v2, single sheet)

KiCad 10 schematic on **one A1 page**, organised like `../block_diagram.drawio`:
power flows along the left, MCU in the centre, peripherals on the right.
Open **`clock.kicad_pro`**.

Rewrite of `../kicad/` (8 hierarchical sheets, label-stub style) with the two
usability problems fixed: everything inside a block is **wired**, and no text
overlaps components.

## Page map

| Region | Blocks |
|---|---|
| left | POWER IN (USB-C + CH224K) → CHARGER (LT3652) → BATTERY (18650 + AP9101C/AOSD32334C + TCO) |
| mid-left | RAILS 5 V→3V3 · 12 V boost (plugged-only) · LED DRIVERS |
| centre | MCU (ESP32-S3) · SENSORS + QRE1113 homing · IO EXPANDER + encoder + buttons |
| right | DISPLAY + microSD (shared SPI2) · AUDIO (TAS5760M + PVDD mux) · MOTOR (2× TB6612) |

## Wire / label conventions
- **Wired:** all block-internal circuitry; the power chain
  (VBUS → LT3652 → VBAT → both boosts); the **+12 V run** to the wake-LED
  connector and the audio PVDD mux; all MCU buses — SPI (display+SD),
  I2S (amp), 8× MCPWM (motor drivers), encoder A/B, expander INT.
- **By name:** GND / +3V3 / +5V / VBAT / PVDD power taps, the multi-drop I²C
  bus, the MCP23017 slow fan-out (`PD_PG`, `CHRG`, `FAULT`, `LCD_DISP`,
  `SPK_SD`, `STEP_STBY`, `BOOST12_EN`, …) and the 3 LED PWM lines.
- Off-board parts enter via connectors: J1 USB-C, J2 PROG, J3 speaker,
  J4 stepper, J5 display FPC, J6 microSD, J7 **sensor board** (I²C + INT),
  J8/J9 LED strings, BT1 cell holder.

## Custom symbols (`gen/clock_custom.kicad_sym`)
`ESP32S3_CLOCK` — WROOM-1 with **functionally grouped pins** (same pad
numbers as the stock symbol/footprint) so buses leave toward their blocks;
`TAS5760M` (DAP-32), `TPS55340`, `TPS61023`, `AOSD32334C`, `VBAT`/`PVDD`
flags, plus flattened clones of stock `extends` symbols (AO3400A, AO3401A,
2N7002, DM3AT, MCP23017) so ERC never reports lib_symbol_mismatch.

## Validation (all automated in `gen/`)
- `kicad-cli sch erc` → **2 known-benign items** (same two as v1):
  LT3652 BAT output ↔ VBAT PWR_FLAG, QRE1113 open-emitter ↔ GND flag.
- `gen/netdiff.py` → connectivity **identical to `../kicad/`** as a
  (ref,pin) partition, modulo the intended changes below.
- `gen/build.py` lint: no dangling wires, no body overlaps, no wires through
  symbols; junctions auto-placed at T-points.

### Intended differences vs `../kicad/`
1. **R_SENSE topology fixed**: v1 ran L1 straight to BAT and left R18
   floating between SENSE and BAT (no charge current through the shunt).
   Now per the LT3652 datasheet: SW → L1 → R18 → BAT, SENSE taps the L1/R18
   node.
2. **J7 sensor connector is 5-pin** and carries `SENSOR_INT` (was 4-pin +
   note).
3. **R22 ADC divider taps the cell side** of the reverse P-FET (per
   `power.md` "divider off the cell"); v1 tapped VBAT.
4. R71/R72 keep their v1 nets (DAT1/DAT2 pull-ups); 2-pin symmetric parts may
   have pins 1/2 swapped (electrically identical).

## ⚠ Flags to reconcile before fab (carried over from v1)
- `QRE1113` (U14) and TCO `F1` have **empty footprints** (no stock land
  pattern) — draw custom ones.
- J5 uses the **FH12**-10S footprint as a stand-in — the BOM part is
  **FH34SRJ-10S**; make/verify the FH34 land pattern.
- Bench-verify: TAS5760M bootstrap caps (33 nF placeholder), TPS55340
  compensation, AP9101C CO/DO ↔ charge/discharge FET-gate assignment.

## Regenerating
```
cd gen && python3 mksym.py && python3 build.py && python3 netdiff.py
```
`sch2.py` — manual-placement builder (explicit wires; auto-junctions; lint) ·
`b_*.py` — one file per block (all coordinates hand-chosen, 1.27 mm grid) ·
`mksym.py` — custom symbols · `netdiff.py` — connectivity diff vs `../kicad/`.
Validate: `kicad-cli sch erc clock.kicad_sch`
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).
