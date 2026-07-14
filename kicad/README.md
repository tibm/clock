# Wooden Smart Clock — KiCad schematic

KiCad 10 hierarchical schematic generated from the project spec (`../README.md`,
`../power.md`, `../power_values.md`, `../esp32.md`, `../led.md`,
`../block_diagram.drawio`). Open **`clock.kicad_pro`** in KiCad 10.

## Sheets (root = `clock.kicad_sch`)
| # | File | Contents |
|---|------|----------|
| 2 | `power_in.kicad_sch` | USB-C + CH224K PD sink (15 V) → VBUS; ERC power-flags |
| 3 | `charger.kicad_sch` | LT3652 buck charger (BAT node = VBAT) + AP9101C/AOSD32334C protector, reverse P-FET, TCO, NTC, Vbat divider |
| 4 | `rails.kicad_sch` | TPS61023 → +5 V, TLV62569 → +3V3, TPS55340 → +12 V (plugged-only) |
| 5 | `mcu.kicad_sch` | ESP32-S3-WROOM-1-N16R8 + straps + XTAL32K + prog header |
| 6 | `audio.kicad_sch` | TAS5760M PBTL amp + LTC4412 PVDD mux + LC filter + speaker |
| 7 | `motor.kicad_sch` | 2× TB6612FNG + X40.879 stepper connector |
| 8 | `display_sd.kicad_sch` | LS032B7DD02 FPC + microSD (shared SPI2) |
| 9 | `io.kicad_sch` | MCP23017 + sensors (STEMMA-QT) + QRE1113 homing + encoder + buttons + 3× AO3400A LED drivers |

**Connectivity**: on-page nets are drawn as **wires** where parts are clustered
(chargers, converters, discrete circuits); dense-IC pins and spread/cross-sheet
nets use **labels** (local within a sheet, global across sheets) — power rails
are power-port symbols. ~169 components; nets verified via `kicad-cli sch export
netlist` (no unintended merges) and `kicad-cli sch erc`.

## Custom symbols (`gen/clock_custom.kicad_sym`)
Parts with no stock KiCad symbol: `TPS61023`, `TPS55340`, `AOSD32334C`,
`TAS5760M`, plus a `VBAT` power flag. Registered via `sym-lib-table`.

## Known ERC items (2 — both benign, documented)
- **pin_to_pin (U2 BAT ↔ PWR_FLAG)** — the VBAT rail flag (power-output) shares
  the net with the LT3652 BAT pin (typed *output*); KiCad flags output↔power-
  output. The connection is correct (BAT node = VBAT).
- **pin_to_pin (U14 QRE emitter ↔ PWR_FLAG)** — QRE1113 phototransistor emitter
  (open-emitter) tied to a GND that carries a PWR_FLAG. Pin-matrix false
  positive; emitter→GND is correct.

## ⚠ Flags to reconcile before fab
1. **TAS5760M = DAP 32-pin** (`TAS5760MDAPR`, `HTSSOP-32-1EP`), per datasheet p.6
   pin config — matches the BOM. (Resolved.)
2. **Footprints TBD** (no stock KiCad land pattern): `QRE1113` (U14) and the
   one-shot **TCO** (F1, SDF-DF077S) have empty footprints — add custom land
   patterns.
3. **Verify on bench**: TAS5760M bootstrap-cap value (placeholder 33 nF),
   TPS55340 comp network, AP9101C CO/DO ↔ charge/discharge FET-gate assignment,
   FH34 display FPC footprint (currently a generic 10-pin 0.5 mm FPC placeholder).

## Regenerating
The schematic is emitted by a Python generator (reproducible, diff-friendly):
```
cd gen && python3 mksym.py && python3 build.py
```
`sch.py` (doc builder) · `schlib.py` (symbol loader + pin geometry) ·
`sexp.py` (S-expr I/O) · `mksym.py` (custom symbols) · `s_*.py` (one per sheet) ·
`build.py` (orchestrator). Validate: `kicad-cli sch erc clock.kicad_sch`.
