# Wooden Smart Clock — Hardware Specification

> Living spec for a high-quality, low-power, wide-format desk clock.

**Status:** v0.5 draft · **Owner:** you (FW/HW) · **Last updated:** 2026-07-02

**Changelog**
- v0.5 — locked **Dayton PC68-4** speaker; added **Juken X10.506** + more movements; new requirement **R12** (MCU sets/adjusts analog time → sweep-quartz demoted).
- v0.4 — added **speaker comparison** (§8b) and **analog-movement comparison** for the split-face option (§22: analog dial + MIP).
- v0.3 — added **ESP32-S3** single-chip path + firmware libraries (§7c), **audio sizing** (§8a), **internal packaging** layout (§21, see `packaging_layout.svg`), and a **Mouser-linked shopping list** (§17).
- v0.2 — display moved E-Paper → fast **bar TFT** (NHD-3.9); MCU → STM32U5 (LTDC) + ST67W611M1, with ESP32-S3 as single-chip alt.
- v0.1 — initial spec (EPD + nRF5340).

---

## 1. Requirements (source of truth)

| # | Requirement | Primary approach (see §) |
|---|-------------|--------------------------|
| R1 | Accurate time (TZ, seconds, day, date), periodic server sync | TCXO RTC + SNTP over Wi-Fi (§7, §9) |
| R2 | Dimmable + auto backlight, **true 0% off at night**, ambient light sensor | Backlight w/ hard-off enable + ALS (§10) |
| R3 | Alarm, tap-to-snooze, loud multi-asset audio, sunrise halo from bottom | Accel tap-detect + Class-D audio + RGBW halo (§8, §10) |
| R4 | Air quality (VOC/AQI), temperature, humidity | SHT4x + SGP41 (or BME688); optional CO₂/PM (§9) |
| R5 | USB-C powered, battery backup + charge, run untethered | USB-C PD sink + charger + Li-ion + fuel gauge (§11) |
| R6 | Programmable, low-power, **second-refresh, no ghosting, 0 light when off** | Fast bar TFT (backlight off = dark); MIP alt (§6) |
| R7 | Wi-Fi + BLE (sync + API data: weather, stocks, …) | ESP32-S3 (single chip) or STM32U5 + ST67W611M1 (§7) |
| R8 | Minimal rear buttons, minimal on-device config | 2–3 rear tactiles + tap; rotation dial; config via app (§13) |
| R9 | Companion iOS/web app: BLE pair → Wi-Fi provision, alarms, tone upload, widgets | BLE provisioning + SD asset store (§14) |
| R10 | Extensible: more sensors, multiple sizes | I²C sensor bus + FPC/B2B connectors + modular FW (§15) |
| R11 | High-quality speaker + high-quality assets | Good full-range driver + DSP amp + WAV/FLAC on SD (§8) |
| R12 | MCU fully sets/adjusts the analog time (timezone/DST jumps, NTP-synced, drift-corrected) | Bidirectional, absolutely-addressable stepper movement + homing (§22) |

---

## 2. Key design decisions & open tensions

The old "seconds vs E-Paper ghosting" tension is resolved — a **fast, ghost-free** display was chosen; seconds tick freely. Remaining tension is **reflective vs wide**:

- **Wide + backlit (chosen): bar TFT.** Fits the look, full color/gray, fast, no ghost, no burn-in. Backlight **fully off = zero emitted light** at night. Cost: needs backlight on to be seen; always draws backlight power when visible.
- **Reflective + small: MIP (Sharp/JDI).** Sunlight-readable, µW, fast, no ghost, emits nothing — but **max 4.4″**, so no wide bar. Does need additional light for night readability?
- **E-Paper: dropped** for ticking seconds (slow + ghosting/wear).

**Baked-in:** 0-light via backlight **hard-enable** (not just PWM=0); two light systems (backlight + RGBW halo); split power domains (always-on low-power + bursty Wi-Fi);

---

## 6. Display

### 6a. Decision & constraints
Hard constraints (R6): **second-refresh, no ghosting, 0 light when off.** Pass: MIP, transflective TFT, backlit TFT, OLED. E-Paper fails (slow/ghosts); OLED out (burn-in on a static clock). Width (20–30 cm) forces a **TFT** (MIP maxes at 4.4″).

| Tech | 0-light | Sec-refresh | Ghost / burn-in | Reflective | Wide bar? | Verdict |
|------|---------|-------------|-----------------|------------|-----------|---------|
| **Bar TFT (IPS)** ⭐ | backlight OFF | ✅ | none / none | ❌ | ✅ | **Primary** |
| MIP (Sharp/JDI) | reflective | ✅ | none / none | ✅ | ❌ ≤4.4″ | Small/future |
| Transflective TFT | backlight OFF | ✅ | none / none | ⚠️ dim | ✅ | If daylight-reflective wanted |
| OLED | pixels off | ✅ | none / **burn-in** | ❌ | ⚠️ | Rejected |
| E-Paper | reflective | ❌ | **ghosts** | ✅ | custom | Rejected (seconds) |

### 6b. Making it not look cheap
Optically bond a cover glass (OCA) → deep black, no air gap; black-on-black UI under edge-to-edge glass w/ printed border mask (bezel disappears); AR/anti-glare; perfectly even backlight; auto-dim + warm night dim; restrained variable-font typography, single accent; tear-free 60 fps (double-buffer).

---

## 7. MCU + wireless

**"One STM32 for RGB + Wi-Fi + BLE?"** No — no STM32 integrates Wi-Fi, and the BLE parts (WB/WBA) have no LTDC. Options:

| Option | Wi-Fi + BLE | Drives RGB panel | Low power | Notes | Verdict |
|--------|-------------|------------------|-----------|-------|---------|
| **ESP32-S3** ⭐ single-chip | Wi-Fi 4 (2.4G) + BLE 5 | ✅ LCD_CAM + PSRAM | good deep-sleep, higher idle | cheapest, fastest bring-up, huge lib support (§7c) | **Primary (this build)** |
| **STM32U5F9/G9 + ST67W611M1** | Wi-Fi 6 + BLE 5.4 (SPI) | ✅ LTDC + NeoChromVG | **excellent** | ST ref design; 3 MB SRAM; dual-band | **ST low-power alt** |
| nRF5340 + nRF7002 + BT817 EVE | Wi-Fi 6 + BLE 5.x | ✅ via EVE (SPI) | **best radio power** | keeps Nordic familiarity; +1 chip | Nordic-native alt |
| NXP RW612 single-chip | Wi-Fi 6 dual-band + BLE 5.4 + Thread | ❌ 8080/SPI only | good | needs a *smart* 8080/SPI panel, not raw RGB | if switching panel |
| ESP32-P4 + C6 | Wi-Fi 6 + BLE (C6) | ✅ RGB + **MIPI-DSI** | med | drives future 8.8″ MIPI bar; 2 chips | future/big-display |

### 7c. ESP32-S3 quick-start libraries
Base: **ESP-IDF v5.x** (C, production, best power control) or **Arduino-ESP32 / PlatformIO** (fast prototyping).

| Requirement | Library / component |
|---|---|
| RGB panel | `esp_lcd` → `esp_lcd_new_rgb_panel()` (framebuffer in PSRAM, bounce buffers) |
| GUI / widgets | **LVGL v9** + `esp_lvgl_port`; Arduino alt: **LovyanGFX** / **Arduino_GFX** |
| Wi-Fi | `esp_wifi` |
| BLE-pair → Wi-Fi provision (R9) | `wifi_provisioning` (BLE transport) + Espressif **"ESP BLE Provisioning"** phone app (ready-made pairing UX) |
| BLE stack | **NimBLE** |
| NTP + TZ/DST (R1) | `esp_netif_sntp` + POSIX `TZ` string (`setenv`/`tzset`) |
| Env sensors (R4) | Sensirion `embedded-i2c-sht4x / sgp41 / scd4x / sps30`; Bosch **BSEC/BME68x**; **BMA400** driver |
| Light / RTC | VEML7700 (esp-idf-lib / Adafruit); DS3231 `RTClib` or RV-3028 lib |
| Audio out (I²S) | `esp_driver_i2s`; decode via **ESP-ADF** (MP3/AAC/FLAC/WAV) or Arduino **ESP32-audioI2S** |
| Halo + backlight | `led_strip` (RMT/SPI, SK6812) + `ledc` (PWM dimming) |
| SD + assets | `esp_vfs_fat` + `sdmmc`; `LittleFS` for internal flash |

**Fastest possible bring-up:** **ESPHome** (YAML) already bundles LVGL, SHT4x/SGP4x/SCD4x/BME680/VEML7700, SNTP, addressable light, and an I²S `media_player` — clock running in an afternoon, then graduate to ESP-IDF. **esp-bsp** has ready board+display+LVGL configs.
**Caveat:** nPM1300 driver support is Nordic/Zephyr-centric — on ESP32-S3 use **BQ25185 + MAX17048** (mature Arduino/IDF libs) instead.

**RTOS/lang:** C/C++ + FreeRTOS (ESP-IDF). MicroPython for quick experiments only.

---

## 8. Audio subsystem (R3, R11)

- **Amp:** **MAX98357A** (I²S, 3.2 W @ 4 Ω/5 V, zero config — the easy default; loud enough for an alarm) or **TAS5825M** (I²S + I²C DSP EQ/limiter/loudness — "close to a BT speaker", louder).
- **Chain:** ESP32-S3 → **I²S** (3 pins: BCLK, LRCLK, DOUT) → amp → speaker. ✅ exactly as you assumed.
- **Source:** WAV (trivial) / FLAC (decoder + CPU) on microSD; system sounds in flash.
- **Tap-to-snooze:** accel hardware tap IRQ (§9) so the MCU sleeps until tapped.

### 8a. Sizing (what "close to a BT speaker" costs in space)
The small display does **not** limit audio — the speaker lives behind/beside it in the wood body. Quality comes from **enclosure depth + a sealed air chamber**, not display area.

| Item | Spec | Footprint / volume |
|---|---|---|
| Driver | **2″ (50 mm) full-range, 4 Ω, 3–5 W** (Dayton CE / Tang Band W2 class) | ~55 mm dia × ~25 mm deep |
| Bass helper | passive radiator (optional, big low-end gain) | ~2″, ~20 mm deep |
| **Sealed chamber** | **~250–400 cc** sealed, or ~150–250 cc w/ passive radiator | dominant space cost → sets min body depth ~40 mm |
| Amp | MAX98357A ~10×15 mm / TAS5825M ~15×20 mm + LC filter | on main PCB |

**Loudness:** MAX98357A + 2″ driver ≈ 85–90 dB @ 0.5 m — plenty for an alarm, pleasant at desk distance. **Rule:** to sound good, plan a body **≥60–80 mm deep** with a dedicated ~250 cc+ sealed chamber. Body volume buys the sound, independent of the small screen.

### 8b. Speaker options — comparison

| Option | Type | Size (mm) | Power / Ω | Application | ~Cost | Pros | Cons |
|--------|------|-----------|-----------|-------------|-------|------|------|
| **Adafruit 3351** v1 | Mono, enclosed | 30×70×17 | 3 W / 4 Ω | alarm, voice, casual music | $4 | plug-and-play, tiny, JST, sealed | thin, ~no bass, modest SPL |
| Adafruit 1669 | Stereo enclosed set | 2× ~30×70 | 3 W / 4 Ω | casual stereo, chimes | $8 | stereo, enclosed | small, limited bass |
| **Dayton PC68-4** ⭐ **chosen** | 2.5″ full-range driver | Ø66 × ~30 | ~15 W / 4 Ω, 83 dB | quality desk audio | $9 | real driver, great value | needs ~250 cc chamber + baffle |
| Tang Band W2-series | 2″ full-range (premium) | Ø~57 | ~15 W / 4–8 Ω | premium music | $25–40 | best small-driver fidelity | pricey, needs box |
| Driver + passive radiator | 2.5″ FR + PR | Ø66 + PR | — | best "BT-speaker" feel | $15–25 | real bass in a small box | 2 parts + box tuning |
| 3″ full-range (Visaton FRS8) | 3″ driver | Ø80 | 20–30 W | if depth allows | $12–20 | fullest, loudest | biggest footprint |

All pair with **MAX98357A** (I²S) or **TAS5825M** (DSP). **Chosen: Dayton PC68-4** in a sealed **~250–400 cc** chamber (Fs 117 Hz → sealed roll-off ~150 Hz; add a passive radiator or small port for more low-end), driven by **TAS5825M** (DSP EQ, recommended) or MAX98357A. Body depth ≥60–80 mm follows from this. **Adafruit 3351** stays the compact/budget fallback.

---

## 9. Sensors

| Function | Part | Notes | ~Cost |
|----------|------|-------|-------|
| Temp + humidity | **Sensirion SHT4x** | best-in-class, low power | $2–4 |
| VOC (+NOx) index | **Sensirion SGP41** | pairs with SHT4x | $5–8 |
| *All-in-one alt* | **Bosch BME688** | T/RH/pressure/gas; BSEC lib | $8–12 |
| CO₂ *(opt)* | **Sensirion SCD41** | true NDIR CO₂ | $18–30 |
| PM2.5 *(opt)* | **Sensirion SPS30** | has fan; power/size | $30–45 |
| Ambient light | **Vishay VEML7700** | auto-brightness | $1.5–3 |
| Motion / tap | **Bosch BMA400** | nA-class, tap engine | $1.5–2.5 |
| RTC (holdover) | **RV-3028-C7** (ULP) or **DS3231** | coin-cell backup, SNTP-disciplined | $2–6 |
| Accel/Gyro | TBD | device orientation and tap | TBD |

All I²C on a shared bus (§15). **Vent AQ sensors to outside air, far from amp/LEDs/battery** — their heat skews T/RH/VOC (§21).

---

## 10. Lighting (R2, R3)

- **Backlight:** CC LED driver with **enable pin** (hard-off = 0 emission at night); PWM dim from VEML7700; warm night dim. *(MIP path: reflective + optional edge front-light.)*
- **Halo:** **SK6812 RGBW** (warm-white channel) or APA102/SK9822 (SPI, deterministic) along the base; frosted diffuser; sunrise = warm-white ramp over N min before audio.

---

## 11. Power (R5)

- **Input:** USB-C. 
- **Charge/path/gauge:** ESP path → **BQ25185** (charger + power-path) + **MAX17048** (fuel gauge). ST path → **nPM1300** (all-in-one). Discrete alt: BQ24074.
- **Battery:** single-cell Li-ion/LiPo pouch (flat), 2000–3000 mAh.
- **Rails:** 3V3 main; gated boost for backlight/halo on battery (run off USB 5 V when plugged).

---

## 12. Storage (R9, R11)

- **microSD** (SDMMC) — user tones, images, widget assets.
- **Flash** (module 16 MB + optional external OSPI NOR) — fonts, glyph atlases, system sounds, OTA, config.

---

## 13. Inputs / buttons (R8)

- **2–3 rear tactiles:** `POWER/WAKE`, `PAIR/MENU` (long-press = pair / factory reset), optional `SNOOZE/MUTE`.
- **360 deg rotary encoder dial:** menu adjustments, set clock, set alarm, etc.
- **Tap-to-snooze** via accel (top-tap) → front button-free.

---

## 14. Companion app (R9)

BLE pair → GATT provisioning → push Wi-Fi creds → join AP. Configure alarms, sunrise, brightness, TZ, widgets/APIs. Upload tones (→ SD) + layouts/fonts (→ flash) over BLE or Wi-Fi. iOS app and/or web (Web Bluetooth). ESP path: reuse Espressif's provisioning app for v1.
QR code for quick setup?

---

## 15. Extensibility (R10)

Shared **I²C** (Qwiic/STEMMA-QT) for drop-in sensors; display driver behind an LVGL geometry config → one codebase for small (MIP) / wide (TFT) / future 8.8″ MIPI; display, LED, speaker on FPC/B2B connectors; reusable compute+radio+power core block.

---

## 16. PCB considerations

4-layer min (6 if dense); solid ground plane, RF keep-out (or use a module w/ onboard antenna to inherit certs). Length-match the RGB pixel-clock group; keep off RF/audio. Class-D + speaker return away from RF/analog. Copper for charger/amp/boost heat. FPC/ZIF for TFT, JST for battery/speaker/LED, SD cage, USB-C w/ ESD+CC.

---

## 17. Shopping list (Mouser links)

> Mouser search links by MPN — click to verify live stock/price. Prices approximate, single-unit. Wood/enclosure excluded.

### 17a. Prototyping kit (fast start — dev boards + breakouts)

| Item | MPN | ~Price | Mouser |
|------|-----|--------|--------|
| SoC dev board (16 MB flash / 8 MB PSRAM) | ESP32-S3-DevKitC-1-N16R8 | ~$18 | https://www.mouser.com/c/?q=ESP32-S3-DevKitC-1-N16R8 |
| Display (bar TFT, RGB) | NHD-3.9-480128AF-ASXP | ~$30 | https://www.mouser.com/c/?q=NHD-3.9-480128AF-ASXP |
| I²S amp breakout | Adafruit 3006 (MAX98357A) | ~$6 | https://www.mouser.com/c/?q=Adafruit%203006 |
| Speaker (2″ full-range) | Dayton Audio CE-series / Tang Band W2 *(Parts Express)* | ~$8–20 | *(Parts Express — not Mouser)* |
| T/RH breakout | Adafruit 5776 (SHT40) | ~$5 | https://www.mouser.com/c/?q=Adafruit%205776 |
| VOC breakout | Adafruit 4829 (SGP40) | ~$15 | https://www.mouser.com/c/?q=Adafruit%204829 |
| Light breakout | Adafruit 4162 (VEML7700) | ~$5 | https://www.mouser.com/c/?q=Adafruit%204162 |
| Accel breakout | Adafruit 4770 (LIS3DH) or BMA400 | ~$5 | https://www.mouser.com/c/?q=Adafruit%204770 |
| RTC breakout | Adafruit 3013 (DS3231) | ~$14 | https://www.mouser.com/c/?q=Adafruit%203013 |
| RGBW halo strip | SK6812 RGBW strip | ~$8 | https://www.mouser.com/c/?q=SK6812%20RGBW |

### 17b. Production BOM (ICs, custom PCB)

| Block | MPN | ~Unit | Mouser/Digikey |
|-------|-----|-------|--------|
| SoC module | ESP32-S3-WROOM-1-N16R8 | ~$5 | https://www.mouser.com/c/?q=ESP32-S3-WROOM-1-N16R8 |
| Display | LS032B7DD02 | ~$39 | https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498 (46.02mm W x 73.20mm H) |
| Audio amp (simple) | MAX98357AETE+T | ~$2.5 | https://www.mouser.com/c/?q=MAX98357AETE%2BT |
| Audio amp (DSP alt) | TAS5825MRHBR | ~$5 | https://www.mouser.com/c/?q=TAS5825MRHBR |
| T/RH | SHT40-AD1B-R2 | ~$3 | https://www.mouser.com/c/?q=SHT40-AD1B-R2 |
| VOC | SGP41-D-R4 | ~$6 | https://www.mouser.com/c/?q=SGP41-D-R4 |
| Light | VEML7700-TR | ~$2 | https://www.mouser.com/c/?q=VEML7700-TR |
| Accel (tap) | BMA400 | ~$2 | https://www.mouser.com/c/?q=BMA400 |
| RTC | RV-3028-C7 | ~$3 | https://www.mouser.com/c/?q=RV-3028-C7 |
| PD sink | STUSB4500QTR | ~$2 | https://www.mouser.com/c/?q=STUSB4500QTR |
| Charger + power-path | BQ25185YBGR | ~$1.5 | https://www.mouser.com/c/?q=BQ25185 |
| Fuel gauge | MAX17048G+T10 | ~$1.5 | https://www.mouser.com/c/?q=MAX17048G%2BT10 |
| Backlight LED driver | TPS61165 (or CC driver) | ~$1 | https://www.mouser.com/c/?q=TPS61165 |
| Halo LEDs | SK6812MINI-E ×~16 | ~$0.2 ea | https://www.mouser.com/c/?q=SK6812MINI-E |
| microSD socket | Hirose DM3AT-SF-PEJM5 | ~$1 | https://www.mouser.com/c/?q=DM3AT-SF-PEJM5 |
| USB-C recept | GCT USB4085-GF-A | ~$0.8 | https://www.mouser.com/c/?q=USB4085-GF-A |
| *CO₂ (opt)* | SCD41-D-R2 | ~$24 | https://www.mouser.com/c/?q=SCD41-D-R2 |
| *PM2.5 (opt)* | SPS30 | ~$38 | https://www.mouser.com/c/?q=SPS30 |
| Speaker driver | 2″ full-range *(Parts Express: Dayton CE / Tang Band W2)* | ~$8–20 | *(Parts Express)* |
| Passive radiator | 2″ PR *(Parts Express / Mouser search)* | ~$4–8 | https://www.mouser.com/c/?q=passive%20radiator%20speaker |

**Core electronics subtotal (excl. speaker/battery/PCB): ~$95–110.** With speaker + Li-ion pouch + 4-layer PCB + passives ≈ **~$150–170**. +CO₂/PM ≈ +$62.

**Cost/space levers:** skip touch (−$20); MAX98357A vs TAS5825M (−$2.5, no DSP); skip CO₂/PM; display is the biggest variable ($30 bar TFT vs $28 MIP vs $60–130 8.8″ MIPI).

---

## 18. Alternatives quick-reference

- **MCU:** ESP32-S3 ⭐ single-chip / STM32U5F9-G9 + ST67W611M1 (ULP) / nRF5340+nRF7002+BT817 EVE / RW612 (needs 8080 panel) / ESP32-P4+C6 (future MIPI).
- **Display:** NHD-3.9 bar TFT ⭐ / Sharp-JDI MIP / 8.8″ 1920×480 MIPI / EPD (on-demand-seconds variant).
- **Amp:** MAX98357A ⭐ (simple) / TAS5825M (DSP).
- **Power:** BQ25185+MAX17048 (ESP) / nPM1300 (ST).
- **RTC:** RV-3028-C7 (ULP) / DS3231 (simple).

## 22. Analog movement — split-face option (analog dial + MIP)

Design: left = physical **analog dial (hour + minute hands)**; right = **MIP** (Sharp LS027B7DH01A 2.7″ 400×240) for date/weather/stock/seconds. Silent, reflective, emits nothing at night — the most premium-looking option and more space-efficient than the wide RGB.

**New requirement (R12):** the MCU must **set/adjust the displayed time on demand** — jump for timezone/DST, correct NTP drift, both directions. That needs a **bidirectional, absolutely-addressable** movement with a **home reference**, which rules out the forward-only sweep-quartz and favors stepper movements (ideally with built-in zero detection).

**Motor comparison** — must do **360° on *both* hands**, be **extremely quiet**, use **light hands**, and let the **MCU set any absolute time** (R12):

| Motor | Type · 360° both? | MCU sets abs. time (bidir + homing) | Torque → hand weight | Noise | Size | Power | ~Cost | Where |
|-------|-------------------|-------------------------------------|----------------------|-------|------|-------|-------|-------|
| **Juken X10.506** ⭐ | Dual-shaft concentric, purpose-built **clock** motor (hour+minute) · ✅ (no-stop ver.) | ✅✅ **zero-detection variant = built-in homing**, bidirectional | low, indicator-class (~1–2 mN·m; see SP-X10 datasheet) → light hands | very quiet (Swiss, microstep) | small (~Ø28 mm class) | ~15–20 mA | ~$11 | Minitools / cars-equipment / DigiKey (Juken) |
| **VID28-05 / BKA30D-R5** | Dual-shaft concentric · ✅ (**buy 360° variant!**) | ✅ bidirectional; **add external Hall home** | ~4 mN·m → ≤~10 g @3 cm unbal.; ≤2–3 g balanced | microstep ~20–30 dBA; full-step ~45 dBA | 29×59 mm | ~15–20 mA/coil | $4–6 | AliExpress/eBay/Amazon |
| Juken X40.879 (premium) | Dual-shaft concentric, higher torque · ✅ | ✅ (homing variant) | higher → heavier hands OK | very quiet, smooth | larger | low | ~$20 | Minitools / DigiKey / mfr |
| Sonceboz 6407 | Dual-shaft, automotive-grade · ✅ | ✅ | higher, smooth | very quiet | ~30 mm | low | $$ | mfr direct (MOQ) |
| Juken/Switec X27.168 / X25.168 | **Single**-shaft gauge · ⚠️ 315° (remove stop) | ✅ bidirectional, but single shaft → **need 2 + coax mechanics** | ~0.6–1 mN·m (weakest) | microstep quiet | Ø26 mm | ~20 mA | $4–8 | DigiKey / Adafruit |
| Continuous-sweep quartz movement | Geared clock · ✅ | ❌ **forward-only + coupled → can't jump for TZ/DST** | strong (real hands) | **silent <20 dBA** | 56×56×15 mm | µA–mA | $3–10 | clock-parts shops |
| 28BYJ-48 geared stepper | Single-shaft geared · ✅ | ✅ electrically | strong (~34 mN·m) | **buzzy 45–55 dBA ✗** | 28 mm+box | 100–200 mA | $2 | everywhere |

**Picks (with R12):**
- **Best / purpose-built: Juken X10.506** — dual concentric hour+minute; the **zero-detection variant gives automatic homing**, so the MCU drives both hands to any absolute time (TZ/DST/NTP), both directions, Swiss-quiet. ⭐
- **Budget: VID28-05 / BKA30D-R5** — same concept; add a Hall (DRV5032) + magnet home index per shaft.
- **Premium / higher torque: Juken X40.879 or Sonceboz 6407.**
- **Now demoted:** sweep-quartz **fails R12** (forward-only, coupled); 28BYJ-48 stays rejected (noise).

**Design notes:**
- Prefer a **zero-detection (homing) variant**: on boot and after each NTP sync, home both hands, then step to the exact time; TZ/DST = step to the new position (fast, both directions).
- No built-in homing (VID28/BKA30D) → add a **Hall DRV5032 + magnet** or slotted opto per shaft.
- Buy the **360°/no-stop** version; **microstep @ >20 kHz PWM** for silence; **light counterbalanced hands** (metal hands → premium/higher-torque motor).
- **Driver:** DRV8833 (Adafruit 3297) or ULN2803 per motor. **Libs:** SwitecX25 / GewoonGijs-VID28 / AccelStepper. **Seconds** live on the MIP.

---

### Decision log
| Date | Decision | Rationale |
|------|----------|-----------|
| 2026-06-30 | ~~EPD primary~~ (superseded) | Fails ticking seconds (ghost/wear) |
| 2026-07-02 | **Fast bar TFT (NHD-3.9) primary** | Second-refresh + no-ghost + 0-light; wide; on Mouser |
| 2026-07-02 | **ESP32-S3 as v1 MCU** (STM32U5+ST67W as ULP alt) | Single-chip drives RGB panel + Wi-Fi + BLE; fastest bring-up; huge lib support |
| 2026-07-02 | Body depth ≥60–80 mm w/ sealed ~250 cc chamber | Audio quality needs volume, not display area |
| 2026-07-02 | Premium finish is a spec item | Optical bonding + black-on-black + even backlight |
| _TBD_ | Bar TFT vs reflective MIP | pending §19.1 |
| _TBD_ | 3.9″ vs 8.8″; ESP32-S3 vs STM32U5 | pending §19.2–3 |
| 2026-07-02 | Split-face (analog dial + MIP) added as premium variant | Real hands + reflective MIP look less cheap than RGB; silent; dark at night |
| 2026-07-02 | Movement: VID28-05/BKA30D-R5 (indep) or sweep quartz (coupled) | 360° both hands, quiet (microstep/sweep), cheap; avoid 28BYJ-48 |
| 2026-07-02 | Speaker locked: **Dayton PC68-4** (2.5″ FR) in sealed chamber | Real driver quality; needs ~250–400 cc + ≥60–80 mm depth |
| 2026-07-02 | Added **R12** (MCU sets/adjusts analog time) | Requires bidirectional addressable movement + homing |
| 2026-07-02 | Movement lead → **Juken X10.506** (zero-detection variant) | Purpose-built dual clock motor w/ built-in homing → clean absolute time (R12); VID28/BKA30D = budget alt |
