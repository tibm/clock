# Wooden Smart Clock — KiCad schematic (single sheet)

KiCad 10 schematic on **one A1 page**, organised like `../block_diagram.drawio`
(⚠ the drawio still shows the pre-v0.19 display-era design):
power flows along the left, MCU in the centre, peripherals on the right.
Open **`clock.kicad_pro`**. The sheet is **generated** by the Python code in
`gen/` (see *Regenerating* below) — edit the block files, not the `.kicad_sch`.

## Page map

| Region | Blocks |
|---|---|
| left | POWER IN (vertical USB-C + CH224K) → CHARGER (LT3652) → BATTERY (18650 + HY2111/AOSD32334C + TCO) |
| mid-left | RAILS 5 V→3V3 · 12 V boost (plugged-only) · LED (wake drivers) · NEOPIXELS (7× SK6812) |
| centre | MCU (ESP32-S3) · SENSORS + QRE1113GR homing · IO EXPANDER + KNOB (EM14) + J11 toggle |
| right | STORAGE (microSD, SPI2) · AUDIO (TAS5760M + PVDD mux) · MOTOR (2× TB6612) |

## Wire / label conventions
- **Wired:** all block-internal circuitry; the power chain
  (VBUS → LT3652 → VBAT → both boosts); the **+12 V run** to the wake-LED
  connector and the audio PVDD mux; all MCU buses — SPI (microSD),
  I2S (amp), 8× MCPWM (motor drivers), knob A/B, expander INT; the
  SK6812 daisy-chain.
- **By name:** GND / +3V3 / +5V / VBAT / PVDD power taps, the multi-drop I²C
  bus, the MCP23017 slow fan-out (`PD_PG`, `CHRG`, `FAULT`, `SPK_SD`,
  `STEP_STBY`, `BOOST12_EN`, …), the 2 wake PWM lines, `NEOPIX_DATA` and
  `ENC_SW`.
- Off-board parts enter via connectors: J1 USB-C (**vertical** since
  2026-07-19 — port exits the back wall, no extension), J2 PROG, J3 speaker,
  J6 microSD, J7 **sensor board** (I²C + INT), J9 wake-LED strips,
  J10 **knob** (EM14 encoder + switch, top face), J11 **radio-off toggle**
  (rear), BT1 cell holder. The **X40.879 stepper is on-board (M1)** since
  2026-07-17 — soldered to the PCB, shafts through a board hole (J4
  dropped). *(J5 display FPC + J8 panel string removed 2026-07-19 with the
  display; the 7 status/dial NeoPixels are on-board D40–D46.)*
- **J10 knob connector** (reworked 2026-07-19/20 for the EM14 optical
  encoder): JST **ZH** 1×06 **B6B-ZR** — same family as J7, so ONE cheap
  pre-crimped ZH↔ZH cable type (A06ZR06ZR28H102B-style) serves both.
  Pinout `1 GND · 2 +5V · 3 A · 4 B · 5 SW · 6 GND`. A/B are 5 V
  outputs → on-board **100k/200k dividers** (R111/R114, R112/R115) →
  IO47/48; SW → 10 k PU (R113) + 100 nF (C239) → IO17.
  **J11** = JST ZH 1×02 **B2B-ZR** → expander GPA3 (`RADIO_OFF`,
  MCP internal PU + IOC). J7 sensor = ZH 1×06 (pin 6 spare).
  J3 speaker / J9 wake strips stay JST PH (power).

## Custom symbols (`gen/clock_custom.kicad_sym`)
`ESP32S3_CLOCK` — WROOM-1 with **functionally grouped pins** (same pad
numbers as the stock symbol/footprint) so buses leave toward their blocks;
`TAS5760M` (DAP-32), `TPS55340`, `TPS61023`, `AOSD32334C`, **`HY2111`**
(U3 protector — same body/pin geometry as the stock `AP9101CK6` symbol it
replaced 2026-07-17, HYCON pin names OD/CS/OC), **`X40_879`** (M1 stepper,
pins 1-4 = 1e-4e external shaft, 5-8 = 1i-4i internal), `VBAT`/`PVDD`
flags, plus flattened clones of stock `extends` symbols (AO3400A, AO3401A,
2N7002, DM3AT, MCP23017, **74AHCT1G125** — U15 NeoPixel buffer) so ERC
never reports lib_symbol_mismatch. NeoPixels use the stock `LED:SK6812`
symbol (standalone, RGBW-compatible pinout).

## Custom footprints (`clock.pretty`, fp-lib-table nickname `clock`)
| Footprint | Part | Source |
|---|---|---|
| `OnSemi_QRE1113GR` | U14 homing sensor (SMD) | land pattern from datasheet p.8 (Case 100CY): 1.66×0.79 pads, 5.66 span, 2.59 row pitch |
| `Fuse_TCO_Cantherm_SDF_L10.5mm_D4.0mm_P20.32mm_Horizontal` | F1 TCO 77 °C | Cantherm SDF drawing: Ø4×10.5 body, AWG18 leads, 1.3 mm drills, ≥3 mm bend clearance |
| `Juken_X40-879_DualShaft` | M1 stepper (direct PCB mount) | factory STEP (`X40 v5`) cross-checked vs X27 base-spec Fig. 6: 4× Ø0.8 pin drills on 22.86×7.62 grid (pads 1-4 = 1e-4e), 4× Ø1.8 rim-tail drills at (±13.96, ±6.36) (pads 5-8 = 1i-4i), 3× Ø3.0 NPTH snap pegs, Ø4.6 NPTH shaft hole at (0, −6); body Ø31.5 on the far side (flip to back in layout, shafts through-board); 3D STEP in `3d/X40_879.step` (rotation/offset eyeballed — cosmetic) |
| `GCT_USB4160-03-0230-C_USB_C_Vertical` | J1 vertical USB-C | user-supplied vendor CAD footprint (`~/Downloads/KiCADv6/`), cleaned: 24 SMT pads kept verbatim; the 4 stake slots made **plated, pad `SH`** (GCT drawing 2/4 marks them Solder Area; 1.10×0.61 drill / 1.60×1.10 pad at ±4.00/±1.43 — matches the drawing exactly); pegs → NPTH Ø0.71 (right one slotted); stray Edge.Cuts/S_3 artifacts removed. Only the USB 2.0 subset is wired (16P symbol) → SS pads unconnected. ⚠ cross-check vs the GCT drawing before fab |
| *(unused, kept)* `SameSky_UJ20-C-V-C-2_USB_C_Vertical_TH` | ex-J1 candidate (TH) | drawn 2026-07-19 while the UJ20-C-V-C-2 was primary (superseded by the in-stock USB4160); kept in case the TH option returns |
| *(removed 2026-07-19)* | ~~`Hirose_FH34SRJ-10S-0.5SH…`~~ J5 display FPC | file kept in `clock.pretty` for a future display variant; no longer referenced |

## Validation (all automated in `gen/`)
- `kicad-cli sch erc` → **2 known-benign items**:
  LT3652 BAT output ↔ VBAT PWR_FLAG, QRE1113 open-emitter ↔ GND flag.
- `gen/build.py` lint: no dangling wires, no body overlaps, no wires through
  symbols; junctions auto-placed with eeschema's own rules (T-points,
  pin-joins; wires broken at crossing-joins) so a GUI re-save is a no-op.
- Every symbol has a footprint (audited 2026-07-20 — including the 7
  SK6812 pixels on the stock `LED_SK6812_PLCC4` pattern and the QRE1113GR
  homing sensor); the parts without stock land patterns use `clock.pretty`
  (table above: QRE1113GR, TCO, X40.879, USB4160). Headless `kicad-cli` may
  warn `footprint_link_issues` for exactly those (it skips the project
  fp-lib-table) — eeschema resolves them fine.

### Desk-verified 2026-07-17 (was "reconcile before fab")
1. **TAS5760M bootstrap caps**: the 33 nF placeholder was **wrong** — the
   datasheet typical application (Fig. 62, DAP-32) uses **0.22 µF**;
   §9.2.2.2.3 warns deviating can cause "destructive failure".
   C180–C183 are now **220 nF**.
2. **TPS55340 compensation** (R48 2.55 k / C134 0.1 µF / C135 100 pF):
   recomputed per datasheet §8.2.1.2.11 for our operating point
   (VIN 3.0–4.2 V → 12 V/1 A, 500 kHz, C_OUT ≈ 30 µF eff):
   f_RHPZ ≈ 25 kHz at VIN=3.0 V → BW ceiling ≈ 8.5 kHz; the network gives
   ~6 kHz BW with the zero at 624 Hz ≈ BW/10 and the C135 pole at ~620 kHz —
   matches the datasheet method. Optional bench Bode/transient tune only
   (three 0603s, trivially swappable); **not a fab blocker**.
3. **Protector ↔ FET gates** *(re-verified 2026-07-17 for the HY2111-GB
   that replaced the NRND AP9101C)*: HYCON pin table is arrangement-identical
   (SOT-23-6: 1 OD · 2 CS · 3 OC · 4 NC · 5 VDD · 6 VSS) — OD→G1 with S1 on
   the cell− side (discharge FET), OC→G2 with S2 on the pack− side (charge
   FET); support values re-set from the HY2111 datasheet §10: R20 **100 Ω**
   (allowed 100–200), C109 100 nF (≥10 nF), R21 **2 kΩ** (allowed 1–2 k).
   Wiring unchanged and correct as drawn.

Remaining bench work is bring-up tuning, not schematic risk: QRE1113GR
threshold (10 k load), LT3652 NTC window, TPS55340 loop response, speaker
EQ, EM14 divider levels (100k/200k → ~3.2 V) on a scope. Protector is
**HY2111-GB** since 2026-07-17 (AP9101C went NRND/obsolete; sourcing
rationale in the root README decision log).

### v0.19 cube redesign (2026-07-19)
Display (J5 + FH34), panel LED string (J8 + Q5) and BTN1–3 removed;
added the **7× SK6812 RGBW chain** (D40–D46, U15 74AHCT1G125 buffer,
R110 330 Ω, C230–C237), the **EM14 knob** on J10 (ZH 1×06, +5 V; dividers
R111/R112 + R114/R115, ENC_SW RC R113/C239 → IO17), **J11 radio toggle**
(ZH 1×02, GPA3), and the **vertical USB-C** (GCT USB4160-03-0230-C,
adapted vendor footprint; J7/J10/J11 standardized on JST ZH 2026-07-20).
ERC still returns only the 2 known-benign items. Every symbol resolves to
a footprint (stock KiCad or `clock.pretty`) — audited 2026-07-20.

## Regenerating
```
cd gen && python3 mksym.py && python3 build.py
```
**GUI tuning workflow:** hand-tune symbol/text positions in eeschema, save,
then `python3 harvest.py` — it re-extracts every symbol's position and
ref/value text placement into `cosmetics.py`, which `build.py` applies on
top of the block code (wires are still drawn by `b_*.py`, so if you MOVE a
symbol far enough to need rewiring, fix the block file — lint flags it).
`clock.kicad_pro` is only written if missing (eeschema owns it after that);
`sym-lib-table` / `fp-lib-table` are rewritten each build.
`build.py` finishes by normalizing the sheet through **`kicad-cli sch
upgrade`** (needs KiCad 10 installed), so the file on disk is in the current
KiCad format with canonical ordering and **stable per-pin uuids** — opening
and saving in eeschema produces **zero diff**, and rebuilds are
byte-reproducible.
`sch2.py` — manual-placement builder (explicit wires; eeschema-rule
junctions; collinear-wire merge; lint) · `b_*.py` — one file per block (all
coordinates hand-chosen, 1.27 mm grid) · `mksym.py` — custom symbols.
Validate: `kicad-cli sch erc clock.kicad_sch`
(`/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli`).
