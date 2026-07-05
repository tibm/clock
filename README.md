# Wooden Smart Clock — Hardware Specification

> Living spec for a high-quality, low-power, two-part desk clock: a circular MCU-driven analog dial beside a reflective monochrome info panel.

> 🔩 **Manufacturing constraint — HAND-SOLDERABLE PARTS ONLY.** The bare PCB is fab'd externally; **every part is hand-soldered with an iron.** No **QFN / DFN / WSON / BGA / WLP / LGA** silicon sits bare on the board — every active IC is a **leaded/gullwing** package (SOIC / SOP / SSOP / TSSOP / **HTSSOP** / **MSOP** / SOT-23) or a **castellated/edge module**. The two power parts (amp `TAS5760M`, charger `LT3652`) are HTSSOP/MSOP **PowerPAD** — leads iron-solderable, belly pad on a thermal-via array (back-side hot-air optional). Functions that *only* exist leadless → **pre-made breakout modules** (env/MEMS sensors SHT4x, SGP41, VEML7700, BMA400 as I²C Qwiic/STEMMA boards) or **dropped** (fuel gauge → ESP32 ADC). **Passives ≥ 0603** (0402 min); connectors hand-solderable (through-hole / wide-pad SMD: USB-C, FPC/ZIF, JST, SD cage). **This grows the PCB — accepted.**

**Status:** v0.13 draft · **Owner:** you (FW/HW) · **Last updated:** 2026-07-04

**Changelog**
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
| R2 | ALS-driven lighting, **true 0 emission at night** | Halo + optional display front-light, hard-off enable + ALS (§8, §9) |
| R3 | Alarm, tap-to-snooze, loud multi-asset audio, sunrise halo from bottom | Accel tap-detect + Class-D audio + RGBW halo (§7, §9) |
| R4 | Air quality (VOC/AQI), temperature, humidity | SHT4x + SGP41 (or BME688); optional CO₂/PM (§8) |
| R5 | USB-C powered, battery backup + charge, run untethered | USB-C PD sink + charger + Li-ion + fuel gauge (§10) |
| R6 | Info display: low-power, **second-refresh, no ghosting, 0 light when off, daylight-readable** | Reflective Memory LCD (MIP) — no backlight to leak light (§4) |
| R7 | Wi-Fi + BLE, **single MCU** driving everything | ESP32-S3 single chip: display + motors + audio + sensors (§6) |
| R8 | Minimal rear buttons, minimal on-device config | 2–3 rear tactiles + tap; rotary dial; config via app (§12) |
| R9 | Companion iOS/web app: BLE pair → Wi-Fi provision, alarms, tone upload, widgets | BLE provisioning + SD asset store (§13) |
| R10 | Extensible: more sensors, multiple sizes | I²C sensor bus + FPC/B2B connectors + modular FW (§14) |
| R11 | High-quality speaker + high-quality assets | Full-range driver + DSP amp + WAV/FLAC on SD (§7) |
| R12 | MCU fully sets/adjusts the analog time (TZ/DST jumps, NTP-synced, drift-corrected, both directions) | Bidirectional, absolutely-addressable dual-shaft stepper + homing (§5) |
| R13 | **Two-part form:** circular MCU-driven analog clock (hour + minute) + reflective mono info panel | Dual-shaft movement + Sharp memory LCD on one body (§3, §5, §4) |
| R14 | **Orientation-aware:** works flat (clock left / display right) *or* standing (clock top / display bottom); MCU knows orientation | Accel gravity-vector → MCU rotates the display and re-references the analog "12" (§3, §8) |

---

## 2. Key design decisions & open tensions

The old *"seconds vs E-Paper ghosting"* and *"reflective vs wide"* tensions are resolved. The clock is now a **two-part split-face**:

- **Face A — circular analog dial (chosen):** hour + minute hands, **fully MCU-driven** (dual-shaft stepper + homing). Real hands, silent, dark at night.
- **Face B — reflective monochrome info panel (chosen):** a **Sharp Memory LCD (MIP)** for date/weather/seconds/stocks. Reflective ⇒ **0 emission when off** and **daylight-readable with no backlight**, so the "dark at night" requirement is met *inherently* — there is no backlight to hard-off.

**Baked-in:**
- **Single MCU** (ESP32-S3) drives both faces + audio + sensors + halo — no coprocessor.
- **Numeral-free / symmetric dial** + absolute stepper addressing so the clock reads correctly in **both orientations** (§3).
- Only light emitters are the **RGBW halo** and an **optional on-demand front-light** — both hard-off = 0 emission at night.
- Split power domains (always-on low-power + bursty Wi-Fi).

**Superseded:** the earlier wide color **bar-TFT** primary display (rationale kept in the Decision log & §17).

---

## 3. Form factor & orientation (R13, R14)

**Two modules on one wooden body / PCB:**
- **A) Circular analog clock** — ~75 mm dial, two hands, driven by a dual-shaft concentric stepper with Hall homing (§5). Dial is **numeral-free / 4-fold-symmetric** (no printed numbers).
- **B) Reflective info panel** — Sharp **LS032B7DD02**, 3.16″, 47 × 76 mm (§4). Its ~73 mm height ≈ the 75 mm dial.

**Two orientations** (device is rotated 90° in-plane and tilted between them):

| Mode | Posture | Layout | Viewed |
|------|---------|--------|--------|
| **Flat** | lying on desk | landscape — **clock left, display right** | from above |
| **Standing** | upright | portrait — **clock top, display bottom** | head-on |

**How the MCU knows & adapts (R14):** the **BMA400** accelerometer reads the gravity vector → detects *flat vs standing* and *which 90°*. The MCU then:
1. **Rotates the display framebuffer** 90° so text stays upright.
2. **Re-references the analog "12"** — because the dial has no fixed numerals, the MCU drives both hands (absolute addressing + Hall homing) to the correct positions for the new "up".

This is why the dial is numeral-free and why R12's absolute-addressing does double duty (time *and* orientation). A reflective panel + a numberless dial both read correctly at any rotation, so the whole product is **orientation-agnostic by design**.

---

## 4. Display — reflective info panel (R6)

### 4a. Decision & constraints
Hard constraints (R6): **0 light when off, daylight-readable, second-refresh, no ghosting, silent.** A **reflective Memory-in-Pixel (MIP) LCD** meets all of them with **no backlight** — nothing to leak light at night, and it's readable in daylight by reflection.

**Chosen: Sharp `LS032B7DD02`** — 3.16″, **336 × 536**, reflective monochrome MIP, **3-wire SPI** (3 V logic), **5 V panel (VDD 4.8–5.5 V)**, **30 µW hold / 250 µW update (typ)**, outline 47.02 × 76.0 × 0.705 mm (active 42.67 × 68.07 mm), −20…+70 °C. Active on DigiKey (full device spec `LD-2023X13`, 01-Nov-2023, in [`/datasheet`](datasheet/)). A **front-light variant** exists for on-demand night reading.

| Tech | 0-light off | Daylight-readable | Sec-refresh | Ghost / burn-in | Verdict |
|------|-------------|-------------------|-------------|-----------------|---------|
| **Reflective Memory LCD (MIP)** ⭐ | ✅ inherent (no backlight) | ✅ reflective | ✅ | none / none | **Primary** |
| Mono OLED | ✅ pixels off | ❌ washes out | ✅ | none / **burn-in** (static clock) | Rejected (burn-in + emits) |
| Square IPS TFT (mono UI) | ⚠️ only if fully off (then unreadable) | ❌ backlit | ✅ | none / none | Backlit alt |
| E-Paper | ✅ reflective | ✅ | ❌ | **ghosts** | Rejected (seconds) |

*"White-on-black" is not a hard constraint — the reflective panel shows dark-on-light, which is fine given 0-emission + daylight readability are the real requirements.*

### 4b. Making it not look cheap
Flush cover glass over the panel with a **printed border mask** (bezel disappears) under edge-to-edge glass; **AR / anti-glare** front surface; restrained variable-font typography, single accent; tear-free updates (double-buffer the 1-bit frame). Front-light (if fitted) uses a warm, ALS-gated night dim, **hard-off by default**.

---

## 5. Analog clock module — circular, MCU-driven (R12, R13)

**Design:** a circular dial with **hour + minute** hands on a **dual-shaft concentric** stepper, so the MCU sets any absolute time. Silent, real hands, dark at night. **Seconds live on the info panel** (§4), not the dial.

**R12 recap:** the MCU must **set/adjust the displayed time on demand** — jump for TZ/DST, correct NTP drift, both directions — which needs a **bidirectional, absolutely-addressable** movement with a **home reference** (rules out forward-only sweep-quartz).

**Motor comparison** — must do **360° on *both* hands**, be **extremely quiet**, use **light hands**, and let the **MCU set any absolute time** (R12):

| Motor | Type · 360° both? | MCU sets abs. time (bidir + homing) | Torque → hand size | Noise | Size | Power | ~Cost | Where |
|-------|-------------------|-------------------------------------|--------------------|-------|------|-------|-------|-------|
| **Juken X40.879** ⭐ **(75 mm pick)** | Dual coaxial shaft (hour+minute) · ✅ | ✅ bidirectional; **add external Hall home** (no built-in zero-detect) | higher → OK for ~35 mm hands / 75 mm dial | very quiet (Swiss, microstep) | vertical, compact | ~15–20 mA | ~$14 | **DigiKey** (Juken) |
| Juken X10.506 (small ≤50 mm) | Dual-shaft concentric **clock** motor · ✅ | ✅✅ **zero-detection variant = built-in homing** | low, indicator-class (~1–2 mN·m) → light short hands | very quiet | ~Ø28 mm | ~15–20 mA | ~$11 | Minitools / DigiKey (Juken) |
| VID28-05 / BKA30D-R5 (budget) | Dual-shaft concentric · ✅ (**buy 360° variant**) | ✅ bidirectional; **add external Hall home** | ~4 mN·m → ≤~10 g hands | microstep ~20–30 dBA | 29×59 mm | ~15–20 mA/coil | ~$4–6 | AliExpress/Amazon/eBay (**not DigiKey**) |
| Sonceboz 6407 | Dual-shaft, automotive-grade · ✅ | ✅ (+ homing) | higher, smooth | very quiet | ~30 mm | low | $$ | mfr direct (MOQ) |
| Juken/Switec X27.168 / X25.168 | **Single**-shaft gauge · ⚠️ 315° (remove stop) | ✅ bidirectional, but single shaft → **need 2 + coax mechanics** | ~0.6–1 mN·m (weakest) | microstep quiet | Ø26 mm | ~20 mA | ~$4–8 | **DigiKey** (X27.168) |
| Continuous-sweep quartz | Geared clock · ✅ | ❌ **forward-only + coupled → can't jump for TZ/DST** | strong (real hands) | **silent <20 dBA** | 56×56×15 mm | µA–mA | $3–10 | clock-parts shops |
| 28BYJ-48 geared stepper | Single-shaft geared · ✅ | ✅ electrically | strong (~34 mN·m) | **buzzy 45–55 dBA ✗** | 28 mm+box | 100–200 mA | ~$2 | everywhere |

**Picks (with R12 + 75 mm dial):**
- **Primary: Juken X40.879** ⭐ — dual coaxial hour+minute, **DigiKey-stocked (~$14)**, enough torque for ~35 mm hands on a 75 mm dial; add a **Hall (DRV5032) + magnet home index per shaft** (the X40 has no built-in zero-detect); Swiss-quiet with microstepping. Its datasheet is a short **pinout addendum** (two *independent* dual-shaft bipolar coils, 8 pins, X27-compatible mechanics) that **defers to the X27 base spec** for torque/current/dimensions — so both `stepper_motor_x40-879.pdf` **and** `stepper_motor_x27_base-spec.pdf` are kept in [`/datasheet`](datasheet/).
- **Small alt: Juken X10.506** — if you drop to ~50 mm; its **zero-detection variant gives built-in homing** (cleanest R12), but too little torque for 75 mm hands.
- **Budget alt: VID28-05 / BKA30D-R5** — same concept for ~$5, but off-DigiKey; add Hall homing.
- **Rejected:** sweep-quartz **fails R12** (forward-only, coupled); 28BYJ-48 (noise).

**Design notes:**
- **Homing:** on boot and after each NTP sync, drive each shaft to its Hall index, then step to the exact time; TZ/DST/**orientation** = step to the new absolute position (fast, both directions).
- **Orientation:** because the dial is **numeral-free/symmetric**, the MCU redefines "12" per orientation (§3) using the same absolute addressing that serves R12.
- Buy/reuse the **360°/no-stop** movement; **microstep @ >20 kHz PWM** for silence; **light, counterbalanced hands** (metal hands OK on the X40.879's higher torque).
- **Driver:** **2× TB6612FNG** (SSOP-24, hand-solderable, no exposed pad) — one per shaft; VM 5 V, VCC 3.3 V; **PWM the IN pins with PWMA/PWMB tied high → sign-magnitude microstep in 8 GPIO** (parity with the old DRV8835). *DRV8835/DRV8833 are WSON/HTSSOP — replaced for hand-assembly.* **Homing Hall:** DRV5032 (SOT-23) + magnet per shaft. **Libs:** SwitecX25 / GewoonGijs-VID28 / AccelStepper.

---

## 6. MCU + wireless (R7)

A **single MCU** must drive the SPI memory LCD, two stepper shafts, I²S audio, addressable LEDs, the I²C sensor bus, and Wi-Fi + BLE. **No parallel-RGB / LTDC is needed** (the display is a low-bandwidth 3-wire SPI panel), which widens the field — but the **ESP32-S3 single-chip** still wins on simplicity + library support.

| Option | Wi-Fi + BLE | Runs display + motors + audio | Low power | Notes | Verdict |
|--------|-------------|-------------------------------|-----------|-------|---------|
| **ESP32-S3** ⭐ single-chip | Wi-Fi 4 (2.4G) + BLE 5 | ✅ SPI LCD + PWM steppers + I²S | good deep-sleep, higher idle | cheapest, fastest bring-up, huge lib support (§6c) | **Primary** |
| STM32U5 + ST67W611M1 | Wi-Fi 6 + BLE 5.4 (SPI) | ✅ | **excellent** | ST ULP; LTDC now unused (SPI panel) | ULP alt |
| nRF5340 + nRF7002 | Wi-Fi 6 + BLE 5.x | ✅ (no EVE coprocessor needed now) | **best radio power** | simpler without RGB; Nordic tooling | Nordic ULP alt |
| NXP RW612 single-chip | Wi-Fi 6 dual-band + BLE 5.4 + Thread | ✅ (SPI panel fine now) | good | viable single-chip now RGB is gone | single-chip alt |
| ESP32-P4 + C6 | Wi-Fi 6 + BLE (C6) | ✅ overkill | med | 2 chips; for a large MIPI panel in future | future |

### 6c. ESP32-S3 quick-start libraries
Base: **ESP-IDF v5.x** (C, production, best power control) or **Arduino-ESP32 / PlatformIO** (fast prototyping).

| Requirement | Library / component |
|---|---|
| Info display (mono, SPI) | **LVGL v9** at 1-bit depth + Sharp Memory-LCD panel driver (`esp_lcd` panel or an Adafruit `SHARP_Memory` port) |
| Analog steppers (§5) | **TB6612FNG** via GPIO/PWM; **AccelStepper** / **SwitecX25** / VID28 lib; microstep for silence |
| Orientation + tap (R14, R3) | **BMA400** driver: gravity-vector orientation (flat/standing) + hardware tap IRQ |
| Wi-Fi | `esp_wifi` |
| BLE-pair → Wi-Fi provision (R9) | `wifi_provisioning` (BLE transport) + Espressif **"ESP BLE Provisioning"** phone app |
| BLE stack | **NimBLE** |
| NTP + TZ/DST (R1) | `esp_netif_sntp` + POSIX `TZ` string (`setenv`/`tzset`); run the S3 RTC off the **32.768 kHz XTAL32K** (GPIO15/16) so it holds ±~20 ppm between syncs — **no RTC chip, no battery** |
| Env sensors (R4) | Sensirion `embedded-i2c-sht4x / sgp41 / scd4x / sps30`; Bosch **BSEC/BME68x** |
| Light | **TSL2591** (Adafruit_TSL2591 / esp-idf-lib) or VEML7700. Precision temp: **TMP117** (Adafruit_TMP117) |
| Audio out (I²S) | `esp_driver_i2s`; decode via **ESP-ADF** (MP3/AAC/FLAC/WAV) or Arduino **ESP32-audioI2S**; **~150 Hz high-pass + limiter/EQ biquads run here** (TAS5760M has no on-chip DSP) |
| Halo + front-light | `led_strip` (RMT/SPI, SK6812) + `ledc` (PWM dimming) |
| SD + assets | `esp_vfs_fat` + `sdmmc`; `LittleFS` for internal flash |

**Fastest bring-up:** **ESPHome** gets sensors / SNTP / addressable LED / I²S audio running in an afternoon; the memory LCD + steppers likely need a custom component or a drop to ESP-IDF. **esp-bsp** has ready board+display+LVGL configs.
**Caveat:** the **LT3652** charger is autonomous (no driver needed) — read charge state via **CHRG/FAULT GPIO + cell-voltage ADC** (no I²C charger/gauge); the health-cap 4.05 V is fixed in hardware by the float divider. See [`power.md`](power.md).

**RTOS/lang:** C/C++ + FreeRTOS (ESP-IDF). MicroPython for quick experiments only.

---

## 7. Audio subsystem (R3, R11)

- **Amp: TAS5760M** ⭐ (I²S in + I²C control, closed-loop Class-D, **HTSSOP-32 → hand-solderable**; digital clipper + DC-detect/OC/OT protection; up to 55 W stereo / 114 W mono PBTL). Run **PBTL mono** into the 4 Ω DMA58-4; **PVDD ~12 V boost → ~8–12 W** (or 5 V → ~3 W). Needs an **LC output filter**. **No on-chip biquad DSP** (unlike the superseded QFN TAS5825M) → the **~150 Hz high-pass + limiter/EQ that protects the 2 mm-Xmax driver runs in ESP32-S3 firmware**. *Analog alt (all hand-solderable):* **PCM5102A** DAC (TSSOP-20) → **TPA3116D2** Class-D (HTSSOP-32).
- **Chain:** ESP32-S3 → **I²S** (3 pins: BCLK, LRCLK, DOUT) → amp → speaker.
- **Source:** WAV (trivial) / FLAC (decoder + CPU) on microSD; system sounds in flash.
- **Tap-to-snooze:** accel hardware tap IRQ (§8) so the MCU sleeps until tapped.

### 7a. Sizing (what "close to a BT speaker" costs in space)
The small display does **not** limit audio — the speaker lives behind/beside it in the wood body. Quality comes from **enclosure depth + a sealed air chamber**, not display area.

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

## 8. Sensors (R4, R14)

| Function | Part | Notes | ~Cost |
|----------|------|-------|-------|
| Temp + humidity ⭐ | **Sensirion SHT45** (`SHT45-AD1B-R2`) | **±0.1 °C / ±1.0 %RH** — accurate grade of SHT4x; also feeds SGP41 comp | $4–6 |
| Precision temp *(opt)* | **TI TMP117** | **±0.1 °C** dedicated; redundant if SHT45 fitted — use for a fast, RH-decoupled reference | $2–5 |
| VOC (+NOx) index | **Sensirion SGP41** | pairs with SHT45 (needs its T/RH) | $5–8 |
| *All-in-one alt* | **Bosch BME688** | T/RH/pressure/gas + AQI; BSEC lib — looser (±0.5 °C / ±3 %RH) | $8–12 |
| CO₂ *(opt)* | **Sensirion SCD41** | true NDIR CO₂ | $18–30 |
| PM2.5 *(opt)* | **Sensirion SPS30** | has fan; power/size | $30–45 |
| Ambient light ⭐ | **ams-OSRAM TSL2591** (`TSL25911FN`) | **188 µlx–88 klx**, 600M:1 range → resolves near-dark; VEML7700 = lux-direct alt | $2–3 |
| **Motion / tap / orientation** | **Bosch BMA400** | nA-class, hardware tap engine **+ gravity-vector orientation (flat/standing, R14)** | $1.5–2.5 |
| Timekeeping | **ESP32-S3 RTC + 32.768 kHz crystal** (no RTC IC) | **No coin cell.** Crystal (~±20 ppm ≈ 1.7 s/day) holds between **SNTP** syncs; total power loss (USB **and** 18650 gone) → re-sync on boot, clock animation meanwhile. A battery-less RTC IC would add parts without fixing the loss case → dropped | ~$0.3 |

A single 3-axis accel (**BMA400**) covers **tap-to-snooze *and* orientation** — no gyro needed, since flat-vs-standing is a static gravity reading. All I²C on a shared bus (§14). **Vent AQ sensors to outside air, away from amp/LEDs/battery** — their heat skews T/RH/VOC.

---

## 9. Lighting (R2, R3)

- **No backlight** — the info panel is reflective (0 emission when off, daylight-readable).
- **Optional display front-light:** edge white-LED front-light (the LS032 front-light variant) for **on-demand** night reading; ALS-gated, warm dim, **hard-off by default** → 0 emission at night.
- **Halo:** **SK6812 RGBW** (warm-white channel) or APA102/SK9822 (SPI, deterministic) along the base; frosted diffuser; sunrise = warm-white ramp over N min before audio; also serves as the night-light.
- *Optional:* subtle edge-lit **analog-dial** illumination if hands need night visibility — off by default.

---

## 10. Power (R5)

> Full power tree, budget, and battery-safety detail in [`power.md`](power.md). Summary below (kept in sync).

- **Input:** USB-C with **USB-PD sink** (**CH224K**, ESSOP-10, resistor-set request 15 V; graceful fallback 5 V).
- **Charge / path:** **LT3652** (MSOP-12E 1-cell **buck** charger, VIN ≤32 V, resistor-set **4.05 V** float + charge current, **NTC** temp-qualified charging, safety timer). The **BAT node is the always-on system supply** → runs plugged with **no cell** (LT3652 sources up to 2 A). *No I²C — autonomous; state via CHRG/FAULT + ADC.*
- **Fuel gauge:** **ESP32-S3 ADC** on a cell divider (no hand-solderable gauge IC exists) → voltage-based SoC, low-batt shutdown ~3.2 V.
- **Battery:** **user-supplied 18650 in a holder, Li-ion ONLY** (labeled "Li-ion 18650 only · 2.5–4.2 V"). Recommend a protected 3000–3500 mAh cell. **48 h backup** target.
- **Health:** charge-cap **~80 % (4.05 V)** — fixed in hardware by the LT3652 float divider; optional GPIO-switched divider for a 4.2 V "full" mode.
- **Rails:** **3.3 V** (MCU, from 5 V) · **5 V** (panel + stepper) · **12 V boost** (audio PVDD, gated) · **15 V VBUS** (LED sunrise, plugged only). All rail converters in **leaded** packages (see §16 / `power.md`).
- **Safety (mandatory, wood/bedroom, any 18650) — simple + double-redundant:** independent **cell protector** (**S-8261** SOT-23-6 + **AO4800** dual-N SO-8 FET: OV/OD/OC/SC) redundant to the charger, **reverse-polarity P-FET**, **NTC** temp-qualified charge, TVS, insert-qualify, FR barrier + venting. Double-redundant overcharge (charger 4.05 V + protector **4.28 V**), layered over-discharge. *(⚠ vs the superseded BQ25628E: no I²C telemetry + single-window NTC, not multi-zone JEITA — still meets "no charge <0/>45 °C". Secondary OVP/TCO/PPTC still dropped.)* Full wiring/config in [`power.md`](power.md).

---

## 11. Storage (R9, R11)

- **microSD** (SPI mode, 4-wire — saves GPIO vs SDMMC) — user tones, images, widget assets.
- **Flash** (module 16 MB + optional external OSPI NOR) — fonts, glyph atlases, system sounds, OTA, config.

---

## 12. Inputs / buttons (R8)

- **2–3 rear tactiles:** `POWER/WAKE`, `PAIR/MENU` (long-press = pair / factory reset), optional `SNOOZE/MUTE`.
- **360° rotary encoder dial:** menu adjustments, set clock, set alarm, etc.
- **Tap-to-snooze** via accel (top-tap) → front button-free.

---

## 13. Companion app (R9)

BLE pair → GATT provisioning → push Wi-Fi creds → join AP. Configure alarms, sunrise, brightness, TZ, widgets/APIs. Upload tones (→ SD) + layouts/fonts (→ flash) over BLE or Wi-Fi. iOS app and/or web (Web Bluetooth). ESP path: reuse Espressif's provisioning app for v1. QR code for quick setup?

---

## 14. Extensibility (R10)

Shared **I²C** (Qwiic/STEMMA-QT) for drop-in sensors; the display driver sits behind an LVGL geometry config so a larger/second reflective panel drops in; movement, LED, speaker, and display on FPC/B2B/JST connectors; reusable compute+radio+power core block.

---

## 15. PCB considerations

4-layer min (6 if dense); solid ground plane, RF keep-out (or use a module w/ onboard antenna to inherit certs). **3-wire SPI** to the memory LCD is short and low-bandwidth — **no length-matching needed** (the RGB pixel-clock group is gone with the bar TFT). **Two stepper shafts** via 2× TB6612FNG — keep coil traces + PWM away from RF/audio; place a **home Hall (DRV5032)** at each shaft. Class-D + speaker return away from RF/analog. Copper for charger/amp heat. FPC/ZIF for the display, JST for battery/speaker/LED/movement, SD cage, USB-C w/ ESD+CC. **Hand-solder rules:** leaded ICs only (no QFN/DFN/BGA/LGA); **≥0603 passives**; the PowerPAD amp + charger get a **thermal-via array** under the belly pad; leadless-only sensors ride on I²C **breakout modules**; use generous footprints/spacing — board area is traded for hand-assembly.

---

## 16. Shopping list (DigiKey)

> DigiKey MPN / part links — click to verify live stock/price. Prices approximate, single-unit. Wood/enclosure excluded. Parts marked ✅ verified active on DigiKey; others use a DigiKey keyword search — confirm the exact orderable variant. Core-part datasheets + a summary table live in [`/datasheet`](datasheet/).

### 16a. Prototyping kit (fast start — dev boards + breakouts)

| Item | MPN | ~Price | Source |
|------|-----|--------|--------|
| SoC dev board (16 MB / 8 MB PSRAM) | ESP32-S3-DevKitC-1-N16R8 | ~$18 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=ESP32-S3-DevKitC-1-N16R8) |
| Info display (reflective MIP) | Sharp LS032B7DD02 | ~$39 | [DigiKey 23349498 ✅](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) |
| Analog movement (dual shaft) | Juken X40.879 | ~$14 | [DigiKey 28528329 ✅](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) |
| Motor driver carrier (×2) | SparkFun TB6612FNG (ROB-14451) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=TB6612FNG%20carrier) |
| Home Hall (×2) | DRV5032 breakout / bare | ~$1 | [DigiKey 7400094 ✅](https://www.digikey.com/en/products/detail/texas-instruments/DRV5032FADBZR/7400094) |
| I²S amp (bring-up) | Adafruit 3006 (MAX98357A) → TAS5760M EVM | ~$6 / — | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%203006) |
| Speaker (2″ full-range) | Dayton DMA58-4 | ~$19 | *(Parts Express 295-582 — not DigiKey)* |
| T/RH breakout (±0.1 °C) | Adafruit 5665 (SHT45) | ~$6 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%205665) |
| Precision temp breakout *(opt)* | Adafruit 4821 (TMP117) | ~$8 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%204821) |
| VOC breakout | Adafruit 4829 (SGP40) | ~$15 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%204829) |
| Light breakout (weak-light) | Adafruit 1980 (TSL2591) | ~$7 | [DigiKey 4990786](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1980/4990786) |
| Accel breakout (tap+orient) | SparkFun BMA400 Qwiic (or Adafruit 2809 LIS3DH) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=BMA400) |
| RGBW halo strip | SK6812 RGBW strip | ~$8 | *(Adafruit / AliExpress)* |

### 16b. Production BOM (ICs, custom PCB)

| Block | MPN | Size / pkg | ~Unit | Active | DigiKey ref |
|-------|-----|-----------|-------|--------|-------------|
| SoC module | ESP32-S3-WROOM-1-N16R8 | 18×25.5 mm | ~$6.8 | ✅ | [16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) |
| Info display | Sharp LS032B7DD02 | 3.16″, 47×76 mm | ~$39 | ✅ | [23349498](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) |
| Display FPC connector | Hirose FH34SRJ-10S-0.5SH(50) | **FPC/ZIF, 10-pos, 0.5 mm** | ~$0.7 | ✅ | [3880272](https://www.digikey.com/en/products/detail/hirose-electric-co-ltd/FH34SRJ-10S-0-5SH-50/3880272) |
| Analog movement | Juken X40.879 (dual shaft) | vertical, compact | ~$14 | ✅ | [28528329](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) |
| Motor driver ×2 | TB6612FNG,C,8,EL | **SSOP-24** | ~$2.4 | ✅ | [1730070](https://www.digikey.com/en/products/detail/toshiba-semiconductor-and-storage/TB6612FNG-C-8-EL/1730070) |
| Home Hall ×2 | DRV5032FADBZR | SOT-23 | ~$0.6 | ✅ | [7400094](https://www.digikey.com/en/products/detail/texas-instruments/DRV5032FADBZR/7400094) |
| Audio amp ⭐ (chosen) | TAS5760MDAPR | **HTSSOP-32** | ~$4–6 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=TAS5760MDAPR) |
| Audio amp (analog alt) | PCM5102A + TPA3116D2 | TSSOP-20 + HTSSOP-32 | ~$4 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=PCM5102A) |
| T/RH ⭐ *(I²C module)* | Sensirion SHT45-AD1B-R2 (±0.1 °C / ±1 %RH) | DFN-4 → module | ~$5 | ✅ | [16360966](https://www.digikey.com/en/products/detail/sensirion-ag/SHT45-AD1B-R2/16360966) |
| Temp, precision *(opt — SHT45 covers it)* | TI TMP117AIDRVR (±0.1 °C) | WSON-6 → module | ~$5 | ✅ | [9685284](https://www.digikey.com/en/products/detail/texas-instruments/TMP117AIDRVR/9685284) |
| VOC *(I²C module)* | SGP41 breakout (needs SHT45 for T/RH comp) | DFN → module | ~$15 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=Adafruit%204829) |
| Light ⭐ *(I²C module)* | ams-OSRAM TSL2591 / TSL25911FN (188 µlx–88 klx) | WFDFN-6 → module | ~$5 | ✅ | [4162547](https://www.digikey.com/en/products/detail/ams-osram-usa-inc/TSL25911FN/4162547) |
| Accel (tap+orient) *(I²C module)* | Bosch BMA400 (bare) → Qwiic breakout | LGA-12 → module | ~$5 | ✅ | [8634935](https://www.digikey.com/en/products/detail/bosch-sensortec/BMA400/8634935) |
| Timekeeping xtal (no RTC IC, no battery) | 32.768 kHz crystal — Epson FC-135 / Abracon ABS07 (CL 6–12.5 pF) → S3 XTAL32K (GPIO15/16) | 3.2×1.5 mm SMD (2-pad) *(or TH cylinder)* | ~$0.3 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=FC-135%2032.768) |
| PD sink | CH224K | **ESSOP-10** | ~$0.4 | ✅ | [LCSC C970725 *(not DigiKey)*](https://www.lcsc.com/product-detail/C970725.html) |
| Charger (1-cell buck, BAT-node path) | LT3652EMSE#PBF | **MSOP-12E** | ~$5–6 | ✅ | [2225686](https://www.digikey.com/en/products/detail/analog-devices-inc/LT3652EMSE-PBF/2225686) |
| Fuel gauge | *(none — ESP32 ADC on cell divider)* | — | ~$0 | — | — |
| Cell protector (independent) | S-8261AAxMD + AO4800 dual-N FET | **SOT-23-6 + SO-8** | ~$0.7 | ✅ | [S-8261](https://www.digikey.com/htmldatasheets/production/9482/0/0/1/s-8261-series.html) · [AO4800](https://www.digikey.com/en/products/result?keywords=AO4800) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=AO3401A) |
| Cell temp sense | 10 k NTC (Murata NCP18XH103) | 0402 | ~$0.1 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=NCP18XH103F03RB) |
| Transient | TVS SMAJ22A (VBUS) + SMAJ5.0A (BAT) | SMA | ~$0.4 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SMAJ22A) |
| Audio boost BAT→12 V (gated) | LM2587S-ADJ (or MT3608) | **TO-263 / SOT-23-6** | ~$2 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=LM2587S-ADJ) |
| 5 V rail (boost) | MT3608 (or TPS61023) | **SOT-23-6 / SOT-563** | ~$1 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=MT3608) |
| 3.3 V rail (from 5 V) | TLV62569 buck (or AMS1117-3.3 LDO) | **SOT-23-6 / SOT-223** | ~$0.6 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=TLV62569DBVR) |
| LED CC driver (off VBUS) | TPS92200D1 | SOT-23 | ~$1.5 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=TPS92200) |
| 18650 holder | Keystone/MPD 18650 holder | — | ~$1 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=18650%20holder) |
| Halo LEDs ×~16 | SK6812MINI-E | 3.5×3.5 mm | ~$0.2 ea | — | *(Adafruit / alt distributor)* |
| Front-light (opt) | white LED + FET/CC driver | — | ~$0.5 | — | — |
| microSD socket | Hirose DM3AT-SF-PEJM5 | push-push | ~$1 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=DM3AT-SF-PEJM5) |
| USB-C recept (power+CC) | GCT USB4105-GF-A (TH tabs) | TH / wide-pad | ~$0.8 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=USB4105-GF-A) |
| *CO₂ (opt)* | SCD41-D-R2 | — | ~$24 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SCD41-D-R2) |
| *PM2.5 (opt)* | SPS30 | — | ~$38 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SPS30) |
| Speaker driver | Dayton DMA58-4 (2″ FR) | 56×56×32 mm | ~$19 | — | *(Parts Express 295-582)* |
| Passive radiator *(opt)* | Dayton DMA58-PR (2″) | — | ~$8 | — | *(Parts Express)* |

**Core electronics subtotal (excl. speaker/cell/PCB): ~$130–155** (power electronics ≈ +$16–18 — the LT3652 is the priciest swap; env sensors now ride on I²C **breakout modules** ≈ +$10–15 vs bare parts). With speaker + user-supplied 18650 + holder + 4-layer PCB + passives ≈ **~$200–240**. +CO₂/PM ≈ +$62. *(Cell is user-supplied; safety HW is non-negotiable — see [`power.md`](power.md).)*

**Cost/space levers:** budget movement (VID28-05, −$10, off-DigiKey) + Hall homing; skip CO₂/PM; the display + movement are the two big line items. *(Amp DSP is free — it runs in firmware.)*

---

## 17. Alternatives quick-reference

- **MCU:** ESP32-S3 ⭐ single-chip / STM32U5 + ST67W611M1 (ULP) / nRF5340 + nRF7002 (Nordic ULP) / RW612 (single-chip) / ESP32-P4 + C6 (future MIPI).
- **Info display:** Sharp **LS032B7DD02** reflective MIP ⭐ / mono OLED (burn-in, emits) / square IPS TFT (backlit) / EPD (ghosts on seconds). *Superseded: wide color bar-TFT (NHD-3.9).*
- **Analog movement:** Juken **X40.879** (DigiKey, 75 mm) ⭐ / Juken X10.506 (small, built-in homing) / VID28-05·BKA30D-R5 (budget, off-DigiKey) / X27.168 ×2 (single-shaft).
- **Motor driver:** **2× TB6612FNG** ⭐ (SSOP-24, hand-solderable). *(DRV8835/DRV8833 = WSON/HTSSOP, replaced.)*
- **Amp:** **TAS5760M** ⭐ (I²S+I²C, HTSSOP, PBTL mono; firmware DSP) / PCM5102A + TPA3116 (analog).
- **Power:** **CH224K** (PD sink) + **LT3652** (1-cell buck charger, BAT-node path) + **ESP32 ADC** gauge + **S-8261 + AO4800** protector + reverse P-FET; 1S Li-ion 18650 holder. *(All leaded/hand-solderable; CH224K off-DigiKey.)*
- **Timekeeping (no RTC IC / no battery):** ESP32-S3 internal RTC off a **32.768 kHz crystal** (~±20 ppm, GPIO15/16) + periodic **SNTP** — drift ≈ 1–2 s/day between syncs, corrected each sync. On total power loss (USB **and** 18650 gone) time is lost → re-sync on next boot, clock animation meanwhile. *A battery-less RTC IC adds parts without fixing the loss case, so it's dropped; the crystal is the real accuracy fix.*
- **Sensors:** T/RH **SHT45** ⭐ (±0.1 °C / ±1 %RH) + precision temp **TMP117** (opt) / light **TSL2591** ⭐ (weak-light) or VEML7700 (lux-direct) / accel+orient **BMA400** / VOC **SGP41** / all-in-one **BME688**. All leadless → **I²C breakouts**.
- **Display connector:** **Hirose FH34SRJ-10S-0.5SH** ⭐ (10-pin 0.5 mm FPC/ZIF, on-board) — panel-spec recommended; alts FH28-10S-0.5SH / Molex 503480-1000 / Panasonic AYF531035.
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
| 2026-07-04 | **Fuel gauge MAX17048 (µDFN) dropped → ESP32 ADC** | No hand-solderable 1-cell gauge exists; voltage-based SoC on a divider suffices |
| 2026-07-04 | **Protector LC05111 (DFN) → S-8261 (SOT-23-6) + AO4800 (SO-8)** | Ubiquitous leaded protector-IC + dual-FET; OV 4.28 V matches the old part; double-redundant OV preserved |
| 2026-07-04 | **Leadless-only sensors → I²C breakout modules** | SHT4x/SGP41/VEML7700/BMA400 only exist DFN/LGA → mount as Qwiic/STEMMA boards, not bare silicon |
| 2026-07-04 | **Display connector = Hirose FH34SRJ-10S-0.5SH** | LS032B7DD02 tail is a 10-pin 0.5 mm FPC (device spec Table 4-1 / 8-2-1); this HRS part is a spec-recommended, dual-contact mate; 0.5 mm ZIF is hand-solderable → on-board (alts: FH28-10S-0.5SH, Molex 503480-1000, Panasonic AYF531035) |
| 2026-07-04 | **T/RH SHT40 → SHT45; + optional TMP117** | SHT45 hits **±0.1 °C / ±1 %RH** (R4 accuracy) and supplies SGP41's T/RH compensation; **TMP117** (±0.1 °C) added as an optional dedicated/RH-decoupled temp reference |
| 2026-07-04 | **Light VEML7700 → TSL2591** | 188 µlx–88 klx / 600M:1 range resolves **near-dark** (weak ambient light) for night auto-dim + 0-emission logic; VEML7700 kept as the lux-direct alt |
| 2026-07-04 | **RTC kept (RV-3028-C7 / DS3231SN)** | The ESP32-S3's own RTC loses time on full power loss (USB out **and** cell removed/dead) and is uncompensated; a coin-cell-backed RTC drives the hands to the correct absolute time **instantly at cold boot**, SNTP-disciplined. RV-3028-C7 = ULP 45 nA (breakout); DS3231SN = SOIC-16 TCXO on-board |
| 2026-07-04 | **RTC IC dropped → S3 RTC + 32.768 kHz crystal (no battery)** *(supersedes above)* | No 2nd battery wanted; a battery-less RTC chip doesn't survive power loss either, so it adds parts for nothing. The S3's internal RC drifts %-level over temp → a **32.768 kHz crystal** on XTAL32K gives ±~20 ppm (≈1.7 s/day), SNTP-disciplined. Total power loss (USB + cell gone) → lose time, re-sync on boot behind a clock animation |
