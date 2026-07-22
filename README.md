# Wooden Smart Clock — Hardware Specification

> Living spec for a high-quality, low-power desk alarm clock: a **walnut cube (~120 mm)** with an **aluminum front plate**, a centered **Ø~90 mm MCU-driven analog dial behind glass** (aluminum hands on walnut, 12 black hour dots, no numerals), a row of **5 RGBW status LEDs** under the dial, and one **aluminum knob (rotate + press)** centered on the top face.

> 🔩 **Manufacturing constraint — HAND-SOLDERABLE PARTS ONLY.** The bare PCB is fab'd externally; **every part is hand-soldered with an iron.** No **QFN / DFN / WSON / BGA / WLP / LGA** silicon sits bare on the board — every active IC is a **leaded/gullwing** package (SOIC / SOP / SSOP / TSSOP / **HTSSOP** / **MSOP** / SOT-23) or a **castellated/edge module**. The two power parts (amp `TAS5760M`, charger `LT3652`) are HTSSOP/MSOP **PowerPAD** — leads iron-solderable, belly pad on a thermal-via array (back-side hot-air optional). Functions that *only* exist leadless → **pre-made breakout modules** (env/MEMS sensors — **BME688, TSL2591, LIS3DH** as I²C Qwiic/STEMMA boards; or a custom SMT daughterboard, §8) or **dropped** (fuel gauge → ESP32 ADC). **Passives ≥ 0603** (0402 min); connectors hand-solderable (through-hole / wide-pad SMD: USB-C, FPC/ZIF, JST, SD cage). **This grows the PCB — accepted.**

**Status:** v0.19 draft · **Owner:** you (FW/HW) · **Last updated:** 2026-07-19

**Changelog**
- v0.19 — **Product redesign: walnut cube + aluminum face — display dropped, NeoPixel status/face LEDs + top-knob UX added (2026-07-19).** New form (§3): perfect **walnut cube ~120 mm** (size flexible, larger eases manufacturing), all faces wood except an **aluminum front plate** (cube − 10 mm in W/H, flush-centered → **5 mm wood reveal**) with a **centered Ø~90 mm dial opening** behind glass — **aluminum hands over a walnut dial**, no numerals, **12 black hour dots**; below the opening, **5 status-LED holes + icons** (Lucide-style `bell`, `alarm-clock`, `clock`, `volume-1`, `battery`); an **aluminum knob (rotary + push)** perfectly centered on the top face; wake light, speaker openings and USB port on the **back**. Consequences: **(1) Sharp LS032B7DD02 + FH34 FPC connector dropped** (§4; their datasheets stay in [`/datasheet`](datasheet/) for possible future reuse) — SPI2 is now **microSD-only**, software-VCOM notes moot, `LCD_DISP`/`LCD_CS` freed. **(2) Panel-LED string dropped** (Cree CLM3C ×≤12 + its AO3400A + one LEDC channel recovered) → **7× SK6812 RGBW NeoPixels on the PCB** ([Adafruit 2758](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2758/6134706), 10-pack $5.95): **5 status + 2 dial-illumination**, chained on the **5 V rail** (works on battery) behind **one data GPIO (IO7 → RMT)** through an **SN74AHCT1G125 3.3→5 V buffer** ([DigiKey 376028](https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT1G125DBVR/376028), $0.14). **(3) UI = single knob**: Bourns **EM14A0D-C24-L064S** **optical** encoder (**no detent, 64 CPR**, integral push switch, **5 V** opto-ASIC, [DigiKey 954403](https://www.digikey.com/en/products/detail/bourns-inc/EM14A0D-C24-L064S/954403), $34.00) under a Kilo **OEJNI-90-1-5** machined aluminum knob (Ø23.5 mm, 1/4″ bore + set screw = the encoder's 1/4″ flatted shaft, [DigiKey 5970396](https://www.digikey.com/en/products/detail/kilo-international/OEJNI-90-1-5/5970396), $13.43), off-board on J10 (JST **ZH** 1×06 B6B-ZR incl. **+5 V** — same family + pre-crimped A06ZR cable as the sensor board) → **A/B are 5 V outputs → 100k/200k dividers → IO47/48 (PCNT)**, **ENC_SW moved MCP23017 → host IO17** (freed by the display) for a tight-debounce interrupt; **rear tactiles BTN1–3 dropped**, one **radio-disable toggle** remains on the expander (GPA3) via **J11** (JST ZH 1×02). **(4) USB-C receptacle → vertical GCT USB4160-03-0230-C** (24-pin USB 3.2 Gen2 vertical, of which only the **USB 2.0 subset** — VBUS/GND/**CC/D±** — is wired, SS pads unconnected → native USB-JTAG + 15 V PD intact; [DigiKey](https://www.digikey.com/en/products/result?keywords=USB4160-03-0230-C), **$1.22, 5,753 in stock**, active) standing on the PCB, port exits the back wall directly — **USB4105 + Adafruit 6069 panel extension dropped**; footprint adapted into `kicad/clock.pretty` from the user-supplied vendor CAD file. *(Vetting trail: **USB4140** rejected = 6-pin power-only, no D±; **UJ20-C-V-C-1** rejected = legs/layout specified for a 0.8 mm PCB; **UJ20-C-V-C-2** (TH, $0.95) was briefly primary but is backorder-only; **USB4145-03-0170-C** ($1.05) remains a same-land-family 16-pin alt.)* **(5) Orientation-awareness (R14) retired** (the cube is fixed upright; LIS3DH stays for tap-to-snooze). Unchanged: wake COB (12 V, plugged-only), audio chain, charger + battery safety, sensors, homing, steppers. Schematic (`kicad/`) regenerated; docs synced.
- v0.18 — **Protector re-picked (AP9101C went NRND/obsolete) → HYCON HY2111-GB** (SOT-23-6, OV 4.28 V / OD 2.90 V, pin-arrangement-identical → same nets; support values per its datasheet: R VDD 330 Ω→**100 Ω**, R CS 2.7 k→**2 kΩ**; AOSD32334C dual-FET unchanged). Quality single-qty alternatives (ABLIC S-8261ABMMD, Nisshinbo R5478N) are reel-only/no-stock at DigiKey → HY2111-GB sourced **LCSC C82747** (off-DigiKey like the CH224K). **X40.879 mounts directly on the PCB**: custom KiCad footprint + symbol drawn from the factory STEP + pinout figure (8 solder pins, Ø10 shaft clearance hole, both shafts pass through the board); **J4 stepper connector dropped**. **USB-C port moved to the enclosure wall** via the Adafruit **6069** panel-mount **extension** ([DigiKey 25897275](https://www.digikey.com/en/products/detail/adafruit-industries-llc/6069/25897275), ~$4.50) — the on-board **USB4105** receptacle + CH224K stay unchanged (a passive C-plug→panel-C-jack passthrough keeps CC/PD intact; the resistor-equipped Adafruit 6153 pigtail was rejected — its built-in 5.1 kΩ CC pull-downs would break the 15 V PD contract). Datasheets: `battery_protector_ap9101c.pdf` → `battery_protector_hy2111.pdf`. Schematic (`kicad/`) regenerated; docs synced.
- v0.17 — **Homing sensor re-picked: Everlight ITR8307 (went unavailable) → onsemi QRE1113.** Same reflective **phototransistor with analog output → 1 ADC pin**, so it drops into the **same 150 Ω ballast / 10 kΩ load network** unchanged (§5, [`power_values.md`](power_values.md)); no firmware or pin-map change. Trade-off: the QRE1113 is **larger (≈3.6×2.9×1.7 mm** vs the ITR8307's 3.4×1.5×1.1 mm) → a marginally bigger dial hole — still far smaller than the dropped **TCND5000** (6×4.3×3.75 mm). Through-hole gull-wing `QRE1113` (SMD `QRE1113GR` alt), trivially hand-solderable, **DigiKey 2175990 (~$0.76)**. Synced §5/§8/§16b + block diagram + [`datasheet/README.md`](datasheet/README.md) (`sensor_homing_optical_itr8307.pdf` → `sensor_homing_QRE1113-D.PDF`).
- v0.16 — **LED 12 V source resolved · MCP23017 IO expander added · pin-level map (`esp32.md`) + schematic values (`power_values.md`) · backup mode / TCO / VCOM decided.** LED: **no barrel jack** — the wake COB runs off the shared **TPS55340 12 V boost, plugged-only**; on battery the amp PVDD auto-drops to the **5 V rail via an LTC4412 mux** (quieter alarm) and the 5 V panel LEDs stay on (§9/§10, [`led.md`](led.md)). **Added the Microchip MCP23017** I²C IO expander (SOIC/SSOP-28) on the shared bus to offload slow/static lines (SPK_SD, 12 V-boost EN, stepper STBY, buttons, encoder-switch, PD PG, charger CHRG/FAULT) → **net −7 host GPIO** (§6/§16b; datasheet §19). Full **pin map now in [`esp32.md`](esp32.md)** (native USB-JTAG on IO19/20, **software VCOM** with EXTMODE=L, shared display+SD on SPI2 → **32/33 module pads**), and **per-converter passive/FB/comp values in [`power_values.md`](power_values.md)**. Decisions (2026-07-12): **backup mode = adaptive** (Wi-Fi modem-sleep → deep-sleep at low SoC); **one-shot ~77 °C TCO added** to the cell path (§10); **software VCOM** locked. Corrected the 32.768 kHz load-cap value (**~18 pF**, was mis-stated). Synced [`datasheet/README.md`](datasheet/README.md), [`power.md`](power.md), [`CLAUDE.md`](CLAUDE.md).
- v0.15 — **Sensor set consolidated to 3 parts + hand-assembly path chosen (§8).** Dropped the redundant **TMP117** (any temp sensor already gives the reading); folded **SHT45 + SGP41 → one Bosch BME688** (T + RH + pressure + VOC/gas via BSEC — one chip, one board on the bus); accel **BMA400 → LIS3DH** (no leaded accel exists; low-res tap+gravity is all we need). **Locked set = BME688 + TSL2591 + LIS3DH on both build paths → firmware identical either way.** **Build = option 2a: a STEMMA QT / Qwiic daisy-chain** of three ready Adafruit boards on one 4-wire I²C chain — **BME688 (Adafruit 5046, ~$19) + TSL2591 (1980, $6.95) + LIS3DH (2809, $4.95)** — no leadless silicon touched by the iron. **Future option 2b:** the **same** three bare chips on one small custom daughterboard, JLCPCB SMT-assembled, hand-solder only its header → smallest footprint, same part numbers → same firmware. Filed `sensor_env_bme688.pdf` + `sensor_accel_lis3dh.pdf`; removed the now-unused TMP117/SHT4x/BMA400 datasheets. Synced §8/§16a/§16b + [`datasheet/README.md`](datasheet/README.md).
- v0.14 — **Homing reworked: 2× DRV5032 Hall + hand magnets → 1× reflective optical (Everlight ITR8307).** One sensor behind a **punched dial hole**, sequential per-shaft homing, **no hand magnets** (§5). Evaluated the longer-range **Vishay TCND5000** for a >3 mm hand stand-off and **dropped it** — at **6×4.3×3.75 mm** it's ~5× the ITR8307 footprint (**3.4×1.5×1.1 mm**) → a bigger dial hole; **ITR8307 locked for the smallest hole**, hands kept **<3 mm** off the dial. Synced §3/§5/§8/§16b/§17 + [`datasheet/README.md`](datasheet/README.md) (dropped the stale Hall rows; only `sensor_homing_optical_itr8307.pdf` filed).
- v0.13 — **RTC IC dropped → ESP32-S3 RTC + 32.768 kHz crystal (no battery).** A battery-less RTC chip loses time on total power loss exactly like the S3, so it adds parts without fixing anything; the S3's internal RC oscillator alone drifts %-level over temperature (bad for a clock). Fix = a ~30¢ **32.768 kHz watch crystal** on XTAL32K (GPIO15/16) → ±~20 ppm ≈ 1.7 s/day, disciplined by **SNTP**. On loss of USB **and** the 18650, time is lost → re-sync on boot with a clock animation (accepted). Removed the RV-3028-C7/DS3231SN row + datasheet; §8, §16, §17, §6c synced.
- v0.12 — **Sensors, display connector & RTC finalized to precise parts + datasheets** ([`/datasheet`](datasheet/) rows 11–16): display FPC → **Hirose FH34SRJ-10S-0.5SH** (10-pin 0.5 mm ZIF, the panel spec's own recommended mate, on-board); T/RH → **SHT45** (±0.1 °C / ±1 %RH, replaces SHT40) + optional dedicated **TMP117** (±0.1 °C); light → **TSL2591** (188 µlx–88 klx, weak-light, replaces VEML7700); accel+orient → **BMA400** (now datasheeted); RTC **kept = RV-3028-C7** (ULP 45 nA) / **DS3231SN** (SOIC-16, on-board) — the S3's internal RTC dies on full power loss, so a backed-up RTC gives instant-correct hands at cold boot + holdover. Production BOM (§16b), §8 sensor table, and [`datasheet/README.md`](datasheet/README.md) synced; leadless sensors ride on I²C breakouts, the FPC connector is on-board.
- v0.11 — **Hand-solderable parts only (hard mfg constraint):** every bare-PCB IC is now a leaded package — no QFN/DFN/BGA/LGA. Swaps: amp **TAS5825M (VQFN) → TAS5760M** (HTSSOP-32, I²S+I²C; HPF/limiter DSP moves to firmware); driver **2× DRV8835 (WSON) → 2× TB6612FNG** (SSOP-24); PD **STUSB4500 (QFN) → CH224K** (ESSOP-10, resistor-set, off-DigiKey); charger **BQ25628E (WQFN) → LT3652** (MSOP-12E buck, BAT-node power-path, resistor-set 4.05 V + NTC + timer — no I²C, single-window temp); **fuel gauge MAX17048 (µDFN) dropped → ESP32 ADC**; protector **LC05111 (DFN) → S-8261 (SOT-23-6) + AO4800 dual-FET (SO-8)**. Leadless-only env sensors → **I²C breakout modules**. New top-of-file mfg note; datasheets swapped in [`/datasheet`](datasheet/); board grows — accepted.
- v0.10 — **Power/safety simplified (Option A)**: buck-boost **BQ25792 → BQ25628E** (1-cell buck charger, integrated FETs — buck is enough since input always > 1S cell); independent **cell protector = onsemi LC05111** (integrated FET) + **reverse-polarity P-FET** + NTC/JEITA + TVS = **double-redundant, industry-standard for 1S**. Dropped BQ29700+FETs, **secondary OVP, one-shot TCO, PPTC** (over-engineering for a single cell). Datasheets added (STUSB4500, BQ25628E, MAX17048, LC05111); `power.md` now has a **How-to-use / bring-up** section. Power subtotal ~$23 → ~$15.
- v0.9 — **Power tree locked** ([`power.md`](power.md)): **USB-PD sink (STUSB4500, 15 V)** → **BQ25792** buck-boost charger/path (+ MAX17048), 1S. Rails 3.3 / 5 / 12 V-audio-boost / 15 V-VBUS-LED. Battery = **user-supplied 18650 in a holder, Li-ion ONLY (labeled)**; **48 h backup**; health-cap ~80 %. **Mandatory board safety for any 18650**: reverse-polarity (LM74700-Q1), primary PCM (BQ29700+FETs), redundant secondary OVP, PPTC + thermal cutoff, NTC/JEITA, TVS, FR barrier — triple-redundant overcharge, layered over-discharge. Charger BQ25185 → **BQ25792**; §10/§16/§17 updated.
- v0.8 — **Amp locked = TI TAS5825M** (I²S + I²C closed-loop Class-D w/ DSP), PBTL mono into the 4 Ω DMA58-4: PVDD 5 V for ~3 W, optional ~12 V boost for ~8–12 W; DSP loads a ~150–180 Hz high-pass + limiter to protect the 2 mm-Xmax driver. MAX98357A demoted to simple/no-DSP alt. **Driver confirmed = 2× TI DRV8835** (dual H-bridge each; VM 5 V for full torque, VCC 3.3 V; PHASE/ENABLE with PWM microstep → 8 GPIO). Full datasheets added to [`/datasheet`](datasheet/); [`datasheet/README.md`](datasheet/README.md) now carries a **System IO & power domains** table (rails + per-part GPIO/voltage).
- v0.7 — **Speaker PC68-4 → Dayton DMA58-4** (2″ dual-magnet full-range — the driver we actually have a datasheet for) → smaller driver relaxes body depth to **≥45–60 mm / ~100–250 cc** chamber (optional **DMA58-PR** passive radiator for bass). **Movement locked = Juken X40.879**: its datasheet is a short **pinout addendum** that defers to the **X27 base spec** for torque/current/dimensions — both now in [`/datasheet`](datasheet/). Added the **full Sharp LS032B7DD02 device spec** (replaced the marketing brief); corrected display **active area → 42.67 × 68.07 mm** and **power → 30 µW hold / 250 µW update @ 5 V (VDD 4.8–5.5 V)**; **X40.879 price → ~$14** (was ~$25, DigiKey). Datasheets summarized in [`datasheet/README.md`](datasheet/README.md).
- v0.6 — **Split-face locked as THE design**: circular MCU-driven analog clock (~75 mm) + reflective monochrome info panel (**Sharp LS032B7DD02**). **Single MCU** (ESP32-S3). New **orientation awareness** (R14): flat (clock left / display right) ↔ standing (clock top / display bottom), accel-sensed; MCU rotates the display and re-references the analog "12". Movement re-picked for 75 mm → **Juken X40.879** (DigiKey) + external Hall homing. Motor driver **DRV8833 → DRV8835** (DRV8833 is NRND). Backlight subsystem removed (reflective display). Shopping list moved **Mouser → DigiKey**. Sections renumbered consecutively; wide color bar-TFT demoted to *superseded*.
- v0.5 — locked **Dayton PC68-4** speaker; added **Juken X10.506** + more movements; new requirement **R12** (MCU sets/adjusts analog time → sweep-quartz demoted).
- v0.4 — added **speaker comparison** and the **analog-movement comparison** for the split-face option.
- v0.3 — added **ESP32-S3** single-chip path + firmware libraries, **audio sizing**, internal packaging notes, and a distributor-linked shopping list.
- v0.2 — display moved E-Paper → fast **bar TFT** (NHD-3.9); MCU → STM32U5 + ST67W611M1, with ESP32-S3 as single-chip alt.
- v0.1 — initial spec (EPD + nRF5340).

---

## 1. Requirements (source of truth)

| # | Requirement | Primary approach (see §) |
|---|-------------|--------------------------|
| R1 | Accurate time (TZ, seconds, day, date), periodic server sync | ESP32-S3 RTC off a 32.768 kHz crystal + SNTP over Wi-Fi (no RTC IC/battery) (§6, §8) |
| R2 | ALS-driven lighting, **true 0 emission at night** | Wake light + status/dial NeoPixels, hard-off + ALS gating (§8, §9) |
| R3 | Alarm, tap-to-snooze, loud multi-asset audio, sunrise glow | Accel tap-detect + Class-D audio + tunable-white sunrise wake light (§7, §9) |
| R4 | Air quality (VOC/AQI), temperature, humidity | **BME688** (T/RH/press/VOC in one chip); optional CO₂/PM (§8) |
| R5 | USB-C powered, battery backup + charge, run untethered | USB-C PD sink + charger + Li-ion + ADC gauge (§10) |
| R6 | Glanceable state without a screen: alarm armed, set modes, volume, low battery — **0 light when idle** | **5 RGBW status LEDs** behind icon holes in the aluminum face; hands double as the readout in set modes (§3, §9, §12) |
| R7 | Wi-Fi + BLE, **single MCU** driving everything | ESP32-S3 single chip: motors + audio + sensors + LEDs (§6) |
| R8 | Single-knob on-device UX, minimal other controls | Top-center knob: **press = cycle modes, rotate = adjust** (§12); rear radio-off toggle; config via app |
| R9 | Companion iOS/web app: BLE pair → Wi-Fi provision, alarms, tone upload, widgets | BLE provisioning + SD asset store (§13) |
| R10 | Extensible: more sensors, multiple sizes | I²C sensor bus + JST connectors + modular FW (§14) |
| R11 | High-quality speaker + high-quality assets | Full-range driver + DSP amp + WAV/FLAC on SD (§7) |
| R12 | MCU fully sets/adjusts the analog time (TZ/DST jumps, NTP-synced, drift-corrected, both directions) | Bidirectional, absolutely-addressable dual-shaft stepper + homing (§5) |
| R13 | **Cube form:** walnut cube, aluminum face, centered Ø~90 mm analog dial (hour + minute) behind glass | Dual-shaft movement behind the face plate (§3, §5) |
| ~~R14~~ | ~~Orientation-aware (flat/standing)~~ — **retired v0.19**: the cube sits fixed upright | Accel kept for tap-to-snooze only (§8) |

---

## 2. Key design decisions & open tensions

The v0.19 redesign collapses the earlier two-part split-face into **one face**: a walnut cube whose only readout is the **analog dial** plus **5 status LEDs**. No screen at all.

- **Circular analog dial (the product):** hour + minute hands, **fully MCU-driven** (dual-shaft stepper + homing). Real hands, silent, dark at night. In set modes the **hands themselves are the display** (they show the alarm time / follow the knob).
- **Status row:** 5 **RGBW NeoPixels** behind small holes + icons in the aluminum face (`bell`, `alarm-clock`, `clock`, `volume-1`, `battery`) — glanceable state with **zero emission when idle**.

**Baked-in:**
- **Single MCU** (ESP32-S3) drives hands + audio + sensors + LED lighting — no coprocessor.
- **Numeral-free dial** (12 black dots) + absolute stepper addressing → the MCU can jump the hands anywhere (time set, alarm preview, TZ/DST).
- Light emitters: the **wake light** (tunable-white sunrise, 12 V), the **2 dial-illumination NeoPixels**, and the **5 status NeoPixels** — all hard-off = 0 emission at night.
- Split power domains (always-on low-power + bursty Wi-Fi).

**Superseded:** the split-face reflective **Sharp memory-LCD** info panel (v0.6–v0.18) and, before it, the wide color **bar-TFT** (rationale kept in the Decision log & §17).

---

## 3. Form factor — walnut cube, aluminum face (R13)

**A perfect cube, ~120 mm per side** (dimension flexible; slightly larger eases manufacturing). All six faces **walnut** except the front:

- **Front = aluminum plate**, square, **cube − 10 mm in width and height** (~110 mm), flush with the front plane and centered → a **5 mm walnut reveal** around it.
- **Centered circular opening Ø~90 mm** (flexible) with a **glass disc** between the aluminum face and the hands.
- Behind the glass: **walnut dial background** (same wood as the walls), **aluminum hour + minute hands**, **no numerals — 12 black dots** for the hours. A small punched hole at the index radius serves the QRE1113 homing sensor (§5).
- **Below the opening:** a horizontal row of **5 evenly spaced LED holes with icons** (Lucide-style `bell`, `alarm-clock`, `clock`, `volume-1`, `battery`; engraved icons vs. text still open) — the **status LEDs** (§9, §12). The 5 SK6812 sit on the main PCB, light pipes / drillings through the plate.
- **Top face:** one **aluminum knob** (premium HiFi feel), **perfectly centered**, on a smooth no-detent encoder with a **press function** (§12).
- **Back face:** wake-light aperture, speaker openings, **USB-C port** (vertical receptacle on the PCB, exits straight out the back), and the **radio-disable toggle**.

Reference sketch: `~/Downloads/Clock_V4.png`. The dial is behind the front plate, the movement solders to the main PCB (shafts through-board, §5); a ~100–250 cc sealed speaker chamber and the cell compartment fill the body (§7a, §10).

---

## 4. Display — *removed in v0.19*

The reflective **Sharp LS032B7DD02** info panel (and its FH34SRJ FPC connector) was **dropped with the cube redesign** — the product now has no screen; state is shown by the hands + 5 status LEDs (§3, §12). The panel/connector datasheets remain in [`/datasheet`](datasheet/) in case a screen returns in a future variant, and the old selection rationale lives in the Decision log. Freed by the removal: the shared-SPI display device (SPI2 is now **microSD-only**), host pin **IO17** (`LCD_CS` → `ENC_SW`), expander line GPB3 (`LCD_DISP`), and the 5 V panel load.

---

## 5. Analog clock module — circular, MCU-driven (R12, R13)

**Design:** a circular **Ø~90 mm** dial (v0.19; was 75 mm) with **hour + minute** hands on a **dual-shaft concentric** stepper, so the MCU sets any absolute time. Silent, real hands, dark at night. **No seconds hand** (and no info panel since v0.19) — seconds aren't displayed. The hands double as the UI readout in the knob's set modes (§12).

**R12 recap:** the MCU must **set/adjust the displayed time on demand** — jump for TZ/DST, correct NTP drift, both directions — which needs a **bidirectional, absolutely-addressable** movement with a **home reference** (rules out forward-only sweep-quartz).

**Motor comparison** — must do **360° on *both* hands**, be **extremely quiet**, use **light hands**, and let the **MCU set any absolute time** (R12):

| Motor | Type · 360° both? | MCU sets abs. time (bidir + homing) | Torque → hand size | Noise | Size | Power | ~Cost | Where |
|-------|-------------------|-------------------------------------|--------------------|-------|------|-------|-------|-------|
| **Juken X40.879** ⭐ **(75 mm pick)** | Dual coaxial shaft (hour+minute) · ✅ | ✅ bidirectional; **add external optical home** (no built-in zero-detect) | higher → OK for ~35 mm hands / 75 mm dial | very quiet (Swiss, microstep) | vertical, compact | ~15–20 mA | ~$14 | **DigiKey** (Juken) |
| Juken X10.506 (small ≤50 mm) | Dual-shaft concentric **clock** motor · ✅ | ✅✅ **zero-detection variant = built-in homing** | low, indicator-class (~1–2 mN·m) → light short hands | very quiet | ~Ø28 mm | ~15–20 mA | ~$11 | Minitools / DigiKey (Juken) |
| VID28-05 / BKA30D-R5 (budget) | Dual-shaft concentric · ✅ (**buy 360° variant**) | ✅ bidirectional; **add external optical home** | ~4 mN·m → ≤~10 g hands | microstep ~20–30 dBA | 29×59 mm | ~15–20 mA/coil | ~$4–6 | AliExpress/Amazon/eBay (**not DigiKey**) |
| Sonceboz 6407 | Dual-shaft, automotive-grade · ✅ | ✅ (+ homing) | higher, smooth | very quiet | ~30 mm | low | $$ | mfr direct (MOQ) |
| Juken/Switec X27.168 / X25.168 | **Single**-shaft gauge · ⚠️ 315° (remove stop) | ✅ bidirectional, but single shaft → **need 2 + coax mechanics** | ~0.6–1 mN·m (weakest) | microstep quiet | Ø26 mm | ~20 mA | ~$4–8 | **DigiKey** (X27.168) |
| Continuous-sweep quartz | Geared clock · ✅ | ❌ **forward-only + coupled → can't jump for TZ/DST** | strong (real hands) | **silent <20 dBA** | 56×56×15 mm | µA–mA | $3–10 | clock-parts shops |
| 28BYJ-48 geared stepper | Single-shaft geared · ✅ | ✅ electrically | strong (~34 mN·m) | **buzzy 45–55 dBA ✗** | 28 mm+box | 100–200 mA | ~$2 | everywhere |

**Picks (with R12 + the ~90 mm dial):**
- **Primary: Juken X40.879** ⭐ — dual coaxial hour+minute, **DigiKey-stocked (~$14)**, holding torque 3.5–4 mN·m — sized for ~35 mm hands on the old 75 mm dial, and still fine for the **~42 mm minute hand of the Ø90 mm dial *if the aluminum hands are kept thin/light and counterbalanced*** (aluminum is ~⅓ the density of brass; verify hand mass ≲1 g on the bench). Home with **one reflective optical sensor** (no magnets — see homing note below) since the X40 has no built-in zero-detect; Swiss-quiet with microstepping. Its datasheet is a short **pinout addendum** (two *independent* dual-shaft bipolar coils, 8 pins, X27-compatible mechanics) that **defers to the X27 base spec** for torque/current/dimensions — so both `stepper_motor_x40-879.pdf` **and** `stepper_motor_x27_base-spec.pdf` are kept in [`/datasheet`](datasheet/).
- **Small alt: Juken X10.506** — if you drop to ~50 mm; its **zero-detection variant gives built-in homing** (cleanest R12), but too little torque for 75–90 mm hands.
- **Budget alt: VID28-05 / BKA30D-R5** — same concept for ~$5, but off-DigiKey; add optical homing.
- **Rejected:** sweep-quartz **fails R12** (forward-only, coupled); 28BYJ-48 (noise).

**Design notes:**
- **Homing (optical, single sensor, no magnets):** one **reflective optical sensor** (onsemi **QRE1113**, ≈3.6×2.9×1.7 mm) sits behind a small **hole punched in the dial** at a chosen index radius that *both* hands sweep over. Because the MCU drives the two shafts independently, home them **sequentially** so the minute-over-hour overlap never matters:
  1. park the **minute** hand ~180° off-index; slowly sweep the **hour** hand until the sensor edge triggers → hour homed;
  2. park the **hour** hand off-index; sweep the **minute** hand until it triggers → minute homed;
  3. drive both to the true time. Add a tiny high-contrast index mark (white dot / reflective foil / notch) on each hand's underside at the sensing radius for a crisp, finish-independent edge. After homing, position is held by **step counting**; re-home on boot, after each NTP sync, and after long runs.
  - *One sensor is enough* — sequential homing removes any "which hand is which" ambiguity. (If you ever want *simultaneous* homing, drive the two hands at **distinct step rates** so the sensor's pulse timing identifies each — but sequential is simpler and foolproof.)
  - *Why not ToF (VL53L1x):* wide FoV (~15–27°), coarse angular resolution, and limited sample rate make it poor for a crisp home index; a reflective edge is far more repeatable.
  - *Sensor locked = onsemi QRE1113GR* (SMD gull-wing, **DigiKey 965451**, ~$0.36; lead form locked 2026-07-17): the earlier **Everlight ITR8307 went unavailable**, so homing moved to this widely second-sourced reflective part — **same phototransistor analog-out interface → same ADC network** (§16b, `power_values.md`), a true electrical drop-in. Its short range is fine because the hands sweep **<3 mm** off the dial (QRE1113 peaks ≈1 mm; index mark on the underside). At **≈3.6×2.9×1.7 mm** it's a bit larger than the ITR8307 → a marginally bigger dial hole, but still far smaller than the **Vishay TCND5000** (6×4.3×3.75 mm, peak 6 mm, range 2–25 mm), which was considered for a >3 mm stand-off and **dropped**; keep the hands close instead.
- **Re-time / set modes:** TZ/DST changes and the knob's *set clock* / *set alarm* / alarm-preview moves = step to the new absolute position (fast, both directions), no re-home needed. *(Orientation re-referencing retired with R14 — the cube is fixed upright.)*
- Buy/reuse the **360°/no-stop** movement; **microstep @ >20 kHz PWM** for silence; **light, counterbalanced hands** — the aluminum hands (§3) must stay thin (mass ≲1 g each) on the ~90 mm dial.
- **Driver:** **2× TB6612FNG** (SSOP-24, hand-solderable, no exposed pad) — one per shaft; VM 5 V, VCC 3.3 V; **PWM the IN pins with PWMA/PWMB tied high → sign-magnitude microstep in 8 GPIO** (parity with the old DRV8835). *DRV8835/DRV8833 are WSON/HTSSOP — replaced for hand-assembly.* **Homing:** **1× reflective optical sensor** (onsemi QRE1113) behind a hole in the dial — single-sensor sequential homing, **no hand magnets** (replaces the earlier 2× DRV5032 Hall + magnets; see homing note above). **Libs:** SwitecX25 / GewoonGijs-VID28 / AccelStepper.

---

## 6. MCU + wireless (R7)

A **single MCU** must drive two stepper shafts, I²S audio, the NeoPixel chain + wake-LED PWM, the I²C sensor bus, microSD, and Wi-Fi + BLE — the **ESP32-S3 single-chip** wins on simplicity + library support (RMT for the SK6812s, MCPWM for the steppers, PCNT for the knob).

The N16R8 module exposes ~33 usable GPIO (octal PSRAM claims 3), and the pin budget is tight, so a **Microchip MCP23017** I²C IO expander (SOIC/SSOP-28, on the shared bus + one INT line) offloads the slow/static lines — amp mute, 12 V-boost enable, stepper STBY, the radio-disable toggle, and PD/charger status. The **knob lives on the host** (A/B → PCNT on IO47/48, press → IO17 with a hard interrupt + tight debounce — *not* the expander). The full pin-level allocation (host + expander) lives in [`esp32.md`](esp32.md) (native USB-JTAG on IO19/20); see also [`datasheet/README.md`](datasheet/README.md) §19.

| Option | Wi-Fi + BLE | Runs motors + audio + LEDs | Low power | Notes | Verdict |
|--------|-------------|----------------------------|-----------|-------|---------|
| **ESP32-S3** ⭐ single-chip | Wi-Fi 4 (2.4G) + BLE 5 | ✅ MCPWM steppers + I²S + RMT NeoPixels | good deep-sleep, higher idle | cheapest, fastest bring-up, huge lib support (§6c) | **Primary** |
| STM32U5 + ST67W611M1 | Wi-Fi 6 + BLE 5.4 (SPI) | ✅ | **excellent** | ST ULP | ULP alt |
| nRF5340 + nRF7002 | Wi-Fi 6 + BLE 5.x | ✅ | **best radio power** | Nordic tooling | Nordic ULP alt |
| NXP RW612 single-chip | Wi-Fi 6 dual-band + BLE 5.4 + Thread | ✅ | good | viable single-chip | single-chip alt |
| ESP32-P4 + C6 | Wi-Fi 6 + BLE (C6) | ✅ overkill | med | 2 chips; only if a display returns | future |

### 6c. ESP32-S3 quick-start libraries
Base: **ESP-IDF v5.x** (C, production, best power control) or **Arduino-ESP32 / PlatformIO** (fast prototyping).

| Requirement | Library / component |
|---|---|
| Analog steppers (§5) | **TB6612FNG** via GPIO/PWM; **AccelStepper** / **SwitecX25** / VID28 lib; microstep for silence |
| Status + dial NeoPixels (§9) | Espressif **`led_strip`** component (RMT backend, SK6812 RGBW timing) — one data GPIO for all 7; gamma + soft ramps in firmware |
| Knob (§12) | **PCNT** hardware quadrature (A/B, glitch filter) + GPIO ISR on ENC_SW with ~5 ms debounce |
| Tap-to-snooze (R3) | **LIS3DH** driver (Adafruit_LIS3DH / esp-idf-lib): hardware tap IRQ *(orientation use retired with R14)* |
| Wi-Fi | `esp_wifi` |
| BLE-pair → Wi-Fi provision (R9) | `wifi_provisioning` (BLE transport) + Espressif **"ESP BLE Provisioning"** phone app |
| BLE stack | **NimBLE** |
| NTP + TZ/DST (R1) | `esp_netif_sntp` + POSIX `TZ` string (`setenv`/`tzset`); run the S3 RTC off the **32.768 kHz XTAL32K** (GPIO15/16) so it holds ±~20 ppm between syncs — **no RTC chip, no battery** |
| Env sensors (R4) | Bosch **BSEC 2.x / BME68x** (chosen — T/RH/press/VOC/IAQ); Sensirion `embedded-i2c-scd4x / sps30` (CO₂/PM opt) |
| Light | **TSL2591** (Adafruit_TSL2591 / esp-idf-lib) or VEML7700 |
| Hand homing (§5) | **1× reflective optical** (QRE1113) → ADC/comparator edge; single-sensor sequential homing in firmware, no magnets |
| Audio out (I²S) | `esp_driver_i2s`; decode via **ESP-ADF** (MP3/AAC/FLAC/WAV) or Arduino **ESP32-audioI2S**; **~150 Hz high-pass + limiter/EQ biquads run here** (TAS5760M has no on-chip DSP) |
| Wake LEDs | `ledc` (PWM dimming, 2 ch tunable-white); 12 V COB via AO3400A — see [`led.md`](led.md) |
| SD + assets | `esp_vfs_fat` + `sdmmc`; `LittleFS` for internal flash |

**Fastest bring-up:** **ESPHome** gets sensors / SNTP / NeoPixels / PWM LED / I²S audio running in an afternoon; the steppers + knob-mode UX need a custom component or a drop to ESP-IDF.
**Caveat:** the **LT3652** charger is autonomous (no driver needed) — read charge state via **CHRG/FAULT GPIO + cell-voltage ADC** (no I²C charger/gauge); the health-cap 4.05 V is fixed in hardware by the float divider. See [`power.md`](power.md).

**RTOS/lang:** C/C++ + FreeRTOS (ESP-IDF). MicroPython for quick experiments only.

---

## 7. Audio subsystem (R3, R11)

- **Amp: TAS5760M** ⭐ (I²S in + I²C control, closed-loop Class-D, **HTSSOP-32 → hand-solderable**; digital clipper + DC-detect/OC/OT protection; up to 55 W stereo / 114 W mono PBTL). Run **PBTL mono** into the 4 Ω DMA58-4; **PVDD ~12 V boost → ~8–12 W** (or 5 V → ~3 W). Needs an **LC output filter**. **No on-chip biquad DSP** (unlike the superseded QFN TAS5825M) → the **~150 Hz high-pass + limiter/EQ that protects the 2 mm-Xmax driver runs in ESP32-S3 firmware**. *Analog alt (all hand-solderable):* **PCM5102A** DAC (TSSOP-20) → **TPA3116D2** Class-D (HTSSOP-32).
- **Chain:** ESP32-S3 → **I²S** (3 pins: BCLK, LRCLK, DOUT) → amp → speaker.
- **Source:** WAV (trivial) / FLAC (decoder + CPU) on microSD; system sounds in flash.
- **Tap-to-snooze:** accel hardware tap IRQ (§8) so the MCU sleeps until tapped.

### 7a. Sizing (what "close to a BT speaker" costs in space)
The cube helps audio — a ~120 mm cube has ample internal volume for the sealed chamber behind the dial/movement. Quality comes from **a sealed air chamber**, and the speaker fires through openings in the back face.

| Item | Spec | Footprint / volume |
|---|---|---|
| Driver | **2″ (Dayton DMA58-4), 4 Ω, 15 W, 86.2 dB** | 56 × 56 mm × ~32 mm deep |
| Bass helper | **DMA58-PR** passive radiator (optional, big low-end gain) | ~2″, ~20 mm deep |
| **Sealed chamber** | **~100–250 cc** sealed, or less w/ DMA58-PR passive radiator | dominant space cost → sets min body depth ~45 mm |
| Amp | **TAS5760M** HTSSOP-32 ~11×11 mm + LC filter | on main PCB |

**Loudness:** TAS5760M + DMA58-4 ≈ 90+ dB @ 0.5 m (12 V PVDD headroom) — plenty for an alarm, pleasant at desk distance. **Rule:** plan a body **≥45–60 mm deep** with a dedicated ~100–250 cc sealed chamber. Vent nothing from the chamber toward the AQ sensors.

### 7b. Speaker options — comparison

| Option | Type | Size (mm) | Power / Ω | Application | ~Cost | Pros | Cons |
|--------|------|-----------|-----------|-------------|-------|------|------|
| **Adafruit 3351** v1 | Mono, enclosed | 30×70×17 | 3 W / 4 Ω | alarm, voice, casual music | $4 | plug-and-play, tiny, JST, sealed | thin, ~no bass, modest SPL |
| Adafruit 1669 | Stereo enclosed set | 2× ~30×70 | 3 W / 4 Ω | casual stereo, chimes | $8 | stereo, enclosed | small, limited bass |
| **Dayton DMA58-4** ⭐ **chosen** | 2″ dual-magnet alu-cone full-range | 56 × 56 × 32 | 15 W / 4 Ω, 86.2 dB | quality desk audio | $19 | rigid alu cone, low distortion, matching DMA58-PR radiator | 2″ → limited bass w/o PR; needs small sealed box |
| Dayton PC68-4 (bigger-box alt) | 2.5″ full-range driver | Ø66 × ~30 | ~15 W / 4 Ω, 83 dB, Fs 117 Hz | more low-end, cheaper | $9 | more bass, great value | 2.5″ → deeper body (~250 cc) |
| Tang Band W2-series | 2″ full-range (premium) | Ø~57 | ~15 W / 4–8 Ω | premium music | $25–40 | best small-driver fidelity | pricey, needs box |
| Driver + passive radiator | 2.5″ FR + PR | Ø66 + PR | — | best "BT-speaker" feel | $15–25 | real bass in a small box | 2 parts + box tuning |
| 3″ full-range (Visaton FRS8) | 3″ driver | Ø80 | 20–30 W | if depth allows | $12–20 | fullest, loudest | biggest footprint |

**Chosen: Dayton DMA58-4** in a sealed **~100–250 cc** chamber (Fs 165.5 Hz → sealed roll-off ~200–260 Hz; add the matching **DMA58-PR** passive radiator for more low-end), driven by **TAS5760M** (I²S; ~150 Hz HPF + limiter in firmware). Body depth ≥45–60 mm follows from this. **Dayton PC68-4** (2.5″, lower Fs, more bass) is the bigger-box alt; **Adafruit 3351** the compact/budget fallback. *(Speaker is from Parts Express, not DigiKey.)*

---

## 8. Sensors (R3, R4)

| Function | Part | Notes | ~Cost |
|----------|------|-------|-------|
| Temp + humidity + VOC ⭐ | **Bosch BME688** | **one chip = T + RH + pressure + VOC/gas index** (BSEC lib) → the whole climate + air-quality job in a single sensor; ±0.5 °C / ±3 %RH (fine for a room readout) | $5 bare / ~$19 board |
| CO₂ *(opt)* | **Sensirion SCD41** | true NDIR CO₂ | $18–30 |
| PM2.5 *(opt)* | **Sensirion SPS30** | has fan; power/size | $30–45 |
| Ambient light ⭐ | **ams-OSRAM TSL2591** (`TSL25911FN`) | **188 µlx–88 klx**, 600M:1 range → resolves near-dark; VEML7700 = lux-direct simpler alt | $3 bare / ~$7 board |
| **Motion / tap** ⭐ | **ST LIS3DH** | tap engine for tap-to-snooze (R3); low-res is fine (tap IRQ). *(Orientation use retired with R14 — cube is fixed upright.)* No leaded accel exists → rides a module on both build paths | $2 bare / ~$5 board |
| Hand homing ⭐ | **onsemi QRE1113GR** (reflective optical, SMD) | 1 sensor, sequential homing, **no hand magnets** (§5); analog out → 1 ADC; ≈3.6×2.9×1.7 mm (replaces the unavailable ITR8307) | ~$0.36 |
| Timekeeping | **ESP32-S3 RTC + 32.768 kHz crystal** (no RTC IC) | **No coin cell.** Crystal (~±20 ppm ≈ 1.7 s/day) holds between **SNTP** syncs; total power loss (USB **and** 18650 gone) → re-sync on boot, clock animation meanwhile. A battery-less RTC IC would add parts without fixing the loss case → dropped | ~$0.3 |

**Every env/MEMS sensor is leadless (LGA/DFN) — no hand-solderable silicon exists**, so none sit bare on the board. Two build paths:
- **2a — chosen: STEMMA QT / Qwiic daisy-chain.** Three ready Adafruit boards on one 4-wire I²C chain — **BME688 (Adafruit 5046, ~$19) · TSL2591 (1980, $6.95) · LIS3DH (2809, $4.95)** ≈ **$31** — zero leadless soldering, fastest bring-up.
- **2b — future: one custom sensor daughterboard.** The **same** three bare chips (BME688 + TSL2591 + LIS3DH) on a small PCB, JLCPCB SMT-assembled; hand-solder only its 0.1″ header / castellated edge → smallest footprint, still no iron on a leadless pad. Same part numbers → same I²C addresses → **the 2a firmware runs unchanged**. Bare-chip datasheets in [`/datasheet`](datasheet/) (see its §15).

The 3-axis accel covers **tap-to-snooze** (hardware tap IRQ; orientation sensing retired with R14). All I²C on a shared bus (§14). **Vent the BME688 to outside air, away from amp/LEDs/battery** — their heat skews T/RH/VOC.

---

## 9. Lighting (R2, R3, R6)

Two subsystems on **two rails** (v0.19): the analog **wake light** (2 PWM channels, AO3400A low-side) and a **digital SK6812 RGBW NeoPixel chain** (1 data GPIO). Full spec in [`led.md`](led.md).

- **Wake light (2 LEDC ch):** **12 V** tunable-white COB (**3000K + 4000K**, Inspired LED) in a 100–120 mm sunrise diffuser behind the back/bottom aperture; warm→neutral ramp over ~30 min before audio; also serves as the night-light. **Plugged-only** (12 V boost is plugged-only; firmware gates the wake PWM off on battery). Warm = IO45, cool = IO46, each via an AO3400A.
- **NeoPixel chain (1 data pin, IO7 → RMT):** **7× SK6812 RGBW** (Adafruit 2758) **on the main PCB, 5 V rail (works on battery)**, one daisy-chained data line through an **SN74AHCT1G125** 3.3→5 V buffer: pixels **1–5 = status LEDs** behind the face holes (`bell` `alarm-clock` `clock` `volume-1` `battery` — red/white semantics per §12), pixels **6–7 = dial illumination** (warm-white W channel lights the walnut dial through the glass; ALS-gated, hard-off by default → 0 emission at night). One wire is ample for 7 pixels (a full 7×32-bit refresh @ 800 kHz ≈ 0.3 ms) — brightness + soft ramps are trivial in firmware; no CC driver, no per-LED resistors, just 100 nF per pixel + a bulk cap and a ~330 Ω series R into the first DIN.
- *(Dropped in v0.19: the 5 V discrete panel-LED string + its AO3400A/LEDC channel — the display it front-lit is gone; dial lighting moved to NeoPixels 6–7.)*

---

## 10. Power (R5)

> Full power tree, budget, and battery-safety detail in [`power.md`](power.md). Summary below (kept in sync).

- **Input:** USB-C with **USB-PD sink** (**CH224K**, ESSOP-10, resistor-set request 15 V; graceful fallback 5 V).
- **Charge / path:** **LT3652** (MSOP-12E 1-cell **buck** charger, VIN ≤32 V, resistor-set **4.05 V** float + charge current, **NTC** temp-qualified charging, safety timer). The **BAT node is the always-on system supply** → runs plugged with **no cell** (LT3652 sources up to 2 A). *No I²C — autonomous; state via CHRG/FAULT + ADC.*
- **Fuel gauge:** **ESP32-S3 ADC** on a cell divider (no hand-solderable gauge IC exists) → voltage-based SoC, low-batt shutdown ~3.2 V.
- **Battery:** **user-supplied 18650 in a holder, Li-ion ONLY** (labeled "Li-ion 18650 only · 2.5–4.2 V"). Recommend a protected 3000–3500 mAh cell. **48 h backup** target.
- **Health:** charge-cap **~80 % (4.05 V)** — fixed in hardware by the LT3652 float divider; optional GPIO-switched divider for a 4.2 V "full" mode.
- **Rails:** **3.3 V** (MCU, from 5 V) · **5 V** (stepper VM + **7× SK6812 NeoPixels** + amp-PVDD mux aux) · **12 V boost** (audio PVDD **+ wake LEDs**, **plugged-only**) · **15 V VBUS** (LT3652 charger VIN, plugged). Audio PVDD auto-muxes 12 V (plugged) / 5 V (battery) via an **LTC4412** → quieter alarm on battery. All rail converters in **leaded** packages (see §16 / `power.md`).
- **Safety (mandatory, wood/bedroom, any 18650) — simple + double-redundant:** independent **cell protector** (**HY2111-GB** SOT-23-6 + **AOSD32334C** dual-N SO-8 FET: OV/OD/OC/SC) redundant to the charger, **reverse-polarity P-FET**, **NTC** temp-qualified charge, **one-shot ~77 °C TCO** (thermal fuse in the cell − path), TVS, insert-qualify, FR barrier + venting. Double-redundant overcharge (charger 4.05 V + protector **4.28 V**), layered over-discharge. *(⚠ vs the superseded BQ25628E: no I²C telemetry + single-window NTC, not multi-zone JEITA — still meets "no charge <0/>45 °C". Secondary OVP/PPTC still dropped.)* Full wiring/config in [`power.md`](power.md).

---

## 11. Storage (R9, R11)

- **microSD** (SPI mode, 4-wire — saves GPIO vs SDMMC; sole device on SPI2 since v0.19) — user alarm tones / audio assets.
- **Flash** (module 16 MB + optional external OSPI NOR) — system sounds, OTA, config.

---

## 12. Inputs — single knob UX (R8)

**One aluminum knob, top-center** (HiFi feel): Kilo **OEJNI-90-1-5** machined aluminum knob (Ø23.5 × 15.9 mm, clear gloss, 1/4″ bore + 6-32 set screw) on a Bourns **EM14A0D-C24-L064S** **optical** encoder — **no detent** (perfectly smooth, no mechanical contacts to wear or bounce), **64 CPR** quadrature ×4 on PCNT = **256 counts/rev** (plenty sensitive; two channels are fully sufficient, incl. direction — sensitivity is just a firmware counts-per-step mapping), integral momentary **push switch**, 1/4″ flatted shaft matching the knob bore. The encoder's opto-ASIC runs on **5 V** (~30 mA), so its A/B outputs are 5 V logic → **100k/200k dividers (~3.2 V) → IO47/48 (PCNT, glitch-filtered)**; the push switch is a dry contact to GND → **IO17** (host GPIO interrupt, 10 k pull-up + 100 nF + ~5 ms firmware debounce — deliberately *not* on the I²C expander). Off-board on **J10** (JST **ZH** 1×06 B6B-ZR: GND · +5V · A · B · SW · GND — same family and pre-crimped A06ZR cable as the sensor board J7; the rear toggle J11 is a ZH 1×02).

**Mode cycle — press steps through the 5 status LEDs (§9), rotate edits the lit mode:**

| # | LED / icon | Press → LED on; rotate → | Hands show |
|---|---|---|---|
| 1 | `bell` — **alarm** | toggle armed (LED **red**) ↔ disarmed (LED **white**) | the **alarm time** while in this mode |
| 2 | `alarm-clock` — **set alarm** | move the alarm time (hands follow live) | alarm time being set |
| 3 | `clock` — **set clock** | move the clock time (hands follow live) | time being set |
| 4 | `volume-1` — **volume** | volume up/down while a pleasant sample plays | current time |
| 5 | `battery` — **status only, not selectable**: lights on low battery (skipped in the cycle) | — | current time |

- Press after *volume* (or a 5th press) → all LEDs off, settings committed, hands return to the time.
- **Timeout:** ≥5 s without rotation → exit to normal (LEDs off, hands back to time).
- **Radio-disable toggle** (rear, on **J11**, expander GPA3): hardware switch to shut down Wi-Fi/BLE (bedroom EMI preference); firmware obeys it as a hard override.
- **Tap-to-snooze** via accel (top-tap) — no other buttons anywhere.

---

## 13. Companion app (R9)

BLE pair → GATT provisioning → push Wi-Fi creds → join AP. Configure alarms, sunrise, LED brightness/color, TZ. Upload tones (→ SD) over BLE or Wi-Fi. iOS app and/or web (Web Bluetooth). ESP path: reuse Espressif's provisioning app for v1. QR code for quick setup? *(Note the rear radio toggle, §12 — with radios off, on-device knob config still works.)*

---

## 14. Extensibility (R10)

Shared **I²C** (Qwiic/STEMMA-QT) for drop-in sensors; the NeoPixel chain extends by just soldering more pixels (same one data line); speaker, LED strips, knob and toggle on JST connectors; reusable compute+radio+power core block. *(A display could return in a future variant — SPI2 has a free CS and the Sharp-panel datasheets are retained.)*

---

## 15. PCB considerations

4-layer min (6 if dense); solid ground plane, RF keep-out (or use a module w/ onboard antenna to inherit certs). **Two stepper shafts** via 2× TB6612FNG — keep coil traces + PWM away from RF/audio; mount **one reflective optical home sensor (QRE1113)** behind the dial, aligned to a punched index hole (shared by both hands via sequential homing). **NeoPixel placement is mechanical:** the 5 status SK6812s sit in a row whose pitch must match the face-plate holes exactly (fix plate + PCB geometry together), the 2 dial-illumination pixels flank the movement; 100 nF at each pixel, bulk 100 µF at the chain head, ~330 Ω into DIN, level shifter close to the first pixel. **Vertical USB-C (USB4160)** stands at the board's back edge, mating axis out the back wall — the 4 shell stakes solder into plated slots for retention; keep the pegs clear of the pour. Class-D + speaker return away from RF/analog. Copper for charger/amp heat. JST for battery/speaker/LED strips/knob/toggle, SD cage, USB-C w/ ESD+CC. **Hand-solder rules:** leaded ICs only (no QFN/DFN/BGA/LGA); **≥0603 passives**; the PowerPAD amp + charger get a **thermal-via array** under the belly pad; leadless-only sensors ride on I²C **breakout modules**; use generous footprints/spacing — board area is traded for hand-assembly.

---

## 16. Shopping list (DigiKey)

> DigiKey MPN / part links — click to verify live stock/price. Prices approximate, single-unit. Wood/enclosure excluded. Parts marked ✅ verified active on DigiKey; others use a DigiKey keyword search — confirm the exact orderable variant. Core-part datasheets + a summary table live in [`/datasheet`](datasheet/).

### 16a. Prototyping kit (fast start — dev boards + breakouts)

| Item | MPN | ~Price | Source |
|------|-----|--------|--------|
| SoC dev board (16 MB / 8 MB PSRAM) | ESP32-S3-DevKitC-1-N16R8 | ~$18 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=ESP32-S3-DevKitC-1-N16R8) |
| Analog movement (dual shaft) | Juken X40.879 | ~$14 | [DigiKey 28528329 ✅](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) |
| NeoPixels RGBW (status ×5 + dial ×2) | Adafruit 2758 (SK6812 RGBW 5050, 10-pack) | $5.95 | [DigiKey 6134706 ✅](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2758/6134706) |
| Knob encoder (smooth optical, push) | Bourns EM14A0D-C24-L064S (no detent, 64 CPR, 5 V) | $34.00 | [DigiKey 954403 ✅](https://www.digikey.com/en/products/detail/bourns-inc/EM14A0D-C24-L064S/954403) |
| Aluminum knob (1/4″ bore) | Kilo OEJNI-90-1-5 (Ø23.5 mm, set screw) | $13.43 | [DigiKey 5970396 ✅](https://www.digikey.com/en/products/detail/kilo-international/OEJNI-90-1-5/5970396) |
| Motor driver carrier (×2) | SparkFun TB6612FNG (ROB-14451) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=TB6612FNG%20carrier) |
| Hand homing (×1, optical) | onsemi QRE1113GR (reflective, analog out, SMD) | ~$0.36 | [DigiKey 965451 ✅](https://www.digikey.com/en/products/detail/onsemi/QRE1113GR/965451) |
| I²S amp (bring-up) | Adafruit 3006 (MAX98357A) → TAS5760M EVM | ~$6 / — | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%203006) |
| Speaker (2″ full-range) | Dayton DMA58-4 | ~$19 | *(Parts Express 295-582 — not DigiKey)* |
| Env breakout (T/RH/press/VOC) ⭐ | Adafruit 5046 (BME688) | ~$19 | [DigiKey 14313482](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5046/14313482) |
| Light breakout (weak-light) ⭐ | Adafruit 1980 (TSL2591) | $6.95 | [DigiKey 4990786](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1980/4990786) |
| Accel breakout (tap+orient) ⭐ | Adafruit 2809 (LIS3DH) | $4.95 | [DigiKey 5774319](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2809/5774319) |
| Wake COB (12 V, 2 ch) | Inspired LED 12V-COB-3000K-12M + 12V-COB-4000K-12M | ~$15 (cut segs) | [16714316](https://www.digikey.com/en/products/detail/inspired-led-llc/12V-COB-3000K-12M/16714316) · [16714317](https://www.digikey.com/en/products/detail/inspired-led-llc/12V-COB-4000K-12M/16714317) |

### 16b. Production BOM (ICs, custom PCB)

> **Status key:** ⭐ **selected** — datasheet on file in [`/datasheet`](datasheet/) · ⚠️ **needed, not yet locked** — exact part/verification/datasheet still to do · ➕ **improvement** — optional upgrade, not required for core function. *(Every ⭐ row has a matching datasheet — the v0.19 additions were filed 2026-07-20 as `datasheet/` rows 30–34 (USB4160, SK6812 RGBW, SN74AHCT1G125, EM14, JST ZH); the **OEJNI knob is mechanical-only** (no datasheet, dims in its row). The dropped display/FPC/USB4105 datasheets **remain on file** for future reuse, marked "not in current build".)*

| Status | Block | MPN | Size / pkg | ~Unit | DigiKey ref |
|:--:|-------|-----|-----------|-------|-------------|
| ⭐ | SoC module | ESP32-S3-WROOM-1-N16R8 | 18×25.5 mm | ~$6.8 | [DigiKey ✅](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) |
| ⭐ | IO expander (I²C, 16-bit) | **Microchip MCP23017** (SPI twin MCP23S17) — offloads SPK_SD / 12 V-EN / stepper STBY / radio-toggle / PD-PG / CHRG / FAULT / FULLCHG_EN / VBAT_DIV_EN / SPK_FAULT; INT→1 GPIO (net −9) | **SOIC-28 W / SSOP-28** | ~$1.7 | [DigiKey ✅](https://www.digikey.com/en/products/detail/microchip-technology/MCP23017T-E-SO/964184) |
| ⭐ | Status + dial NeoPixels ×7 (5 status + 2 dial) | **SK6812 RGBW 5050** — Adafruit **2758** 10-pack; on-PCB, 5 V, one data line | PLCC-4 5.0×5.0 mm | $5.95 /10 | [DigiKey ✅](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2758/6134706) |
| ⭐ | NeoPixel data level shifter | **TI SN74AHCT1G125DBVR** (3.3 V → 5 V buffer, single gate) | **SOT-23-5** | ~$0.14 | [DigiKey ✅](https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT1G125DBVR/376028) |
| ⭐ | Knob encoder (top face, off-board) | **Bourns EM14A0D-C24-L064S** — **optical**, **no detent**, 64 CPR (×4 → 256/rev), integral push switch, 1/4″ flatted shaft, **5 V ~30 mA**; A/B 5 V out → 100k/200k dividers | vertical TH, PC pins | $34.00 | [DigiKey ✅](https://www.digikey.com/en/products/detail/bourns-inc/EM14A0D-C24-L064S/954403) |
| ⭐ | Aluminum knob | **Kilo International OEJNI-90-1-5** — machined, Ø23.5 × 15.9 mm, clear gloss, **1/4″ bore + 6-32 set screw** (mates the EM14 shaft) | solid aluminum | $13.43 | [DigiKey ✅](https://www.digikey.com/en/products/detail/kilo-international/OEJNI-90-1-5/5970396) |
| ⭐ | Sensor-board connector (J7, off-board I²C + power: GND/3V3/SDA/SCL/INT + 1 rsvd) | **JST B6B-ZR(LF)(SN)** header ×2 (one per board, TH vertical, shrouded/keyed, 1 A) + **A06ZR06ZR28H102B** pre-crimped cable ×1 (ZH, 6-pos, socket↔socket, 28 AWG, 102 mm) — **same ZH family + cable as the knob J10** | **TH, 6-pos, 1.5 mm** | ~$0.24 ×2 + ~$1.54 | [DigiKey ✅](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B6B-ZR(LF)(SN)/455-1661-ND/926568) [DigiKey ✅](https://www.digikey.com/en/products/result?keywords=A06ZR06ZR28H102B) |
| ⭐ | Analog movement | Juken X40.879 (dual shaft) — **solders directly to the PCB** (custom footprint `clock.pretty`, shafts through a board hole; no connector) | vertical, compact | ~$20 | [MiniTools ✅](https://store.minitools.com/en/sei-x40-879-juken-x40-879-stepper-motor.html) |
| ⭐ | Motor driver ×2 | TB6612FNG,C,8,EL | **SSOP-24** | ~$2.1 x 2 | [DigiKey ✅](https://www.digikey.com/en/products/detail/toshiba-semiconductor-and-storage/TB6612FNG-C-8-EL/1730070) |
| ⭐ | Hand homing ×1 (optical, reflective — **no magnets**) | onsemi QRE1113 (single sensor, sequential homing; analog out → 1 ADC; replaces the unavailable ITR8307) | **≈3.6×2.9×1.7 mm**, 4-lead (TH; SMD `QRE1113GR` alt) | ~$0.8 | [DigiKey ✅](https://www.digikey.com/en/products/detail/onsemi/QRE1113/2175990) |
| ⭐ | Audio amp | TAS5760MDAPR | **HTSSOP-32** | ~$6.6 | [DigiKey ✅](https://www.digikey.com/en/products/result?keywords=TAS5760MDAPR) |
| ⭐ | Env: T/RH/press/VOC *(I²C module)* | Bosch BME688 (one chip = climate + air-quality); 2a board = Adafruit 5046 | LGA-8 → module | ~$5 / $19 | [PENDING](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5046/14313482) |
| ⭐ | Light *(I²C module)* | ams-OSRAM TSL2591 / TSL25911FN (188 µlx–88 klx); 2a board = Adafruit 1980 | WFDFN-6 → module | ~$5 / $7 | [PENDING](https://www.digikey.com/en/products/detail/ams-osram-usa-inc/TSL25911FN/4162547) |
| ⭐ | Accel (tap+orient) *(I²C module)* | ST LIS3DH; 2a board = Adafruit 2809 | LGA-16 → module | ~$2 / $5 | [PENDING](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2809/5774319) |
| ⭐ | Timekeeping xtal (no RTC IC/battery) | **Abracon ABS07-32.768KHZ-T** (32.768 kHz, **CL 12.5 pF**, ±20 ppm) → S3 XTAL32K (GPIO15/16); match load caps to CL | 3.2×1.5 mm 2-SMD | ~$0.6 | [DigiKey ✅](https://www.digikey.com/en/products/detail/abracon-llc/ABS07-32-768KHZ-T/1236858) |
| ⭐ | PD sink | CH224K | **ESSOP-10** | ~$0.5 | [LCSC ✅](https://www.lcsc.com/product-detail/C970725.html) |
| ⭐ | Charger (1-cell buck, BAT-node path) | LT3652EMSE#PBF | **MSOP-12E** | ~$9.9 | [DigiKey ✅](https://www.digikey.com/en/products/detail/analog-devices-inc/LT3652EMSE-PBF/2225686) |
| ⭐ | Cell protector (independent) | **HYCON HY2111-GB** (OV 4.28 V / OD 2.90 V) + AOSD32334C dual-N FET | **SOT-23-6 + SO-8** | ~$0.1 + ~$0.8 | [LCSC ✅](https://www.lcsc.com/product-detail/Battery-Management-ICs_HYCON-Tech-HY2111-GB_C82747.html) [DigiKey ✅](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AOSD32334C/11567511) |
| ⭐ | Reverse-polarity | **AO3401A** P-FET (−30 V, −4 A, R<sub>DS</sub>~50 mΩ) on BAT+ | **SOT-23-3** | ~$0.24 | [DigiKey ✅](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO3401A/1855773) |
| ⭐ | Cell temp sense | **Murata NCP18XH103F03RB** 10 k NTC (B25/50 = 3380 K, ±1 %) → LT3652 NTC pin | **0603** | ~$0.20 | [DigiKey ✅](https://www.digikey.com/en/products/detail/murata-electronics/NCP18XH103F03RB/1644665) |
| ⭐ | Transient | **Littelfuse SMAJ22CA** (VBUS) + **SMAJ5.0CA** (BAT) — 400 W **bidirectional** *(switched from the uni A-suffix 2026-07-21: the schematic's D_TVS symbol is bidirectional, so the CA parts remove the assembly-orientation risk; $0.48 ea, in stock)* | **DO-214AC (SMA)** | ~$0.5 x 2 | [DigiKey ✅](https://www.digikey.com/en/products/detail/littelfuse-inc/SMAJ22CA/762287) [DigiKey ✅](https://www.digikey.com/en/products/detail/littelfuse-inc/SMAJ5-0CA/762251) |
| ⭐ | USB D± ESD array *(added 2026-07-21)* | **ST USBLC6-2SC6** flow-through on D+/D− at J1 (VBUS pin **NC** — 15 V PD contract exceeds its 5.25 V rating; internal zener still clamps) | **SOT-23-6** | ~$0.36 | [DigiKey ✅](https://www.digikey.com/en/products/detail/stmicroelectronics/USBLC6-2SC6/1121688) |
| ⭐ | VBAT_SENSE clamp *(added 2026-07-21)* | **Vishay BAT42W-E3-08** Schottky, cathode on the ADC divider node — bounds a reversed-cell negative excursion to ~−0.2 V at the ESP32 pad | **SOD-123** | ~$0.47 | [DigiKey ✅](https://www.digikey.com/en/products/detail/vishay-general-semiconductor-diodes-division/BAT42W-E3-08/3104176) |
| ⭐ | Cell one-shot TCO (77 °C) | **Cantherm SDF-DF077S** non-resettable organic-element thermal cut-off in the cell − path (T<sub>f</sub> 77 °C / hold 55 °C, 10 A/250 VAC); solder ≥3 mm from body + heatsink lead | axial radial-lead (Ø4.0, AWG18) | ~$0.9 | [DigiKey ✅](https://www.digikey.com/en/products/detail/cantherm/SDF-DF077S/1014754) |
| ⭐ | Audio + wake-LED boost BAT→12 V (plugged-only) | **TI TPS55340PWPR** (5 A/40 V boost) | **HTSSOP-14 (PWP, PowerPAD)** | ~$3.1 | [DigiKey ✅](https://www.digikey.com/en/products/detail/texas-instruments/TPS55340PWPR/3727185) |
| ⭐ | 5 V rail (boost) | **TI TPS61023DRLR** (3.7 A, 0.5–5.5 V in) — DigiKey-stocked vs off-DK MT3608 | **SOT-563** | ~$1.3 | [DigiKey ✅](https://www.digikey.com/en/products/detail/texas-instruments/TPS61023DRLR/11310667) |
| ⭐ | 3.3 V rail (from 5 V) | **TI TLV62569DBVR** buck (2 A, 2.5–5.5 V in) | **SOT-23-6** | ~$0.3 | [DigiKey ✅](https://www.digikey.com/en/products/detail/texas-instruments/TLV62569DBVR/7688370) |
| ⭐ | LED low-side switches ×2 | **AO3400A** N-FET (+ 100 Ω gate, 10 kΩ pulldown) — 2× wake COB *(3rd/panel FET recovered in v0.19)* | **SOT-23** | ~$0.5 x 2 | [DigiKey ✅](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO3400A/1855772) |
| ⭐ | 18650 holder | **Keystone 1043** (TH PC-pin, UL94V-0, leaf springs; **$2.99, 21.9 k in stock** — chosen over the pricier SMT 1042, whose stale footprint the schematic carried until 2026-07-20; custom 1043 footprint now in `kicad/clock.pretty`) — ventilated placement per `power.md` | TH leaf-spring | $2.99 | [DigiKey ✅](https://www.digikey.com/en/products/detail/keystone-electronics/1043/2745669) |
| ⭐ | Wake LEDs (12 V COB, plugged-only) | Inspired LED **12V-COB-3000K-12M** (warm) + **12V-COB-4000K-12M** (neutral) — self-ballasted, $0.64/0.98″ seg | strip | ~$2.6 x 2 | [DigiKey ✅](https://www.digikey.com/en/products/detail/inspired-led-llc/12V-COB-3000K-12M/16714316) · [DigiKey ✅](https://www.digikey.com/en/products/detail/inspired-led-llc/12V-COB-4000K-12M/16714317) |
| ⭐ | Amp PVDD rail-mux (12 V↔5 V) | **LTC4412** low-loss PowerPath + P-FET — 12 V (plugged) / 5 V (battery) | **SOT-23-6** | ~$5.2 | [DigiKey ✅](https://www.digikey.com/en/products/detail/analog-devices-inc/LTC4412ES6-TRPBF/960173) |
| ⭐ | microSD socket | **Hirose DM3AT-SF-PEJM5** (push-push, 8-pos) | push-push, SMT R/A | ~$2.9 | [DigiKey ✅](https://www.digikey.com/en/products/detail/hirose-electric-co-ltd/DM3AT-SF-PEJM5/2533565) |
| ⭐ | USB-C recept (**vertical**, power+CC+D±) | **GCT USB4160-03-0230-C** (USB 3.2 Gen2 Type-C, 24-pin, **vertical SMT + 4 soldered stakes**, H 7.46 mm — stands on the PCB → port exits the back wall; only the USB 2.0 subset wired, SS pads unconnected). *Replaces the horizontal USB4105 + Adafruit 6069 extension; rejected along the way: USB4140 (no D±), UJ20-C-V-C-1 (0.8 mm-PCB legs), UJ20-C-V-C-2 (backorder); USB4145-03-0170-C = 16-pin same-land alt.* | SMT vertical, 0.5 mm pitch + stake slots | ~$1.22 (5.7 k stock) | [DigiKey ✅](https://www.digikey.com/en/products/result?keywords=USB4160-03-0230-C) |
| ⭐ | Knob connector (J10) | **JST B6B-ZR(LF)(SN)** (ZH 1×06: GND·+5V·A·B·SW·GND — the EM14 needs 5 V) + the same pre-crimped **A06ZR06ZR28H102B** ZH↔ZH cable as the sensor board | ZH 1.5 mm, TH vertical | ~$0.24 | [DigiKey ✅](https://www.digikey.com/en/products/detail/jst-sales-america-inc/B6B-ZR-LF-SN/926568) |
| ⭐ | Radio-toggle connector (J11) | **JST B2B-ZR(LF)(SN)** (ZH 1×02: SIG·GND) → rear toggle switch; pre-crimped A02ZR-style cable | ZH 1.5 mm, TH vertical | ~$0.2 | [DigiKey ✅](https://www.digikey.com/en/products/result?keywords=B2B-ZR%28LF%29%28SN%29) |
| ⭐ | Speaker driver | Dayton DMA58-4 (2″ FR) | 56×56×32 mm | ~$19 | [PartsExpress ✅](https://www.parts-express.com/Dayton-Audio-DMA58-4-2-Dual-Magnet-Aluminum-Cone-Full-Range-Driver-4-Ohm-295-582?quantity=1) |
| ➕ | CO₂ *(improvement)* | Sensirion SCD41-D-R2 | — | ~$24 | [search](https://www.digikey.com/en/products/result?keywords=SCD41-D-R2) |
| ➕ | PM2.5 *(improvement)* | Sensirion SPS30 | — | ~$38 | [search](https://www.digikey.com/en/products/result?keywords=SPS30) |
| ➕ | Passive radiator *(improvement)* | Dayton DMA58-PR (2″) | — | ~$8 | *(Parts Express)* |

**Core electronics subtotal (excl. speaker/cell/PCB): ~$140–165** — the v0.19 redesign removes ~$47 of display lines (LS032B7DD02 ~$38 + FPC + panel extension + Cree string) and adds ~$55 of UI parts (EM14 optical encoder **$34** + Kilo knob **$13.43** + NeoPixel 10-pack $5.95 + buffer + SH connectors) → roughly a wash vs v0.18; power electronics ≈ +$20–22 (LT3652 is the priciest line); the 2a sensor chain is 3 Adafruit **breakout modules** — BME688 + TSL2591 + LIS3DH ≈ **$31**. With speaker + user-supplied 18650 + holder + 4-layer PCB + passives ≈ **~$210–250**. +CO₂/PM ≈ +$62. *(Cell is user-supplied; safety HW is non-negotiable — see [`power.md`](power.md).)*

**Cost/space levers:** budget movement (VID28-05, −$10, off-DigiKey) + optical homing; a mechanical encoder (PEC11R-4015F, −$31) if the optical EM14 feels extravagant; skip CO₂/PM; the EM14 + movement are now the big electronics line items — the enclosure (walnut + machined aluminum plate + knob + glass) dominates overall cost instead. *(Amp DSP is free — it runs in firmware.)*

---

## 17. Alternatives quick-reference

- **MCU:** ESP32-S3 ⭐ single-chip / STM32U5 + ST67W611M1 (ULP) / nRF5340 + nRF7002 (Nordic ULP) / RW612 (single-chip) / ESP32-P4 + C6 (future MIPI).
- **Info display:** **none (v0.19)** ⭐ — hands + 5 status NeoPixels. *Superseded: Sharp LS032B7DD02 reflective MIP (v0.6–v0.18, datasheets retained) / mono OLED / IPS TFT / EPD / wide color bar-TFT (NHD-3.9).*
- **Status/dial LEDs:** **SK6812 RGBW 5050 ×7** ⭐ (one RMT data line, 5 V) / discrete LEDs + PWM (more pins, no color semantics — the v0.18 panel-string approach, dropped).
- **Knob encoder:** Bourns **EM14A0D-C24-L064S** ⭐ (optical, no detent, 64 CPR, push, 5 V — chosen for the smoothest contactless feel) / Bourns PEC11R-4015F (mechanical no-detent, 24 PPR, $2.68 budget alt) / Alps EC11 (detented) / magnetic (AS5600, overkill). Knob: Kilo **OEJNI-90-1-5** ⭐ (Ø23.5 mm alu, 1/4″ bore).
- **Analog movement:** Juken **X40.879** (DigiKey, now Ø~90 mm dial) ⭐ / Juken X10.506 (small, built-in homing) / VID28-05·BKA30D-R5 (budget, off-DigiKey) / X27.168 ×2 (single-shaft).
- **Motor driver:** **2× TB6612FNG** ⭐ (SSOP-24, hand-solderable). *(DRV8835/DRV8833 = WSON/HTSSOP, replaced.)*
- **Amp:** **TAS5760M** ⭐ (I²S+I²C, HTSSOP, PBTL mono; firmware DSP) / PCM5102A + TPA3116 (analog).
- **Power:** **CH224K** (PD sink) + **LT3652** (1-cell buck charger, BAT-node path) + **ESP32 ADC** gauge + **HY2111 + AOSD32334C** protector + reverse P-FET; 1S Li-ion 18650 holder. *(All leaded/hand-solderable; CH224K + HY2111 off-DigiKey.)*
- **Timekeeping (no RTC IC / no battery):** ESP32-S3 internal RTC off a **32.768 kHz crystal** (~±20 ppm, GPIO15/16) + periodic **SNTP** — drift ≈ 1–2 s/day between syncs, corrected each sync. On total power loss (USB **and** 18650 gone) time is lost → re-sync on next boot, clock animation meanwhile. *A battery-less RTC IC adds parts without fixing the loss case, so it's dropped; the crystal is the real accuracy fix.*
- **Sensors:** env **BME688** ⭐ (T/RH/press/VOC in one chip) / light **TSL2591** ⭐ (weak-light) or VEML7700 (lux-direct) / accel+orient **LIS3DH** ⭐. All leadless → **I²C modules, same 3-part set on both paths** (firmware identical): **2a** = Adafruit STEMMA QT chain (5046 + 1980 + 2809, ≈$31); **2b** = future custom daughterboard (same bare chips). *(Dropped TMP117/SHT45/SGP41/BMA400.)*
- **Hand homing:** **1× reflective optical** (onsemi **QRE1113** ⭐, **≈3.6×2.9×1.7 mm**, analog out → 1 ADC) behind a punched dial hole — single-sensor sequential, **no magnets**; replaces the unavailable **ITR8307** (same interface + support network); Vishay **TCND5000** (6×4.3×3.75 mm) evaluated for a long stand-off and dropped; ToF (VL53L1x) rejected (coarse angle). *(Replaces 2× DRV5032 Hall.)*
- **Display connector:** *(dropped with the display, v0.19; FH34SRJ datasheet retained.)*
- **USB-C receptacle:** **GCT USB4160-03-0230-C** ⭐ (vertical 24-pin USB 3.2, USB 2.0 subset wired, $1.22, in stock) / GCT USB4145-03-0170-C (16-pin same-land alt) / Same Sky UJ20-C-V-C-2 (TH pins, backorder) / USB4140 & UJ20-C-V-C-1 (rejected: power-only resp. 0.8 mm-PCB legs) / USB4105-GF-A + Adafruit 6069 extension (the superseded horizontal + panel-remote combo).
- **Speaker:** Dayton DMA58-4 ⭐ (2″) / Dayton PC68-4 (2.5″, more bass) / Adafruit 3351 (budget).

---

### Decision log
| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-30 | ~~EPD primary~~ (superseded) | Fails ticking seconds (ghost/wear) |
| 2026-07-02 | ~~Fast bar TFT (NHD-3.9) primary~~ (superseded 07-03) | Replaced by the split-face reflective panel |
| 2026-07-02 | **ESP32-S3 as the MCU** (STM32U5+ST67W as ULP alt) | Single chip drives display + motors + Wi-Fi + BLE; fastest bring-up; huge lib support |
| 2026-07-02 | Body depth ≥60–80 mm w/ sealed ~250 cc chamber | Audio quality needs volume, not display area |
| 2026-07-02 | Premium finish is a spec item | Flush cover glass + printed border + restrained type |
| 2026-07-02 | Split-face (analog dial + reflective panel) added as premium variant | Real hands + reflective panel look less cheap than RGB; silent; dark at night |
| 2026-07-02 | Speaker locked: **Dayton PC68-4** (2.5″ FR) in sealed chamber | Real driver quality; needs ~250–400 cc + ≥60–80 mm depth |
| 2026-07-02 | Added **R12** (MCU sets/adjusts analog time) | Requires bidirectional addressable movement + homing |
| 2026-07-03 | **Split-face locked as THE design** (circular analog + reflective mono panel) | Refined product direction; premium, silent, dark-at-night |
| 2026-07-03 | **Display = reflective Sharp LS032B7DD02**; bar-TFT superseded | 0 emission + daylight-readable; removes the whole backlight subsystem |
| 2026-07-03 | **ESP32-S3 confirmed as the single MCU**; wide-display size question dropped | RGB/LTDC no longer needed (SPI panel) → single-chip simplicity |
| 2026-07-03 | **Orientation awareness (R14)**: flat↔standing, accel-sensed; MCU rotates display + analog "12" | Numeral-free dial + absolute stepper addressing make it orientation-agnostic |
| 2026-07-03 | 75 mm dial → movement = **Juken X40.879** (DigiKey) + Hall homing | X10.506 too low-torque for 75 mm; X40.879 is dual-shaft and DigiKey-stocked |
| 2026-07-03 | Motor driver **DRV8833 → DRV8835** | DRV8833 marked NRND on DigiKey; DRV8835 is active |
| 2026-07-03 | Shopping list moved **Mouser → DigiKey** | Sourcing preference; parts verified active |
| 2026-07-04 | Speaker changed **PC68-4 → Dayton DMA58-4** (2″) | The driver we hold a datasheet for; smaller → body depth relaxed to ≥45–60 mm / ~100–250 cc chamber (add DMA58-PR for bass). PC68-4 kept as bigger-box alt |
| 2026-07-04 | **X40.879 confirmed** as the movement; **X27 = its companion datasheet** | X40 datasheet is a pinout addendum (two independent dual-shaft coils, X27-compatible mechanics) that defers to the X27 base spec; both filed in `/datasheet` |
| 2026-07-04 | Corrected display specs vs the full Sharp device spec | Active area **42.67 × 68.07 mm**; power **30 µW hold / 250 µW update** @ **5 V (VDD 4.8–5.5 V)**, 3 V logic; replaced the marketing brief with `LD-2023X13` |
| 2026-07-04 | **X40.879 price ~$25 → ~$14** | Live DigiKey pricing (28528329) |
| 2026-07-04 | **Amp locked = TAS5825M** (MAX98357A → simple alt) | DSP high-pass/limiter protects the 2 mm-Xmax DMA58-4 and makes a 2″ driver sound full; PBTL mono, 5 V→~3 W or +12 V boost→~8–12 W; full datasheet on file |
| 2026-07-04 | **Driver confirmed = 2× DRV8835** | High-impedance (~260 Ω) gauge coils are voltage-driven, not chopper-driven; DRV8835 gives full 5 V torque + flyback; avoid A4988/DRV8825/TMC choppers; full datasheet on file |
| 2026-07-04 | **Power: USB-PD (15 V) + BQ25792 buck-boost** (was BQ25185) | PD gives headroom for loud audio + sunrise + fast charge; buck-boost accepts 3.6–24 V + firmware health-cap; supersedes nPM1300/BQ25185 |
| 2026-07-04 | **Battery = user-supplied 18650, Li-ion ONLY, labeled** | Replaceable; runs with no cell on USB; must be safe for any inserted 18650 |
| 2026-07-04 | **Battery safety non-negotiable** (board-level) | Wood/bedroom: must be safe for any inserted 18650; don't cost-trim safety |
| 2026-07-04 | **Power/safety simplified → Option A** (BQ25792→**BQ25628E** buck; **LC05111** protector + reverse P-FET; drop secondary OVP/TCO/PPTC) | 1S input always > cell → buck suffices; charger + one integrated protector = double-redundant (phone/wearable standard), not laptop-pack triple. Simpler, safe, ~$8 cheaper |
| 2026-07-04 | **Hand-solderable-only mfg constraint** (hard) | Builder hand-solders every part; no equipment for QFN/DFN/BGA/LGA. Board area traded for assembly |
| 2026-07-04 | **Amp TAS5825M (VQFN) → TAS5760M (HTSSOP-32)** | Keeps I²S+I²C + output protection in a leaded pkg; HPF/limiter DSP moves to ESP32 firmware (no on-chip biquads) |
| 2026-07-04 | **Driver 2× DRV8835 (WSON) → 2× TB6612FNG (SSOP-24)** | Pure-gullwing, no exposed pad = easiest hand-solder; PWM-on-IN scheme keeps the 8-GPIO budget |
| 2026-07-04 | **PD STUSB4500 (QFN) → CH224K (ESSOP-10)** | Resistor-set PD sink, hand-solderable; off-DigiKey (every DigiKey PD sink is QFN) |
| 2026-07-04 | **Charger BQ25628E (WQFN) → LT3652 (MSOP-12E)** | Leaded buck charger, VIN ≤32 V, resistor-set 4.05 V + NTC + timer; BAT-node power-path. Loses I²C + multi-zone JEITA (single-window NTC); health-cap now hardware-set |
| 2026-07-05 | **Homing 2× DRV5032 Hall + magnets → 1× reflective optical (ITR8307)**; **TCND5000 dropped** | Single sensor + punched dial hole, no hand magnets (§5); ITR8307 (3.4×1.5×1.1 mm) is ~5× smaller than the TCND5000 (6×4.3×3.75 mm) → smallest hole; short range OK since hands sweep <3 mm off the dial |
| 2026-07-12 | **Homing sensor ITR8307 → onsemi QRE1113** (ITR8307 went unavailable) | Same reflective phototransistor, analog out → same ADC + 150 Ω/10 kΩ network (electrical drop-in, no FW/pin change); QRE1113 larger (≈3.6×2.9×1.7 mm) → marginally bigger dial hole, still ≪ TCND5000; DigiKey 2175990, ~$0.76 |
| 2026-07-12 | **Protector FET AO4800 → AOSD32334C** (AO4800 sold only in 3000-pc reels) | Pin-function-identical dual-N SO-8 (two independent FETs), same 30 V / ±20 V / 7 A, lower R<sub>DS(on)</sub> (<20 mΩ@10 V / <26 mΩ@4.5 V); DigiKey cut-tape from 1 pc (11567511, ~$0.75). Electrical + footprint drop-in — no FW/pin change |
| 2026-07-12 | **Protector IC S-8261 → Diodes AP9101CK6-BXTRG1** (S-8261 reel-only, 3000-pc MOQ) | Same controller+external-dual-N-FET topology (AOSD32334C unchanged); **-BX suffix = OV 4.28 V / OD 2.80 V** → identical overcharge cutoff, double-redundant OV preserved. SOT-23-6, DigiKey **cut-tape from 1 pc** (7352542, ~$0.28). NRND but stocked — **reviewed & kept 2026-07-12** (one-off, in stock; buy 2–3 spares — active single-qty fallbacks are R5478N / DW01A on LCSC if a restock gap hits). IC pinout differs from S-8261 (CO/DO drive) → set schematic nets from the AP9101C datasheet |
| 2026-07-05 | **Sensors consolidated to 3 parts: TMP117 dropped; SHT45 + SGP41 → BME688; accel BMA400 → LIS3DH. Locked set = BME688 + TSL2591 + LIS3DH on both build paths.** Build path chosen: **2a Adafruit STEMMA QT chain**, 2b custom daughterboard deferred | TMP117 is redundant (any temp sensor covers it); one BME688 does T/RH/press/VOC → 3 sensors → 1 board on the bus; no leaded accel exists, so LIS3DH rides a module. **2b uses the identical bare chips → same part numbers, same I²C addresses, same firmware.** 2a = Adafruit 5046 + 1980 + 2809 (≈$31), zero leadless soldering; 2b = one JLCPCB-assembled daughterboard (hand-solder its header only). Filed `sensor_env_bme688.pdf` + `sensor_accel_lis3dh.pdf`; removed unused TMP117/SHT4x/BMA400 datasheets |
| 2026-07-04 | **Fuel gauge MAX17048 (µDFN) dropped → ESP32 ADC** | No hand-solderable 1-cell gauge exists; voltage-based SoC on a divider suffices |
| 2026-07-04 | **Protector LC05111 (DFN) → S-8261 (SOT-23-6) + AO4800 (SO-8)** | Ubiquitous leaded protector-IC + dual-FET; OV 4.28 V matches the old part; double-redundant OV preserved |
| 2026-07-04 | **Leadless-only sensors → I²C breakout modules** | SHT4x/SGP41/VEML7700/BMA400 only exist DFN/LGA → mount as Qwiic/STEMMA boards, not bare silicon |
| 2026-07-04 | **Display connector = Hirose FH34SRJ-10S-0.5SH** | LS032B7DD02 tail is a 10-pin 0.5 mm FPC (device spec Table 4-1 / 8-2-1); this HRS part is a spec-recommended, dual-contact mate; 0.5 mm ZIF is hand-solderable → on-board (alts: FH28-10S-0.5SH, Molex 503480-1000, Panasonic AYF531035) |
| 2026-07-04 | **T/RH SHT40 → SHT45; + optional TMP117** | SHT45 hits **±0.1 °C / ±1 %RH** (R4 accuracy) and supplies SGP41's T/RH compensation; **TMP117** (±0.1 °C) added as an optional dedicated/RH-decoupled temp reference |
| 2026-07-04 | **Light VEML7700 → TSL2591** | 188 µlx–88 klx / 600M:1 range resolves **near-dark** (weak ambient light) for night auto-dim + 0-emission logic; VEML7700 kept as the lux-direct alt |
| 2026-07-04 | **RTC kept (RV-3028-C7 / DS3231SN)** | The ESP32-S3's own RTC loses time on full power loss (USB out **and** cell removed/dead) and is uncompensated; a coin-cell-backed RTC drives the hands to the correct absolute time **instantly at cold boot**, SNTP-disciplined. RV-3028-C7 = ULP 45 nA (breakout); DS3231SN = SOIC-16 TCXO on-board |
| 2026-07-04 | **RTC IC dropped → S3 RTC + 32.768 kHz crystal (no battery)** *(supersedes above)* | No 2nd battery wanted; a battery-less RTC chip doesn't survive power loss either, so it adds parts for nothing. The S3's internal RC drifts %-level over temp → a **32.768 kHz crystal** on XTAL32K gives ±~20 ppm (≈1.7 s/day), SNTP-disciplined. Total power loss (USB + cell gone) → lose time, re-sync on boot behind a clock animation |
| 2026-07-07 | **IO expander added = MCP23017** (SOIC/SSOP-28) on the shared I²C + 1 INT | N16R8 pin budget is tight (~33 usable, octal PSRAM eats 3); offload slow/static lines (SPK_SD, 12 V-EN, stepper STBY, buttons, enc-SW, PD PG, CHRG, FAULT) → **net −7 host GPIO**. Steppers stay on MCPWM, encoder on PCNT — PWM/quadrature can't be bit-banged over I²C. Full map in `esp32.md` |
| 2026-07-12 | **LED 12 V source resolved — no barrel jack** | Wake COB runs off the shared **TPS55340 boost, plugged-only**; on battery the amp PVDD auto-drops to 5 V via an **LTC4412 mux** (quieter alarm) and the 5 V panel LEDs stay on. A bright sunrise on a draining backup cell is pointless → bright LEDs are a mains feature |
| 2026-07-12 | **Backup mode = adaptive** | Wi-Fi modem-sleep while SoC is healthy (responsive to app/BLE/alarm) → **deep-sleep at low battery** to stretch runtime; alarm always fires |
| 2026-07-12 | **One-shot ~77 °C TCO added** (cell − path) | Non-resettable thermal fuse for extra abuse margin vs a hot/internally-shorted cell, independent of the NTC/charger; small cost + assembly add (exact part TBD) |
| 2026-07-12 | **Software VCOM locked** (Sharp panel: EXTMODE=L, no EXTCOMIN) | Saves a GPIO at the 32/33 pin budget; the MCU inverts VCOM via the SPI frame-inversion bit, only while DISP is on (off/retained = no toggling) |
| 2026-07-17 | **Homing sensor lead form locked: QRE1113 → QRE1113GR** (SMD, Case 100CY) | Same die/interface, gull-wing SMD; custom land pattern drawn from the case drawing (`kicad/clock.pretty`); [DigiKey 965451](https://www.digikey.com/en/products/detail/onsemi/QRE1113GR/965451), ~$0.36 (cheaper than the TH form) |
| 2026-07-17 | **UI moved off-board: EC11 encoder + 3 buttons → J10** (JST SH 1×08, SM08B-SRSS-TB, [DigiKey 926714](https://www.digikey.com/en/products/detail/jst-sales-america-inc/SM08B-SRSS-TB/926714), ~$0.80) *(superseded 2026-07-19/20 → single EM14 knob on ZH)* | Encoder/buttons mount on the enclosure, not the PCB → placement freedom in the wooden case; same connector family as the sensor board J7 (cheap pre-crimped SH cables). Pinout: 1 GND · 2 ENC_A · 3 ENC_B · 4–6 BTN1-3 · 7 ENC_SW · 8 GND. Switch lines keep MCP23017 internal pull-ups; A/B keep on-board 10 k pull-ups |
| 2026-07-17 | **TAS5760M bootstrap caps corrected: 33 nF → 0.22 µF ×4** | Datasheet Fig. 62 typical application value; §9.2.2.2.3 warns deviations can range "degradation … to destructive failure". Was a placeholder flagged in `kicad/README.md` |
| 2026-07-17 | **Protector IC AP9101CK6-BX → HYCON HY2111-GB** (whole AP9101/AP9101CA family now NRND/obsolete at Diodes) | Same controller+external-dual-N-FET topology and **same SOT-23-6 pin arrangement** (1 OD · 2 CS · 3 OC · 4 NC · 5 VDD · 6 VSS) → nets unchanged; **-GB = OV 4.28 V (rel 4.08) / OD 2.90 V / OC 150 mV, 0 V-charge allowed** — overcharge cutoff identical, OD backstop 0.1 V higher (negligible runtime cost). Support values per HYCON datasheet: R VDD **100 Ω** (was 330), R CS **2 kΩ** (was 2.7 k), C 0.1 µF kept. Sourcing: quality drop-ins (ABLIC S-8261ABMMD exact twin, Nisshinbo R5478N) are 3000-MOQ/no-stock at DigiKey → **LCSC C82747** (~$0.09, 4 k+ stock, MOQ 5; off-DigiKey precedent = CH224K); EVVO DW01 clone (DigiKey Marketplace) rejected for the safety layer |
| 2026-07-17 | **X40.879 solders directly to the PCB — custom footprint + symbol** (`kicad/clock.pretty` / `clock_custom`), **J4 stepper connector dropped** | Motor mounts on the board with both shafts through a clearance hole (hands in front, PCB behind the dial); footprint drawn from the factory **STEP model** + pinout figure (8 pins: 1e–4e external coil, 1i–4i internal); STEP filed as the 3D model in `kicad/3d/` |
| 2026-07-17 | **USB-C port moved to the enclosure wall — Adafruit 6069 panel-mount extension**; on-board USB4105 + CH224K unchanged *(superseded 2026-07-19 — vertical receptacle, extension dropped)* | Passive C-plug→panel-C-jack passthrough keeps CC continuity → 15 V PD negotiation intact ([DigiKey 25897275](https://www.digikey.com/en/products/detail/adafruit-industries-llc/6069/25897275), ~$4.50). The wired 6-pin pigtail route (Adafruit 6153) was rejected: its **built-in 5.1 kΩ CC pull-downs** parallel the CH224K's Rd → PD falls back to 5 V and the LT3652 (UVLO 11.2 V) would never charge |
| 2026-07-19 | **Product form → walnut cube (~120 mm), aluminum front plate + top knob; info display DROPPED; R14 orientation retired** | Redesign after sketching (`Clock_V4.png`): one Ø~90 mm dial behind glass in a cube−10 mm aluminum plate (5 mm wood reveal), aluminum hands on walnut, 12 black dots; hands double as the set-mode readout so no screen is needed. Sharp LS032B7DD02 + FH34 removed from BOM/schematic; **datasheets intentionally kept in `/datasheet`** for a future variant. SPI2 → microSD-only; IO17 + expander GPB3 freed |
| 2026-07-19 | **Status/dial lighting → 7× SK6812 RGBW NeoPixels on the PCB** (Adafruit 2758): 5 status (bell/alarm-clock/clock/volume/battery icons) + 2 dial illumination; **panel-LED string dropped** | One RMT data GPIO (IO7, freed from PANEL_PWM) drives all 7 daisy-chained — one wire is ample (7×32 bit @ 800 kHz ≈ 0.3 ms/refresh; brightness + soft ramps in firmware). 5 V rail → works on battery. 3.3 V GPIO is below the SK6812's 0.7·VDD=3.5 V VIH spec → **SN74AHCT1G125** buffer. Recovers 1 AO3400A + 1 LEDC ch; RMT was free (SK6812 halo had been dropped v0.16) |
| 2026-07-19 | **UI → single top knob: Bourns EM14A0D-C24-L064S optical encoder + Kilo OEJNI-90-1-5 aluminum knob** on **J10** (ZH 1×06 w/ +5 V); **ENC_SW → host IO17** (was expander GPA6); **BTN1–3 dropped**; **radio-disable toggle on J11** (ZH 1×02, expander GPA3) | Press cycles the 5 status-LED modes, rotate edits (§12). Optical + no-detent + heavy machined knob = smoothest HiFi feel, no contact wear/bounce; 64 CPR ×4 on PCNT = 256 counts/rev — 2 channels fully sufficient. **EM14 is a 5 V part** (~30 mA opto-ASIC): A/B outputs are 5 V logic → **100k/200k dividers (~3.2 V)** into IO47/48 (S3 pins are not 5 V-tolerant); shaft 1/4″ flatted = the knob's 1/4″ set-screw bore. Press needs a crisp host interrupt + ~5 ms debounce — I²C-polled expander adds latency → moved to IO17 (freed by the display). Radio toggle is slow/static → expander. *(Earlier same-day PEC11R-4015F pick superseded by the user's EM14 choice.)* |
| 2026-07-19 | **USB-C receptacle → vertical Same Sky UJ20-C-V-C-2-SMT-TR** *(superseded 2026-07-20 → USB4160)*; USB4105 + Adafruit 6069 extension dropped | Port now stands on the PCB and exits the back wall directly — no extension cable. Vetting trail: the sketch's **USB4140** is the **6-pin power-only** vertical variant (no D± → kills native USB-JTAG); the suggested **UJ20-C-V-C-1** has legs/layout specified for a **0.8 mm PCB** (ours is 1.6 mm 4-layer); the **-C-2** sibling has 2.0 mm **through-hole pins** — right for 1.6 mm, easiest to hand-solder, strongest against plug insertions ([DigiKey 24766932](https://www.digikey.com/en/products/detail/same-sky-formerly-cui-devices/UJ20-C-V-C-2-SMT-TR/24766932), $0.95, **active but backorder-only 2026-07-19** — order on restock or Mouser/Newark; USB4145-03-0170-C stays the in-stock fallback, footprint would need swapping). Custom TH footprint drawn from the Same Sky drawing (`kicad/clock.pretty`) — **verify vs their DXF before fab** |
| 2026-07-20 | **USB-C locked = GCT USB4160-03-0230-C** (24-pin USB 3.2 Gen2 vertical, USB 2.0 subset wired) — supersedes the backordered UJ20-C-V-C-2 | User pick, **$1.22 / 5,753 in stock** at DigiKey, active. Same GCT vertical land family as the USB4145; the SS pads (A2/A3/A10/A11/B2/B3/B10/B11) are left unconnected (16P symbol kept). Footprint = the user-supplied vendor CAD file cleaned into `kicad/clock.pretty` (stake slots made plated `SH` per the GCT drawing's Solder Area, pegs normalized, Edge.Cuts artifacts removed) — cross-check vs the GCT drawing before fab. 2.30 mm stakes protrude ~0.7 mm under a 1.6 mm board (fine; 0170 variant is the flush option). Datasheet filed (`connector_usb_c_usb4160.pdf`) |
| 2026-07-20 | **Off-board connectors standardized on JST ZH (1.5 mm)**: J7 sensor + J10 knob = **B6B-ZR** 1×06, J11 toggle = **B2B-ZR** 1×02; J3 speaker + J9 wake strips stay **JST PH** (power) | One cheap pre-crimped cable family — **A06ZR06ZR28H102B**-style ZH↔ZH jumpers (more/fewer pins as needed) — serves sensor board, knob and toggle; ZH is 1 A/50 V, TH top-entry, KiCad-stock footprints. J7 grew 1×05 → 1×06 (pin 6 = spare wire). Replaces the short-lived SH picks (SM06B/SM02B). ZH series datasheet filed (`connector_jst_zh.pdf`) + v0.19 part datasheets rows 30–34 in `datasheet/README.md` |
| 2026-07-20 | **18650-holder footprint mismatch resolved → Keystone 1043 (TH)** — BT1's footprint had stayed on the SMT **1042** while the BOM said 1043 | **1043 wins on stock + price** ($2.99, **21.9 k** at DigiKey vs the $5.99 SMT 1042) and TH PC pins suit the hand-soldered build + a heavy cell. Custom `BatteryHolder_Keystone_1043_1x18650` drawn in `kicad/clock.pretty`: body/pegs/index identical to the stock 1042 footprint (shared molding), pins on the axis at ±35.8 with Ø2.6 drill / Ø4.2 pad (covers the ±36.11 reading of KiCad MR !2043 too) — verify vs the Keystone drawing before fab |
| 2026-07-21 | **PCB layout generated** (`kicad/gen/pcb_build.py`, 110×110 mm 4-layer, placement-only — no traces routed); **5 of 7 status/dial NeoPixels moved off-board** onto a new 3-pin JST-PH breakout **J12** (`+5V`/`DATA`/`GND`) | Full component placement + stackup + GND pours + net assignment, built on KiCad's `pcbnew` Python API; see `kicad/PCB_NOTES.md` for the constraint-by-constraint rationale and DRC/clearance verification. The 5-LED status row couldn't get real component spacing on-board without crowding every other functional block, so it moved to off-board wiring (pitch set by the face-plate holes directly); only the 2 dial-wash pixels (renumbered **D40/D41**, chain pos 1–2, closing the ref gap) stay on the PCB, 9 & 3 o'clock either side of the movement |
