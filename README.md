# Wooden Smart Clock — Hardware Specification

> Living spec for a high-quality, low-power, two-part desk clock: a circular MCU-driven analog dial beside a reflective monochrome info panel.

**Status:** v0.7 draft · **Owner:** you (FW/HW) · **Last updated:** 2026-07-04

**Changelog**
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
| R1 | Accurate time (TZ, seconds, day, date), periodic server sync | TCXO/RTC + SNTP over Wi-Fi (§6, §8) |
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
- **Driver:** **DRV8835** (active) — 2×, one per shaft. *DRV8833 is NRND on DigiKey; TB6612FNG is an alternate.* **Homing Hall:** DRV5032 + magnet per shaft. **Libs:** SwitecX25 / GewoonGijs-VID28 / AccelStepper.

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
| Analog steppers (§5) | **DRV8835** via GPIO/PWM; **AccelStepper** / **SwitecX25** / VID28 lib; microstep for silence |
| Orientation + tap (R14, R3) | **BMA400** driver: gravity-vector orientation (flat/standing) + hardware tap IRQ |
| Wi-Fi | `esp_wifi` |
| BLE-pair → Wi-Fi provision (R9) | `wifi_provisioning` (BLE transport) + Espressif **"ESP BLE Provisioning"** phone app |
| BLE stack | **NimBLE** |
| NTP + TZ/DST (R1) | `esp_netif_sntp` + POSIX `TZ` string (`setenv`/`tzset`) |
| Env sensors (R4) | Sensirion `embedded-i2c-sht4x / sgp41 / scd4x / sps30`; Bosch **BSEC/BME68x** |
| Light / RTC | VEML7700 (esp-idf-lib / Adafruit); RV-3028 lib or DS3231 `RTClib` |
| Audio out (I²S) | `esp_driver_i2s`; decode via **ESP-ADF** (MP3/AAC/FLAC/WAV) or Arduino **ESP32-audioI2S** |
| Halo + front-light | `led_strip` (RMT/SPI, SK6812) + `ledc` (PWM dimming) |
| SD + assets | `esp_vfs_fat` + `sdmmc`; `LittleFS` for internal flash |

**Fastest bring-up:** **ESPHome** gets sensors / SNTP / addressable LED / I²S audio running in an afternoon; the memory LCD + steppers likely need a custom component or a drop to ESP-IDF. **esp-bsp** has ready board+display+LVGL configs.
**Caveat:** nPM1300 driver support is Nordic/Zephyr-centric — on ESP32-S3 use **BQ25185 + MAX17048** (mature Arduino/IDF libs) instead.

**RTOS/lang:** C/C++ + FreeRTOS (ESP-IDF). MicroPython for quick experiments only.

---

## 7. Audio subsystem (R3, R11)

- **Amp:** **MAX98357A** (I²S, 3.2 W @ 4 Ω/5 V, zero config — loud enough for an alarm) or **TAS5825M** (I²S + I²C DSP EQ/limiter/loudness — "close to a BT speaker", louder).
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
| Amp | MAX98357A ~10×15 mm / TAS5825M ~15×20 mm + LC filter | on main PCB |

**Loudness:** MAX98357A + DMA58-4 ≈ 85–90 dB @ 0.5 m — plenty for an alarm, pleasant at desk distance. **Rule:** plan a body **≥45–60 mm deep** with a dedicated ~100–250 cc sealed chamber. Vent nothing from the chamber toward the AQ sensors.

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

**Chosen: Dayton DMA58-4** in a sealed **~100–250 cc** chamber (Fs 165.5 Hz → sealed roll-off ~200–260 Hz; add the matching **DMA58-PR** passive radiator for more low-end), driven by **TAS5825M** (DSP EQ, recommended) or MAX98357A. Body depth ≥45–60 mm follows from this. **Dayton PC68-4** (2.5″, lower Fs, more bass) is the bigger-box alt; **Adafruit 3351** the compact/budget fallback. *(Speaker is from Parts Express, not DigiKey.)*

---

## 8. Sensors (R4, R14)

| Function | Part | Notes | ~Cost |
|----------|------|-------|-------|
| Temp + humidity | **Sensirion SHT4x** | best-in-class, low power | $2–4 |
| VOC (+NOx) index | **Sensirion SGP41** | pairs with SHT4x | $5–8 |
| *All-in-one alt* | **Bosch BME688** | T/RH/pressure/gas; BSEC lib | $8–12 |
| CO₂ *(opt)* | **Sensirion SCD41** | true NDIR CO₂ | $18–30 |
| PM2.5 *(opt)* | **Sensirion SPS30** | has fan; power/size | $30–45 |
| Ambient light | **Vishay VEML7700** | auto-dim halo + front-light | $1.5–3 |
| **Motion / tap / orientation** | **Bosch BMA400** | nA-class, hardware tap engine **+ gravity-vector orientation (flat/standing, R14)** | $1.5–2.5 |
| RTC (holdover) | **RV-3028-C7** (ULP) or **DS3231** | coin-cell backup, SNTP-disciplined | $2–6 |

A single 3-axis accel (**BMA400**) covers **tap-to-snooze *and* orientation** — no gyro needed, since flat-vs-standing is a static gravity reading. All I²C on a shared bus (§14). **Vent AQ sensors to outside air, away from amp/LEDs/battery** — their heat skews T/RH/VOC.

---

## 9. Lighting (R2, R3)

- **No backlight** — the info panel is reflective (0 emission when off, daylight-readable).
- **Optional display front-light:** edge white-LED front-light (the LS032 front-light variant) for **on-demand** night reading; ALS-gated, warm dim, **hard-off by default** → 0 emission at night.
- **Halo:** **SK6812 RGBW** (warm-white channel) or APA102/SK9822 (SPI, deterministic) along the base; frosted diffuser; sunrise = warm-white ramp over N min before audio; also serves as the night-light.
- *Optional:* subtle edge-lit **analog-dial** illumination if hands need night visibility — off by default.

---

## 10. Power (R5)

- **Input:** USB-C.
- **Charge/path/gauge:** ESP path → **BQ25185** (charger + power-path) + **MAX17048** (fuel gauge). ST path → **nPM1300** (all-in-one). Discrete alt: BQ24074.
- **Battery:** single-cell Li-ion/LiPo pouch (flat), 2000–3000 mAh.
- **Rails:** 3V3 main; gated boost for **halo + optional front-light** on battery (run off USB 5 V when plugged).

---

## 11. Storage (R9, R11)

- **microSD** (SDMMC) — user tones, images, widget assets.
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

4-layer min (6 if dense); solid ground plane, RF keep-out (or use a module w/ onboard antenna to inherit certs). **3-wire SPI** to the memory LCD is short and low-bandwidth — **no length-matching needed** (the RGB pixel-clock group is gone with the bar TFT). **Two stepper shafts** via 2× DRV8835 — keep coil traces + PWM away from RF/audio; place a **home Hall (DRV5032)** at each shaft. Class-D + speaker return away from RF/analog. Copper for charger/amp heat. FPC/ZIF for the display, JST for battery/speaker/LED/movement, SD cage, USB-C w/ ESD+CC.

---

## 16. Shopping list (DigiKey)

> DigiKey MPN / part links — click to verify live stock/price. Prices approximate, single-unit. Wood/enclosure excluded. Parts marked ✅ verified active on DigiKey; others use a DigiKey keyword search — confirm the exact orderable variant. Core-part datasheets + a summary table live in [`/datasheet`](datasheet/).

### 16a. Prototyping kit (fast start — dev boards + breakouts)

| Item | MPN | ~Price | Source |
|------|-----|--------|--------|
| SoC dev board (16 MB / 8 MB PSRAM) | ESP32-S3-DevKitC-1-N16R8 | ~$18 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=ESP32-S3-DevKitC-1-N16R8) |
| Info display (reflective MIP) | Sharp LS032B7DD02 | ~$39 | [DigiKey 23349498 ✅](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) |
| Analog movement (dual shaft) | Juken X40.879 | ~$14 | [DigiKey 28528329 ✅](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) |
| Motor driver carrier (×1–2) | Pololu DRV8835 (#2135) | ~$4 | [DigiKey 10450429](https://www.digikey.com/en/products/detail/pololu/2135/10450429) |
| Home Hall (×2) | DRV5032 breakout / bare | ~$1 | [DigiKey 7400094 ✅](https://www.digikey.com/en/products/detail/texas-instruments/DRV5032FADBZR/7400094) |
| I²S amp breakout | Adafruit 3006 (MAX98357A) | ~$6 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%203006) |
| Speaker (2″ full-range) | Dayton DMA58-4 | ~$19 | *(Parts Express 295-582 — not DigiKey)* |
| T/RH breakout | Adafruit 5776 (SHT40) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%205776) |
| VOC breakout | Adafruit 4829 (SGP40) | ~$15 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%204829) |
| Light breakout | Adafruit 4162 (VEML7700) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=Adafruit%204162) |
| Accel breakout (tap+orient) | BMA400 breakout (or Adafruit 2809 LIS3DH) | ~$5 | [DigiKey search](https://www.digikey.com/en/products/result?keywords=BMA400) |
| RGBW halo strip | SK6812 RGBW strip | ~$8 | *(Adafruit / AliExpress)* |

### 16b. Production BOM (ICs, custom PCB)

| Block | MPN | Size / pkg | ~Unit | Active | DigiKey ref |
|-------|-----|-----------|-------|--------|-------------|
| SoC module | ESP32-S3-WROOM-1-N16R8 | 18×25.5 mm | ~$6.8 | ✅ | [16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) |
| Info display | Sharp LS032B7DD02 | 3.16″, 47×76 mm | ~$39 | ✅ | [23349498](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) |
| Display FPC connector | (match panel FPC pitch) | — | ~$0.5 | — | [search](https://www.digikey.com/en/products/result?keywords=FPC%20connector) |
| Analog movement | Juken X40.879 (dual shaft) | vertical, compact | ~$14 | ✅ | [28528329](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) |
| Motor driver ×2 | DRV8835DSSR | 12-WSON | ~$1.3 | ✅ | [3088201](https://www.digikey.com/en/products/detail/texas-instruments/DRV8835DSSR/3088201) |
| Home Hall ×2 | DRV5032FADBZR | SOT-23 | ~$0.6 | ✅ | [7400094](https://www.digikey.com/en/products/detail/texas-instruments/DRV5032FADBZR/7400094) |
| Audio amp (simple) | MAX98357AETE+T | TQFN-16 | ~$2.5 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=MAX98357AETE%2BT) |
| Audio amp (DSP alt) | TAS5825MRHBR | VQFN-32 | ~$5 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=TAS5825MRHBR) |
| T/RH | SHT40-AD1B-R2 | DFN-4 | ~$3 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SHT40-AD1B-R2) |
| VOC | SGP41-D-R4 | DFN-6 | ~$6 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SGP41-D-R4) |
| Light | VEML7700-TR | OPLGA | ~$2 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=VEML7700-TR) |
| Accel (tap+orient) | BMA400 | LGA-12 | ~$2 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=BMA400) |
| RTC | RV-3028-C7 | C7 pkg | ~$3 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=RV-3028-C7) |
| PD sink | STUSB4500QTR | QFN-24 | ~$2 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=STUSB4500QTR) |
| Charger + power-path | BQ25185YBGR | DSBGA | ~$1.5 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=BQ25185YBGR) |
| Fuel gauge | MAX17048G+T10 | µDFN | ~$1.5 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=MAX17048G%2BT10) |
| Halo LEDs ×~16 | SK6812MINI-E | 3.5×3.5 mm | ~$0.2 ea | — | *(Adafruit / alt distributor)* |
| Front-light (opt) | white LED + FET/CC driver | — | ~$0.5 | — | — |
| microSD socket | Hirose DM3AT-SF-PEJM5 | push-push | ~$1 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=DM3AT-SF-PEJM5) |
| USB-C recept | GCT USB4085-GF-A | — | ~$0.8 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=USB4085-GF-A) |
| *CO₂ (opt)* | SCD41-D-R2 | — | ~$24 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SCD41-D-R2) |
| *PM2.5 (opt)* | SPS30 | — | ~$38 | ✅ | [search](https://www.digikey.com/en/products/result?keywords=SPS30) |
| Speaker driver | Dayton DMA58-4 (2″ FR) | 56×56×32 mm | ~$19 | — | *(Parts Express 295-582)* |
| Passive radiator *(opt)* | Dayton DMA58-PR (2″) | — | ~$8 | — | *(Parts Express)* |

**Core electronics subtotal (excl. speaker/battery/PCB): ~$110–130** (the analog movement + drivers + Halls add ~$18 vs the display-only build). With speaker + Li-ion pouch + 4-layer PCB + passives ≈ **~$180–210**. +CO₂/PM ≈ +$62.

**Cost/space levers:** budget movement (VID28-05, −$10, off-DigiKey) + Hall homing; MAX98357A vs TAS5825M (−$2.5, no DSP); skip CO₂/PM; the display + movement are the two big line items.

---

## 17. Alternatives quick-reference

- **MCU:** ESP32-S3 ⭐ single-chip / STM32U5 + ST67W611M1 (ULP) / nRF5340 + nRF7002 (Nordic ULP) / RW612 (single-chip) / ESP32-P4 + C6 (future MIPI).
- **Info display:** Sharp **LS032B7DD02** reflective MIP ⭐ / mono OLED (burn-in, emits) / square IPS TFT (backlit) / EPD (ghosts on seconds). *Superseded: wide color bar-TFT (NHD-3.9).*
- **Analog movement:** Juken **X40.879** (DigiKey, 75 mm) ⭐ / Juken X10.506 (small, built-in homing) / VID28-05·BKA30D-R5 (budget, off-DigiKey) / X27.168 ×2 (single-shaft).
- **Motor driver:** DRV8835 ⭐ / TB6612FNG. *(DRV8833 NRND.)*
- **Amp:** MAX98357A ⭐ (simple) / TAS5825M (DSP).
- **Power:** BQ25185 + MAX17048 (ESP) / nPM1300 (ST).
- **RTC:** RV-3028-C7 (ULP) / DS3231 (simple).
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
