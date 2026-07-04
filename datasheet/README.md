# Datasheet Summary

Quick-reference for the datasheets in this folder. Prices are single-unit USD and approximate — click through to verify live stock/price. All parts are **currently active/orderable** (verified 2026-07-04) and now **match the root [`README.md`](../README.md)**.

> 🔩 **The stepper ships as two files.** `stepper_motor_x40-879.pdf` is a 2-page **pinout addendum** for the dual-shaft **X40.879** (the motor we're buying); it explicitly defers to the **X27 base spec** (`stepper_motor_x27_base-spec.pdf`) for torque, current, and dimensions. Keep both.

| # | File | Part | Mfr | Active | ~Price | Interface |
|---|------|------|-----|--------|--------|-----------|
| 1 | `display_ls032b7dd02.pdf` | LS032B7DD02 (full device spec) | Sharp | ✅ | ~$38 | 3-wire SPI |
| 2 | `mcu_esp32-s3-wroom-1-n16r8.pdf` | ESP32-S3-WROOM-1-N16R8 | Espressif | ✅ | ~$6.8 | Wi-Fi/BLE + UART/SPI/I²C/I²S |
| 3 | `speaker_dma58-4.pdf` | DMA58-4 | Dayton Audio | ✅ | ~$19 | Analog (passive driver) |
| 4 | `stepper_motor_x40-879.pdf` | **X40.879** (dual-shaft) | Juken / Switec | ✅ | ~$14 | 2-phase bipolar × 2 |
| 4b | `stepper_motor_x27_base-spec.pdf` | X27 base spec *(companion to #4)* | Juken / Switec | ✅ | — | — |

---

## 1. Sharp LS032B7DD02 — Reflective Memory LCD

- **Product:** 3.16″ transflective monochrome Memory-in-Pixel (MIP) LCD module, 336 × 536.
- **Refs:** Part # `LS032B7DD02` · Mfr **Sharp** · DigiKey # 23349498 · spec `LD-2023X13` (full device spec, dated **01-Nov-2023**).
- **Price / link:** ~**$38.14** — [DigiKey 23349498](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) (active, in stock).
- **Dimensions (W × L × D):** **47.02 × 76.00 × 0.705 mm** (outline). Active area 42.672 × 68.072 mm; top polarizer 46.02 × 73.2 mm; dot pitch 0.127 mm; mass 5.5 g max.
- **Power:**
  - **Voltage:** VDD/VDDA recommended **4.8 / 5.0 / 5.5 V** (min/typ/max); absolute max −0.3…+5.8 V. Logic inputs (SCLK/SI/SCS/DISP/EXTCOMIN) high = **2.7 / 3.0 / VDD V** → **3.3 V-MCU compatible**.
  - **Power consumption:** HOLD (no update) **30 µW typ** (330 µW max); data update @ 1 Hz **250 µW typ** (750 µW max) — i.e. ≈6 µA hold / ≈50 µA update at 5 V. (Peak COM current is higher; size the rail with margin.)
- **Description:** Reflective/transflective monochrome MIP LCD — retains its image at near-zero power, daylight-readable, no backlight (a backlight can be added for all-ambient use). Response 30 ms, viewing angle 120°/120°, operating −20…+70 °C, storage −30…+80 °C. FPC connector.
- **Interface:** **3-wire SPI** (SCLK, SI, SCS) plus DISP / EXTCOMIN / EXTMODE control lines.
- **Released:** device spec © 2023 (rev `LD-2023X13`, 01-Nov-2023). *(This file replaced the earlier 2-page marketing brief.)*

## 2. Espressif ESP32-S3-WROOM-1-N16R8 — Wi-Fi + BLE MCU module

- **Product:** ESP32-S3 SoC module, dual-core Xtensa LX7 @ up to 240 MHz, **16 MB flash / 8 MB PSRAM** (the `N16R8` suffix), on-board PCB antenna.
- **Refs:** Part # `ESP32-S3-WROOM-1-N16R8` · Mfr **Espressif Systems** · DigiKey # 16162642.
- **Price / link:** ~**$6.76** — [DigiKey 16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) (active, ships today).
- **Dimensions (W × L × D):** **18.0 × 25.5 × 3.1 mm** (±0.2 / ±0.2 / ±0.15).
- **Power:**
  - **Voltage:** 3.0–3.6 V (typ 3.3 V). Absolute max 3.6 V, min −0.3 V. External supply must deliver ≥ 0.5 A.
  - **Current:** Wi-Fi TX peak up to **355 mA** (297 mA @ 54 Mbps); BLE TX peak up to **344 mA**; RX ~95 mA. Modem-sleep ~13–108 mA (freq/core dependent). Light-sleep **240 µA**; deep-sleep 7–170 µA; power-off **1 µA**.
- **Description:** Single-chip 2.4 GHz Wi-Fi 802.11 b/g/n + Bluetooth LE 5 module, up to 36 GPIO, rich peripheral set. (WROOM-1 = PCB antenna; the `-1U` variant swaps to an external-antenna connector.)
- **Interface:** **Wireless** — Wi-Fi + BLE 5. **Wired** — UART (default flashing/console) plus SPI, I²C, I²S, SDIO, etc. mux'd onto GPIO.
- **Released:** module ~2021 (ESP32-S3 SoC launched Dec 2020); datasheet v1.8.

## 3. Dayton Audio DMA58-4 — 2″ Full-Range Driver *(chosen speaker)*

- **Product:** 2″ dual-neodymium-magnet, aluminum-cone full-range driver, 4 Ω, open 8-spoke frame.
- **Refs:** Part # `DMA58-4` · Mfr **Dayton Audio** · Parts Express # 295-582 · Amazon ASIN B07L9JKSGV. **Not stocked on DigiKey.**
- **Price / link:** ~**$18.99** — [Parts Express 295-582](https://www.parts-express.com/Dayton-Audio-DMA58-4-2-Dual-Magnet-Aluminum-Cone-Full-Range-Driver-4-Ohm-295-582) · also [Amazon](https://www.amazon.com/dp/B07L9JKSGV).
- **Dimensions (W × L × D):** **55.9 × 55.9 × 31.75 mm** (2.20″ × 2.20″ square frame × 1.25″ depth). Baffle cutout Ø 49.8 mm (1.96″); bolt circle Ø 64.3 mm (2.53″).
- **Power / electrical:** Passive driver — **no supply voltage.** 4 Ω nominal (Re 3.6 Ω), **15 W RMS** power handling → at full power ≈ **7.7 V RMS / ≈1.94 A RMS** across 4 Ω. Fs 165.5 Hz, SPL 86.2 dB @ 2.83 V/1 m, Xmax 2.0 mm, VC Ø 25 mm, Vas 0.149 L, usable range 160–20,000 Hz.
- **Description:** High-performance 2″ full-range driver for a small sealed box; matching tunable passive radiator (**DMA58-PR**) available for more low-end. Needs a baffle + small enclosure (~100–250 cc sealed).
- **Interface:** None (analog audio; driven by a Class-D amp such as the MAX98357A / TAS5825M in root §7).
- **Released:** ~2019 (reviewed in *Voice Coil*, Dec 2019).

## 4. Juken / Switec X40.879 — Dual-Shaft Clock Stepper *(chosen movement)*

> Two datasheets: `stepper_motor_x40-879.pdf` (the **X40 pinout note**, `SP-X40-e-A-Pinout`) covers *only* the pinout and drive sequence; everything else — torque, current, coil resistance, dimensions, temperature — comes from `stepper_motor_x27_base-spec.pdf` (the **X27 series spec**, `SP-X27-e-C`), because the X40's two shafts use **X27-compatible mechanics**.

- **Product:** X40.879 — a **dual coaxial-shaft** stepper (independent hour + minute shafts) built from two X27-class movements; automotive/indicator-grade, drives directly from an MCU.
- **Refs:** Part # `X40.879` · Mfr **Juken Swiss Technology (JST) / Switec** · DigiKey # 28528329. Base spec applies to the `X27` series (e.g. X27.168, DigiKey # 26832207).
- **Price / link:** ~**$14.00** (qty 30) — [DigiKey 28528329](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) (active, confirmed dual-axis).
- **Dimensions:** per-movement **Ø 30 × 9 mm** (from X27 base spec). Overall X40 outline isn't stated in the pinout note — it stacks two coaxial shafts, so the vertical profile is taller (DigiKey lists it as "vertical, compact").
- **Power / electrical** (from the X27 base spec):
  - **Voltage:** operating **5–9 V DC** (phase shift 60°); absolute-max driving voltage 10 V.
  - **Current:** coil resistance 230 / 260 / 290 Ω per coil → **≈19 mA per coil at 5 V** (V/R); **two coils per shaft**, driven bipolar (8 pins total: external-shaft 1–4, internal-shaft 5–8).
  - Holding torque 3.5–4.0 mN·m; dynamic torque 1.0–1.45 mN·m @ 200°/s; noise ~40 dB(A) @ 4 cm; operating temp −40…+105 °C.
- **Description:** 1/3° per step at the indicator shaft (60°/step at rotor), up to 600°/s, 1/180 gear reduction. Base X27 has a 315° internal stop — for a full-rotation clock, buy/reuse the **360°/no-stop** variant and add external Hall homing (per root §5).
- **Interface:** Not a data bus — **two 2-phase bipolar coil sets** (one per shaft), driven from MCU GPIO via an H-bridge (DRV8835), partial-step or micro-step.
- **Released:** X40 pinout note rev A (`FO-220-01-B`); X27 base is a long-standing automotive gauge motor, still stocked.

---

## Reconciliation with root README

All previously-flagged mismatches are now resolved (root [`README.md`](../README.md) bumped to **v0.7**, 2026-07-04):

| Item | Was | Now |
|------|-----|-----|
| **Speaker** | README specced PC68-4; datasheet was DMA58-4 | ✅ README switched to **DMA58-4** (§7/§16); PC68-4 kept as a documented "bigger-box alt". Body depth relaxed to ≥45–60 mm / ~100–250 cc chamber (+ DMA58-PR). |
| **Stepper datasheet** | file was the X27 series only, mislabeled `x40-879` | ✅ Added the real **X40.879 pinout note**; renamed the X27 file to `..._x27_base-spec.pdf`; root §5 explains the X40→X27 dependency. |
| **X40.879 price** | README ~$25 | ✅ Corrected to **~$14** (DigiKey 28528329, qty 30). |
| **Display active area** | README "46.7 × 68.1 mm" | ✅ Corrected to **42.67 × 68.07 mm** (46.02 × 73.2 mm is the polarizer). |
| **Display power** | README "40 µW static / 250 µW dynamic" (uncited) | ✅ **Confirmed** by the full device spec: **30 µW hold / 250 µW update (typ)** @ 5 V; README updated to match. |
| **Display datasheet** | 2-page marketing brief | ✅ Replaced with the **full device spec** `LD-2023X13` (01-Nov-2023). |

*Clean matches from the start:* ESP32-S3-WROOM-1-N16R8 (18 × 25.5 mm, 3.0–3.6 V, 16 MB/8 MB).

**No open discrepancies remain.** The one intentional non-"match" is the **PC68-4**, deliberately retained in the root README as a larger/cheaper speaker alternative — not the chosen part.
