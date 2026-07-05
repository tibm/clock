# Datasheet Summary

Quick-reference for the datasheets in this folder. Prices are single-unit USD and approximate — click through to verify live stock/price. All parts are **currently active/orderable** (verified 2026-07-04) and **match the root [`README.md`](../README.md)** (v0.8).

> 🔩 **The stepper ships as two files.** `stepper_motor_x40-879.pdf` is a 2-page **pinout addendum** for the dual-shaft **X40.879** (the motor we're buying); it explicitly defers to the **X27 base spec** (`stepper_motor_x27_base-spec.pdf`) for torque, current, and dimensions. Keep both.

| # | File | Part | Mfr | Active | ~Price | Interface |
|---|------|------|-----|--------|--------|-----------|
| 1 | `display_ls032b7dd02.pdf` | LS032B7DD02 (full device spec) | Sharp | ✅ | ~$38 | 3-wire SPI |
| 2 | `mcu_esp32-s3-wroom-1-n16r8.pdf` | ESP32-S3-WROOM-1-N16R8 | Espressif | ✅ | ~$6.8 | host: Wi-Fi/BLE + UART/SPI/I²C/I²S |
| 3 | `speaker_dma58-4.pdf` | DMA58-4 | Dayton Audio | ✅ | ~$19 | Analog (passive driver) |
| 4 | `stepper_motor_x40-879.pdf` | **X40.879** (dual-shaft) | Juken / Switec | ✅ | ~$14 | 2-phase bipolar × 2 |
| 4b | `stepper_motor_x27_base-spec.pdf` | X27 base spec *(companion to #4)* | Juken / Switec | ✅ | — | — |
| 5 | `amp_tas5825m.pdf` | TAS5825M | Texas Instruments | ✅ | ~$3–5 | I²S in + I²C control |
| 6 | `motor_driver_drv8835.pdf` | DRV8835 (× 2) | Texas Instruments | ✅ | ~$2.2 | GPIO/PWM (PH/EN) |

---

## System IO & power domains

The pin/rail picture is getting busy, so track it here. The **ESP32-S3 (3.3 V logic)** is the host; everything else hangs off it. Two power rails are needed — **3.3 V** (logic) and **5 V** (display panel + stepper VM + amp PVDD) — from USB-C 5 V / a battery boost; an optional **~12 V boost** only if we want the amp louder than ~3 W.

| Part | Power rail(s) (abs-max) | Logic level | ESP32-S3 signals (GPIO count) |
|------|-------------------------|-------------|-------------------------------|
| **ESP32-S3-WROOM-1** (host) | 3.0–3.6 V (max 3.6) | 3.3 V | — drives everything below; ≤36 GPIO to budget |
| **LS032B7DD02** display | Panel VDD/VDDA **5 V** (4.8–5.5; abs 5.8) | **3 V** inputs | SPI SCLK · SI · SCS + DISP + EXTCOMIN → **~4–5** (EXTMODE tied to VDD) |
| **TAS5825M** amp | PVDD **5 V** (4.5–26.4) + DVDD **3.3 V** (abs 3.9) | 3.3 V (DVDD-ref) | I²S BCLK · LRCLK · SDIN + PDN (+ FAULT in) + I²C(shared) → **~4 + shared I²C** |
| **DMA58-4** speaker | — (passive, from amp OUT) | — | none (analog off TAS5825M PBTL output) |
| **DRV8835 × 2** driver | VM **5 V** (0–11) + VCC **3.3 V** (2–7) | 3.3 V | MODE(tie-hi) + APHASE·AENBL·BPHASE·BENBL → **4 / chip = 8** |
| **X40.879** stepper | coils fed from DRV8835 VM **5 V** | — | none direct (via DRV8835); 2 shafts × 2 coils |

**Shared / not-yet-datasheeted:** the **I²C bus** (SDA/SCL, 2 GPIO) is shared by the amp + all sensors (SHT4x, SGP41, VEML7700, BMA400, RTC). Homing uses **2× DRV5032** Hall (3.3 V, open-drain → 2 GPIO). Rough running total: I²S 3 + I²C 2 + display SPI ~5 + steppers 8 + Halls 2 + amp PDN 1 + SD (SDMMC 6 / SPI 4) + LEDs ~2 + encoder/buttons ~4 ≈ **30–34 GPIO** → fits, but reserve a strapping-safe map early. **The 8 stepper pins are the single biggest consumer** (direct-GPIO drive would cost the same 8, so DRV8835 is free in pin terms).

---

## 1. Sharp LS032B7DD02 — Reflective Memory LCD

- **Product:** 3.16″ transflective monochrome Memory-in-Pixel (MIP) LCD module, 336 × 536.
- **Refs:** Part # `LS032B7DD02` · Mfr **Sharp** · DigiKey # 23349498 · spec `LD-2023X13` (full device spec, dated **01-Nov-2023**).
- **Price / link:** ~**$38.14** — [DigiKey 23349498](https://www.digikey.com/en/products/detail/sharp-microelectronics/LS032B7DD02/23349498) (active, in stock).
- **Dimensions (W × L × D):** **47.02 × 76.00 × 0.705 mm** (outline). Active area 42.672 × 68.072 mm; top polarizer 46.02 × 73.2 mm; dot pitch 0.127 mm; mass 5.5 g max.
- **Power / IO:**
  - **Voltage:** VDD/VDDA recommended **4.8 / 5.0 / 5.5 V** (min/typ/max); absolute max −0.3…+5.8 V. Logic inputs (SCLK/SI/SCS/DISP/EXTCOMIN) high = **2.7 / 3.0 / VDD V** → **3.3 V-MCU compatible**.
  - **Power consumption:** HOLD (no update) **30 µW typ** (330 µW max); data update @ 1 Hz **250 µW typ** (750 µW max) — i.e. ≈6 µA hold / ≈50 µA update at 5 V. (Peak COM current is higher; size the rail with margin.)
  - **IO:** 3-wire SPI (SCLK, SI, SCS) + DISP (on/off) + EXTCOMIN (VCOM toggle) + EXTMODE (tie to VDD). ~4–5 ESP32 GPIO.
- **Description:** Reflective/transflective monochrome MIP LCD — retains its image at near-zero power, daylight-readable, no backlight. Response 30 ms, viewing angle 120°/120°, operating −20…+70 °C. FPC connector.
- **Interface:** **3-wire SPI** + DISP/EXTCOMIN/EXTMODE control.
- **Released:** device spec © 2023 (rev `LD-2023X13`, 01-Nov-2023). *(This file replaced the earlier 2-page marketing brief.)*

## 2. Espressif ESP32-S3-WROOM-1-N16R8 — Wi-Fi + BLE MCU module *(host)*

- **Product:** ESP32-S3 SoC module, dual-core Xtensa LX7 @ up to 240 MHz, **16 MB flash / 8 MB PSRAM** (`N16R8`), on-board PCB antenna.
- **Refs:** Part # `ESP32-S3-WROOM-1-N16R8` · Mfr **Espressif** · DigiKey # 16162642.
- **Price / link:** ~**$6.76** — [DigiKey 16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) (active, ships today).
- **Dimensions (W × L × D):** **18.0 × 25.5 × 3.1 mm** (±0.2 / ±0.2 / ±0.15).
- **Power / IO:**
  - **Voltage:** 3.0–3.6 V (typ 3.3 V). Absolute max 3.6 V, min −0.3 V. External supply ≥ 0.5 A.
  - **Current:** Wi-Fi TX peak up to **355 mA**; BLE TX peak up to **344 mA**; RX ~95 mA. Modem-sleep ~13–108 mA. Light-sleep **240 µA**; deep-sleep 7–170 µA; power-off **1 µA**.
  - **IO:** up to **36 GPIO**, muxable to UART/SPI/I²C/I²S/SDIO/PWM/ADC. GPIO source 40 mA / sink 28 mA.
- **Description:** Single-chip 2.4 GHz Wi-Fi 802.11 b/g/n + Bluetooth LE 5 host driving the whole system. (`-1U` = external antenna.)
- **Interface:** **Wireless** Wi-Fi + BLE 5; **wired** UART/SPI/I²C/I²S/SDIO via GPIO mux.
- **Released:** module ~2021 (SoC launched Dec 2020); datasheet v1.8.

## 3. Dayton Audio DMA58-4 — 2″ Full-Range Driver *(chosen speaker)*

- **Product:** 2″ dual-neodymium-magnet, aluminum-cone full-range driver, 4 Ω, open 8-spoke frame.
- **Refs:** Part # `DMA58-4` · Mfr **Dayton Audio** · Parts Express # 295-582 · Amazon ASIN B07L9JKSGV. **Not on DigiKey.**
- **Price / link:** ~**$18.99** — [Parts Express 295-582](https://www.parts-express.com/Dayton-Audio-DMA58-4-2-Dual-Magnet-Aluminum-Cone-Full-Range-Driver-4-Ohm-295-582) · also [Amazon](https://www.amazon.com/dp/B07L9JKSGV).
- **Dimensions (W × L × D):** **55.9 × 55.9 × 31.75 mm** (2.20″ square × 1.25″ deep). Cutout Ø 49.8 mm (1.96″); bolt circle Ø 64.3 mm (2.53″).
- **Power / IO:** Passive driver — **no supply, no logic.** 4 Ω nominal (Re 3.6 Ω), **15 W RMS** → ≈7.7 V RMS / ≈1.94 A RMS at full power. Fs 165.5 Hz, SPL 86.2 dB @ 2.83 V/1 m, Xmax 2.0 mm, VC Ø 25 mm, Vas 0.149 L, range 160–20,000 Hz. **Connects to the TAS5825M PBTL output**, not the MCU.
- **Description:** High-performance 2″ full-range driver for a small sealed box; matching **DMA58-PR** passive radiator available. Needs baffle + ~100–250 cc sealed enclosure.
- **Interface:** None (analog audio).
- **Released:** ~2019 (reviewed *Voice Coil*, Dec 2019).

## 4. Juken / Switec X40.879 — Dual-Shaft Clock Stepper *(chosen movement)*

> Two datasheets: `stepper_motor_x40-879.pdf` (**X40 pinout note**, `SP-X40-e-A-Pinout`) covers *only* pinout + drive sequence; torque, current, coil resistance, dimensions, temperature all come from `stepper_motor_x27_base-spec.pdf` (**X27 series spec**, `SP-X27-e-C`) — the X40's two shafts use **X27-compatible mechanics**.

- **Product:** X40.879 — a **dual coaxial-shaft** stepper (independent hour + minute shafts) built from two X27-class movements; drives directly from an MCU (here, via 2× DRV8835).
- **Refs:** Part # `X40.879` · Mfr **Juken Swiss Technology / Switec** · DigiKey # 28528329. Base spec = `X27` series (e.g. X27.168, DigiKey # 26832207).
- **Price / link:** ~**$14.00** (qty 30) — [DigiKey 28528329](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) (active, confirmed dual-axis).
- **Dimensions:** per-movement **Ø 30 × 9 mm** (X27 base). X40 overall isn't in the pinout note — it stacks two coaxial shafts → taller vertical profile.
- **Power / IO** (from X27 base spec):
  - **Voltage:** operating **5–9 V DC**; absolute-max driving voltage 10 V. Driven at **5 V** via DRV8835 VM for full torque.
  - **Current:** coil R 230/260/290 Ω → **≈19 mA/coil at 5 V**; **two coils per shaft**, bipolar. **8 coil terminals** total (external-shaft 1–4, internal-shaft 5–8) → four H-bridges = two DRV8835.
  - Holding torque 3.5–4.0 mN·m; dynamic 1.0–1.45 mN·m @ 200°/s; noise ~40 dB(A); temp −40…+105 °C.
- **Description:** 1/3° per step (60°/step rotor), up to 600°/s, 1/180 gear. Base X27 has a 315° internal stop — for a clock, buy/reuse the **360°/no-stop** variant + external Hall homing.
- **Interface:** Not a data bus — **two 2-phase bipolar coil sets** driven from the ESP32 via **2× DRV8835** (below), PWM microstep.
- **Released:** X40 pinout rev A (`FO-220-01-B`); X27 base is a long-standing automotive gauge motor.

## 5. Texas Instruments TAS5825M — I²S DSP Class-D Amp *(chosen amp)*

- **Product:** Digital-input, **closed-loop Class-D** amp with a **192-kHz audio DSP** (EQ, 3-band DRC/loudness, limiter, thermal foldback). Stereo, or **PBTL mono** for the single 4 Ω DMA58-4.
- **Refs:** Part # `TAS5825M` (orderable `TAS5825MRHBR`, VQFN-32) · Mfr **Texas Instruments** · datasheet `SLASF29`. [TI product page](https://www.ti.com/product/TAS5825M).
- **Price / link:** ~**$3–5** — DigiKey keyword `TAS5825MRHBR` (active).
- **Package:** VQFN-32 (5 × 5 mm, exposed thermal pad → copper pour).
- **Power / IO:**
  - **Voltage:** **PVDD 4.5–26.4 V** power stage (run at **5 V** for ~3 W, or add a ~12 V boost for ~8–12 W) + **DVDD 1.8 V or 3.3 V** logic/IO (abs-max 3.9 V). Internal 1.5 V (VR_DIG) and 5 V (AVDD) LDOs — **do not load externally**.
  - **Current:** quiescent < 20 mA @ PVDD 12 V.
  - **IO (all DVDD/3.3 V-referenced, thresholds 70 %/30 % VDVDD):** **I²S** = SDIN, SCLK(BCLK), LRCLK (3-wire, no MCLK needed); **I²C** = SDA, SCL (+ `ADR` resistor sets address) — shared bus; **PDN** power-down; GPIO0/1/2 = FAULT/MUTE/SDOUT. → **~4 dedicated GPIO + shared I²C**.
- **Output power / quality:** 1 × 53 W (4 Ω, 22 V, 1 % THD) down to ~3 W at PVDD 5 V; **THD+N ≤ 0.03 %** @ 1 W/12 V; **SNR ≥ 110 dB (A-wt)**, ICN ≤ 35 µV. Needs an **LC output filter** to the speaker (recommended for EMC even though it's inductor-less capable).
- **Why it's chosen:** the DSP loads a **~150–180 Hz high-pass + limiter + loudness/EQ** at boot (via I²C) — protects the small 2 mm-Xmax driver from over-excursion and makes it sound full. Tune with TI **PPC3 (PurePath Console)** → export register/coeff blob for the firmware.
- **Interface:** **I²S** (audio in) + **I²C** (control/DSP).
- **Alt (simpler):** MAX98357A — no I²C, 3.2 W @ 5 V, filterless, but no DSP/protection.

## 6. Texas Instruments DRV8835 — Dual H-Bridge Motor Driver *(× 2, chosen stepper driver)*

- **Product:** Dual low-voltage H-bridge. **One DRV8835 per shaft** (2 H-bridges = the shaft's 2 coils); **two chips** for the X40.879.
- **Refs:** Part # `DRV8835` (orderable `DRV8835DSSR`, 12-WSON) · Mfr **Texas Instruments** · datasheet `SLVSB18H` (Aug 2016). DigiKey # 3088201.
- **Price / link:** ~**$2.19/1** — [DigiKey 3088201](https://www.digikey.com/en/products/detail/texas-instruments/DRV8835DSSR/3088201) (active). *(DRV8833 is NRND; TB6612FNG is the drop-in alt.)*
- **Package:** 12-WSON (3 × 3 mm, thermal pad).
- **Power / IO:**
  - **Voltage:** **VM 0–11 V** motor supply (run at **5 V** for full stepper torque) + **VCC 2–7 V** logic (**3.3 V** from the MCU). Internal charge pump; sleep 95 nA.
  - **Current:** **1.5 A per H-bridge** max — vs the ~19 mA the coils need → huge margin, runs cold.
  - **IO (per chip, 3.3 V logic):** `MODE` (tie **high** = PHASE/ENABLE mode) + `APHASE`/`AENBL` + `BPHASE`/`BENBL`. PHASE = coil direction, **ENABLE = PWM duty for microstep** (drive >20 kHz via ESP32 MCPWM). → **4 GPIO/chip = 8 GPIO total**. Outputs AOUT1/2, BOUT1/2 to the coils. Decouple VM & VCC with 0.1 µF (min).
- **Description:** Robust, integrated flyback (protects the MCU), level-shifted 3.3 V inputs, full 5 V coil drive. No internal current chopper → microstepping is PWM/voltage-based (fine for these high-impedance gauge coils). **Do not** use A4988/DRV8825/DRV8434/TMC-class choppers — they can't regulate ~19 mA into 260 Ω.
- **Interface:** GPIO + PWM (PHASE/ENABLE). Homing via 2× DRV5032 Hall (separate).
- **Released:** rev H, Aug 2016; long-standing, active.

---

## Reconciliation with root README (v0.8)

All parts now match the root [`README.md`](../README.md):

| Item | Status |
|------|--------|
| **Speaker** | ✅ DMA58-4 chosen (§7/§16); PC68-4 kept as documented "bigger-box alt". |
| **Stepper** | ✅ X40.879 (+ X27 companion datasheet); root §5 explains the dependency. |
| **Audio amp** | ✅ **TAS5825M** locked as *the* amp (v0.8); MAX98357A demoted to simple alt. |
| **Motor driver** | ✅ **2× DRV8835** locked as *the* driver (already the pick; confirmed v0.8). |
| **X40.879 price** | ✅ ~$14 (was ~$25). |
| **Display specs** | ✅ Active area 42.67 × 68.07 mm; power 30 µW hold / 250 µW update @ 5 V; full device spec on file. |

**No open discrepancies.** The one intentional non-"match" is **PC68-4**, deliberately retained in the root README as a larger/cheaper speaker *alternative* — not the chosen part.
