# Datasheet Summary

Quick-reference for the datasheets in this folder. Prices are single-unit USD and approximate — click through to verify live stock/price. All parts are **currently active/orderable** (verified 2026-07-04; homing sensor 2026-07-05; sensor set 2026-07-05; **power-path & interconnect set 2026-07-05**; **IO expander 2026-07-07**) and **match the root [`README.md`](../README.md)** (v0.15).

> 🔩 **HAND-SOLDERABLE PARTS ONLY (hard requirement).** The bare PCB is fab'd externally; **every part is soldered by hand** with an iron. So **no QFN / DFN / WSON / BGA / WLP / LGA** parts sit bare on the board — every active IC here is a **leaded/gullwing** package (SOIC / SOP / SSOP / TSSOP / **HTSSOP** / **MSOP** / SOT-23) or a **castellated/edge module**. Two power parts (amp, charger) are HTSSOP/MSOP **PowerPAD**: the leads are iron-solderable and the belly pad is grounded through a thermal-via array (back-side hot-air optional). This trades **board area** for hand-assembly — accepted. Parts that *only* exist leadless (env/MEMS sensors — **BME688, TSL2591, LIS3DH**; fuel gauge) are pushed onto **pre-made breakout modules** (2a) or a **custom SMT-assembled daughterboard** (2b), or **dropped**; see §15 + the root README manufacturing section.

> 🔩 **The stepper ships as two files.** `stepper_motor_x40-879.pdf` is a 2-page **pinout addendum** for the dual-shaft **X40.879** (the motor we're buying); it explicitly defers to the **X27 base spec** (`stepper_motor_x27_base-spec.pdf`) for torque, current, and dimensions. Keep both.

| # | File | Part | Mfr | Package (hand-solder) | Active | ~Price | Interface |
|---|------|------|-----|-----------------------|--------|--------|-----------|
| 1 | `display_ls032b7dd02.pdf` | LS032B7DD02 (full device spec) | Sharp | module + FPC | ✅ | ~$38 | 3-wire SPI |
| 2 | `mcu_esp32-s3-wroom-1-n16r8.pdf` | ESP32-S3-WROOM-1-N16R8 | Espressif | **module (castellated)** | ✅ | ~$6.8 | Wi-Fi/BLE + UART/SPI/I²C/I²S |
| 3 | `speaker_dma58-4.pdf` | DMA58-4 | Dayton Audio | wired (passive) | ✅ | ~$19 | Analog |
| 4 | `stepper_motor_x40-879.pdf` | **X40.879** (dual-shaft) | Juken / Switec | wired | ✅ | ~$14 | 2-phase bipolar × 2 |
| 4b | `stepper_motor_x27_base-spec.pdf` | X27 base spec *(companion to #4)* | Juken / Switec | wired | ✅ | — | — |
| 5 | `amp_tas5760m.pdf` | **TAS5760M** (`TAS5760MDAPR`) | Texas Instruments | **HTSSOP-32 (DAP, PowerPAD)** | ✅ | ~$4–6 | I²S in + I²C control |
| 6 | `motor_driver_tb6612fng.pdf` | **TB6612FNG** (× 2) | Toshiba | **SSOP-24 (no exposed pad)** | ✅ | ~$2.4 | GPIO/PWM (IN/IN) |
| 7 | `pd_sink_ch224k.pdf` | **CH224K** | WCH (Qinheng) | **ESSOP-10** | ✅ | ~$0.4 | USB-PD (resistor-set) |
| 8 | `charger_lt3652.pdf` | **LT3652** (`LT3652EMSE#PBF`) | Analog Devices | **MSOP-12E (PowerPAD)** | ✅ | ~$5–6 | autonomous (resistor-set) |
| 9 | `battery_protector_s-8261.pdf` | **S-8261** (`S-8261AAxMD`) | ABLIC | **SOT-23-6** | ✅ | ~$0.4 | none (autonomous) |
| 10 | `protection_mosfet_ao4800.pdf` | **AO4800** (dual N-FET, protector) | Alpha & Omega | **SO-8** | ✅ | ~$0.3 | none (protector FET) |
| 11 | `connector_display_fpc_fh34.pdf` | **FH34SRJ-10S-0.5SH** | Hirose | **FPC/ZIF 0.5 mm (SMT, on-board)** | ✅ | ~$0.7 | 10-pin display FPC tail |
| 12 | `sensor_env_bme688.pdf` | **BME688** | Bosch Sensortec | LGA-8 → **module** | ✅ | ~$5 | I²C/SPI — **chosen env part:** T+RH+press+**VOC/gas** (one chip = climate + air-quality) |
| 13 | `sensor_light_tsl2591.pdf` | **TSL2591** (`TSL25911FN`) | ams-OSRAM | WFDFN-6 → **module** | ✅ | ~$3 | I²C (188 µlx–88 klx) |
| 14 | `sensor_accel_lis3dh.pdf` | **LIS3DH** | STMicroelectronics | LGA-16 → **module** | ✅ | ~$2 | I²C/SPI (tap + orient) |
| 15 | `sensor_homing_optical_itr8307.pdf` | **ITR8307** (`ITR8307/TR8`) | Everlight | **4-SMD (3.4×1.5×1.1 mm, gullwing) — on-board** | ✅ | ~$0.5 | reflective opto (analog → ADC/comparator) |
| 16 | `xtal_32k_abs07.pdf` | **ABS07-32.768KHZ-T** (32.768 kHz, CL 12.5 pF, ±20 ppm) | Abracon | **3.2×1.5 mm 2-SMD** | ✅ | ~$0.3 | RTC clock (S3 XTAL32K) |
| 17 | `reverse_pfet_ao3401a.pdf` | **AO3401A** (−30 V, −4 A P-ch) | Alpha & Omega | **SOT-23-3** | ✅ | ~$0.24 | reverse-polarity FET |
| 18 | `ntc_10k_ncp18xh103.pdf` | **NCP18XH103F03RB** 10 k NTC (B25/50 = 3380 K, ±1 %) | Murata | **0603** | ✅ | ~$0.10 | analog → LT3652 NTC |
| 19 | `tvs_smaj.pdf` | **SMAJ22A** (VBUS) + **SMAJ5.0A** (BAT), 400 W uni | Littelfuse | **DO-214AC (SMA)** | ✅ | ~$0.4 ea | TVS clamp |
| 20 | `boost_12v_audio_tps55340.pdf` | **TPS55340PWPR** (5 A/40 V boost) | Texas Instruments | **HTSSOP-14 (PWP, PowerPAD)** | ✅ | ~$2.5 | amp 12 V rail (gated) |
| 21 | `boost_5v_tps61023.pdf` | **TPS61023DRLR** (3.7 A boost, 0.5–5.5 V in) | Texas Instruments | **SOT-563** | ✅ | ~$1.2 | 5 V rail |
| 22 | `buck_3v3_tlv62569.pdf` | **TLV62569DBVR** (2 A buck, 2.5–5.5 V in) | Texas Instruments | **SOT-23-6** | ✅ | ~$0.25 | 3.3 V rail |
| 23 | `holder_18650_keystone_1043.pdf` | **1043** 18650 holder (UL94V-0) | Keystone | **TH PC-pin** | ✅ | ~$2.9 | cell holder |
| 24 | `connector_microsd_dm3at.pdf` | **DM3AT-SF-PEJM5** microSD (push-push) | Hirose | **push-push SMT R/A** | ✅ | ~$2.85 | SD (SPI) |
| 25 | `connector_usb_c_usb4105.pdf` | **USB4105-GF-A** USB-C (USB 2.0, 5 A) | GCT | **SMT + TH tabs** | ✅ | ~$0.8 | USB-C power+CC |
| 26 | `expander_mcp23017.pdf` | **MCP23017** (I²C; SPI twin `MCP23S17`) | Microchip | **SOIC-28 W / SSOP-28** | ✅ | ~$1.3 | I²C 16-bit GPIO expander (+INT) |

**Every env/MEMS sensor is leadless (LGA/DFN) — no hand-solderable silicon exists**, so none sit bare on the board. **Both build paths carry the identical set — BME688 + TSL2591 + LIS3DH — so the firmware is the same either way** (see §15):
- **2a — chosen (build now): STEMMA QT / Qwiic daisy-chain** of three ready Adafruit boards on one 4-wire I²C chain — **BME688 (Adafruit 5046, ~$19) · TSL2591 (1980, $6.95) · LIS3DH (2809, $4.95)**. Zero leadless soldering, fastest bring-up.
- **2b — future: one custom sensor daughterboard** carrying the **same three bare chips**, JLCPCB SMT-assembled; you hand-solder only its 0.1″ header / castellated edge → smallest footprint, still no iron on a leadless pad. Same part numbers → same I²C addresses → 2a firmware runs unchanged.

The **display connector (row 11) is the exception** among leadless-class parts: a 0.5 mm FPC/ZIF is explicitly hand-solderable, so it sits **on the main PCB**. **No RTC IC** — timekeeping is the **ESP32-S3 internal RTC off a 32.768 kHz crystal** (XTAL32K, GPIO15/16) + SNTP; no coin cell (see root README §6/§8/§10).

**Fuel gauge dropped** (no hand-solderable equivalent — every 1-cell gauge is TDFN/WLP): SoC is now **voltage-based via the ESP32-S3 ADC** on a divider off the cell. See root README §10 / `../power.md`.

**Removed datasheets (leadless, replaced above):** `amp_tas5825m.pdf` (VQFN-32), `motor_driver_drv8835.pdf` (WSON-12), `pd_sink_stusb4500.pdf` (QFN-24), `charger_bq25628e.pdf` (WQFN-18), `fuel_gauge_max17048.pdf` (µDFN/WLP), `battery_protector_lc05111cmt.pdf` (WDFN-6). **v0.15 sensor consolidation:** `sensor_temp_tmp117.pdf` (TMP117 — dropped, redundant), `sensor_humidity_temp_sht4x.pdf` (SHT45 — folded into BME688), `sensor_accel_bma400.pdf` (BMA400 — replaced by LIS3DH).

---

## System IO & power domains

The pin/rail picture is getting busy, so track it here. The **ESP32-S3 (3.3 V logic)** is the host; everything else hangs off it. Rails (see [`../power.md`](../power.md) for the full tree + battery safety): **3.3 V** (logic, from the 5 V rail) · **5 V** (display panel + stepper VM) · **12 V boost** (amp PVDD, gated) · **15 V VBUS** (LED, plugged). Source: **USB-PD 15 V** (CH224K) → LT3652 buck charger, 1S Li-ion 18650; the charger **BAT node is the always-on system supply**.

| Part | Power rail(s) (abs-max) | Logic level | ESP32-S3 signals (GPIO count) |
|------|-------------------------|-------------|-------------------------------|
| **ESP32-S3-WROOM-1** (host) | 3.0–3.6 V (max 3.6) | 3.3 V | — drives everything below; **~33 usable GPIO** on **N16R8** (octal PSRAM claims GPIO35/36/37) |
| **LS032B7DD02** display | Panel VDD/VDDA **5 V** (4.8–5.5; abs 5.8) | **3 V** inputs | SPI SCLK · SI · SCS + DISP + EXTCOMIN → **~4–5** (EXTMODE tied to VDD) |
| **TAS5760M** amp | PVDD **12 V** (boost; range 4.5–26.4) + DVDD **3.3 V** | 3.3 V (DVDD-ref) | I²S BCLK · LRCLK · SDIN + SPK_SD (mute/PDN) + I²C(shared) → **~4 + shared I²C** |
| **DMA58-4** speaker | — (passive, from amp OUT) | — | none (analog off TAS5760M PBTL output) |
| **TB6612FNG × 2** driver | VM **5 V** (0–13.5, abs 15) + VCC **3.3 V** (2.7–5.5) | 3.3 V | AIN1·AIN2·BIN1·BIN2 (PWM'd) + STBY → **4 / chip = 8** + shared STBY |
| **X40.879** stepper | coils fed from TB6612FNG VM **5 V** | — | none direct (via TB6612FNG); 2 shafts × 2 coils |
| **CH224K** PD sink | VBUS **4–22 V**; VDD off VBUS | 3.3 V (PG open-drain) | CFG (resistors, no GPIO) + PG → **~1** |
| **LT3652** charger | VIN (VBUS) **≤32 V**; BAT (system node) | 3.3 V (open-drain status) | CHRG + FAULT status → **~2**; NTC/timer/float = passives |
| **Cell voltage sense** | off cell via divider | ADC | 1 ADC pin → **1** |
| **S-8261 + AO4800** protector | across cell (≤12 V) | — | none (autonomous OV/OD/OC/SC) |
| **ITR8307** hand homing | IR LED + phototransistor off **3.3 V** (R-limited) | 3.3 V (ADC) | reflective opto behind a punched dial hole → **1 ADC/comparator**; no magnets |
| **MCP23017** IO expander | 1.8–5.5 V (**3.3 V**) | 3.3 V (I²C + INT) | on the **shared I²C** (no new bus pins) + **INT → 1 GPIO**; fans out 16 slow I/O → offloads SPK_SD, 12 V EN, stepper STBY, buttons, enc-SW (**net −4 MCU GPIO**) |

**Shared bus:** the **I²C bus** (SDA/SCL, 2 GPIO) is shared by the amp + all sensors (**BME688** T/RH/press/VOC, **TSL2591** light, **LIS3DH** accel — rows 12–15b) **and the MCP23017 IO expander (row 26)** — **kept small** otherwise (no charger, no fuel gauge, **no RTC** on I²C). Timekeeping = S3 RTC + a **32.768 kHz crystal** on XTAL32K (GPIO15/16, dedicated — not on I²C). Homing uses **1× ITR8307** reflective optical (§16; 4-SMD, 3.3 V, analog out → **1 ADC GPIO**, no magnets).

**GPIO tally (post-expander):** I²S 3 + shared-I²C 2 + display SPI ~5 + steppers 8 + optical home 1 + SD SPI 4 + charger status 2 + Vbat ADC 1 + PD PG 1 + LED data/dim ~2 + encoder A/B 2 + sensor INT 1 + expander INT 1 ≈ **33 GPIO** + the 2 dedicated XTAL32K pins. The **MCP23017 (§19)** pulls **SPK_SD, 12 V-boost EN, stepper STBY, and the buttons/encoder-switch** off the host (**net −4 GPIO** for +1 INT, since it rides the existing I²C bus).

> ⚠️ **N16R8 budget is tighter than the old "≤36".** The **octal PSRAM claims GPIO35/36/37** (verified in the S3-WROOM-1 datasheet pin table — "not available for other use"), so the module frees only **~33 usable pads**; reserve GPIO19/20 for native USB → **~31 truly free**, four of them strapping. So ~33 needed sits *at* the ceiling. Recover margin by (a) **sharing microSD on the display SPI bus** (common MOSI/SCLK/MISO, per-device CS → **−2**), (b) **software VCOM** (tie EXTMODE=L, drop EXTCOMIN → **−1**), or (c) a **quad-PSRAM module (…R2)**, which keeps GPIO35–37 free (trade 8 MB→2 MB PSRAM).

**Peripheral/PWM count is fine (verified vs. the S3 datasheet):** the 8 stepper AIN/BIN go on **MCPWM** (2 units × 3 operators = **12 PWM outputs**; 8 used) — *not* LEDC — leaving all **8 LEDC** channels for LED-CC dimming + optional EXTCOMIN. SK6812 halo → **RMT** (4 TX ch). Encoder A/B → **PCNT** (4 units, hardware quadrature, zero CPU). Audio → **I²S0** (of 2). Display + SD → the **2 GP-SPI** hosts (or one shared). Both analog inputs (Vbat, homing) **must land on ADC1 = GPIO1–10** — **ADC2 is unusable while Wi-Fi is active**. **The steppers stay 8 pins** — TB6612FNG driven PWM-on-the-inputs (PWMA/PWMB tied high); the conventional PWM-pin scheme would cost 12.

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
- **Hand-assembly:** module with an **FPC tail** → mate with a **hand-solderable fine-pitch FPC/ZIF connector** (or an FPC-to-header breakout). No bare leadless silicon.
- **Interface:** **3-wire SPI** + DISP/EXTCOMIN/EXTMODE control.
- **Released:** device spec © 2023 (rev `LD-2023X13`, 01-Nov-2023).

## 2. Espressif ESP32-S3-WROOM-1-N16R8 — Wi-Fi + BLE MCU module *(host)*

- **Product:** ESP32-S3 SoC module, dual-core Xtensa LX7 @ up to 240 MHz, **16 MB flash / 8 MB PSRAM** (`N16R8`), on-board PCB antenna.
- **Refs:** Part # `ESP32-S3-WROOM-1-N16R8` · Mfr **Espressif** · DigiKey # 16162642.
- **Price / link:** ~**$6.76** — [DigiKey 16162642](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-WROOM-1-N16R8/16162642) (active, ships today).
- **Dimensions (W × L × D):** **18.0 × 25.5 × 3.1 mm** (±0.2 / ±0.2 / ±0.15).
- **Power / IO:**
  - **Voltage:** 3.0–3.6 V (typ 3.3 V). Absolute max 3.6 V, min −0.3 V. External supply ≥ 0.5 A.
  - **Current:** Wi-Fi TX peak up to **355 mA**; BLE TX peak up to **344 mA**; RX ~95 mA. Modem-sleep ~13–108 mA. Light-sleep **240 µA**; deep-sleep 7–170 µA; power-off **1 µA**.
  - **IO:** up to **36 GPIO**, muxable to UART/SPI/I²C/I²S/SDIO/PWM/ADC. GPIO source 40 mA / sink 28 mA.
- **Hand-assembly:** **module with castellated edge pads** → drag-solder with an iron. The bottom center GND/thermal pad benefits from a via array (heat from the back / hot-air optional) but the castellations carry all signal + power. This is a *module*, not a bare QFN — acceptable.
- **Interface:** **Wireless** Wi-Fi + BLE 5; **wired** UART/SPI/I²C/I²S/SDIO via GPIO mux.
- **Released:** module ~2021 (SoC launched Dec 2020); datasheet v1.8.

## 3. Dayton Audio DMA58-4 — 2″ Full-Range Driver *(chosen speaker)*

- **Product:** 2″ dual-neodymium-magnet, aluminum-cone full-range driver, 4 Ω, open 8-spoke frame.
- **Refs:** Part # `DMA58-4` · Mfr **Dayton Audio** · Parts Express # 295-582 · Amazon ASIN B07L9JKSGV. **Not on DigiKey.**
- **Price / link:** ~**$18.99** — [Parts Express 295-582](https://www.parts-express.com/Dayton-Audio-DMA58-4-2-Dual-Magnet-Aluminum-Cone-Full-Range-Driver-4-Ohm-295-582) · also [Amazon](https://www.amazon.com/dp/B07L9JKSGV).
- **Dimensions (W × L × D):** **55.9 × 55.9 × 31.75 mm** (2.20″ square × 1.25″ deep). Cutout Ø 49.8 mm (1.96″); bolt circle Ø 64.3 mm (2.53″).
- **Power / IO:** Passive driver — **no supply, no logic.** 4 Ω nominal (Re 3.6 Ω), **15 W RMS** → ≈7.7 V RMS / ≈1.94 A RMS at full power. Fs 165.5 Hz, SPL 86.2 dB @ 2.83 V/1 m, Xmax 2.0 mm, VC Ø 25 mm, Vas 0.149 L, range 160–20,000 Hz. **Connects to the TAS5760M PBTL output** via **wire + JST**, not the MCU.
- **Description:** High-performance 2″ full-range driver for a small sealed box; matching **DMA58-PR** passive radiator available. Needs baffle + ~100–250 cc sealed enclosure.
- **Interface:** None (analog audio).
- **Released:** ~2019 (reviewed *Voice Coil*, Dec 2019).

## 4. Juken / Switec X40.879 — Dual-Shaft Clock Stepper *(chosen movement)*

> Two datasheets: `stepper_motor_x40-879.pdf` (**X40 pinout note**, `SP-X40-e-A-Pinout`) covers *only* pinout + drive sequence; torque, current, coil resistance, dimensions, temperature all come from `stepper_motor_x27_base-spec.pdf` (**X27 series spec**, `SP-X27-e-C`) — the X40's two shafts use **X27-compatible mechanics**.

- **Product:** X40.879 — a **dual coaxial-shaft** stepper (independent hour + minute shafts) built from two X27-class movements; drives directly from an MCU (here, via 2× TB6612FNG).
- **Refs:** Part # `X40.879` · Mfr **Juken Swiss Technology / Switec** · DigiKey # 28528329. Base spec = `X27` series (e.g. X27.168, DigiKey # 26832207).
- **Price / link:** ~**$14.00** (qty 30) — [DigiKey 28528329](https://www.digikey.com/en/products/detail/juken-swiss-technology/X40-879/28528329) (active, confirmed dual-axis).
- **Dimensions:** per-movement **Ø 30 × 9 mm** (X27 base). X40 overall isn't in the pinout note — it stacks two coaxial shafts → taller vertical profile.
- **Power / IO** (from X27 base spec):
  - **Voltage:** operating **5–9 V DC**; absolute-max driving voltage 10 V. Driven at **5 V** via TB6612FNG VM for full torque.
  - **Current:** coil R 230/260/290 Ω → **≈19 mA/coil at 5 V**; **two coils per shaft**, bipolar. **8 coil terminals** total (external-shaft 1–4, internal-shaft 5–8) → four H-bridges = two TB6612FNG.
  - Holding torque 3.5–4.0 mN·m; dynamic 1.0–1.45 mN·m @ 200°/s; noise ~40 dB(A); temp −40…+105 °C.
- **Description:** 1/3° per step (60°/step rotor), up to 600°/s, 1/180 gear. Base X27 has a 315° internal stop — for a clock, buy/reuse the **360°/no-stop** variant + external **optical** homing (ITR8307, §16).
- **Interface:** Not a data bus — **two 2-phase bipolar coil sets** driven from the ESP32 via **2× TB6612FNG** (below), PWM microstep. Connects via **wire + JST**.
- **Released:** X40 pinout rev A (`FO-220-01-B`); X27 base is a long-standing automotive gauge motor.

## 5. Texas Instruments TAS5760M — I²S Digital-Input Class-D Amp *(chosen amp)*

- **Product:** Digital-input **closed-loop Class-D** amp with I²S input and I²C control. Stereo, or **PBTL mono** for the single 4 Ω DMA58-4. Integrated **digital clipper**, selectable gain, DC-detect / over-current / over-temperature (thermal foldback) protection. **ACTIVE — recommended for new designs.**
- **Refs:** Part # `TAS5760M` (orderable **`TAS5760MDAPR`**, HTSSOP-32) · Mfr **Texas Instruments** · datasheet `SLOS736`. [TI product page](https://www.ti.com/product/TAS5760M).
- **Price / link:** ~**$4–6** — DigiKey keyword `TAS5760MDAPR` (verify active/stock).
- **Package:** **HTSSOP-32 (DAP, PowerPAD)** — 32 gullwing leads (iron-solderable) + a thermal belly pad → copper pour + thermal-via array. *(A 48-pin HTSSOP `DCA` also exists; the 32-pin DAP is the compact hand-solderable choice.)*
- **Power / IO:**
  - **Voltage:** **PVDD 4.5–26.4 V** power stage (run at **~12 V** boost for ~8–12 W to the alarm, or 5 V for ~3 W) + **DVDD 3.3 V** logic/IO.
  - **Current:** low quiescent; scales with output.
  - **IO (DVDD/3.3 V-referenced):** **I²S** = SDIN, SCLK(BCLK), LRCLK (no MCLK needed); **I²C** = SDA, SCL (+ address strap) — shared bus; **SPK_SD** (mute / shutdown). → **~4 dedicated GPIO + shared I²C**.
- **Output power / quality:** up to **55 W stereo / 114 W mono PBTL** (rail-dependent) — for our 12 V PVBTL into 4 Ω that's ~8–12 W with plenty of headroom. Needs an **LC output filter** to the speaker (recommended for EMC).
- **Why it's chosen (vs the QFN TAS5825M it replaces):** it keeps the **I²S digital path + I²C control + output protection** in a **leaded, hand-solderable HTSSOP**. It does **not** carry the TAS5825M's full biquad DSP (EQ/DRC), so the **~150 Hz high-pass + limiter/EQ that protects the 2 mm-Xmax driver runs in ESP32-S3 firmware** (a few biquads at 44.1/48 kHz before I²S) — cheap on the S3.
- **Interface:** **I²S** (audio in) + **I²C** (control).
- **Alt (fully analog, hand-solderable):** **PCM5102A** I²S DAC (TSSOP-20) → **TPA3116D2** analog Class-D (HTSSOP-32); all DSP in firmware.

## 6. Toshiba TB6612FNG — Dual H-Bridge Motor Driver *(× 2, chosen stepper driver)*

- **Product:** Dual H-bridge with **low-Rds MOSFET outputs**. **One TB6612FNG per shaft** (2 H-bridges = the shaft's 2 coils); **two chips** for the X40.879.
- **Refs:** Part # `TB6612FNG` (orderable **`TB6612FNG,C,8,EL`**) · Mfr **Toshiba** · datasheet (rev 20141001). DigiKey # 1730070.
- **Price / link:** ~**$2.40/1** — [DigiKey 1730070](https://www.digikey.com/en/products/detail/toshiba-semiconductor-and-storage/TB6612FNG-C-8-EL/1730070) (active, ships today).
- **Package:** **SSOP-24 (5.3 × 8.2 mm, 0.65 mm pitch, NO exposed pad)** — pure gullwing, the **easiest** part on the board to hand-solder (iron only, no belly pad). This is *why* it wins over the WSON DRV8835 and HTSSOP DRV8833.
- **Power / IO:**
  - **Voltage:** **VM 0–13.5 V** motor supply (run at **5 V** for full stepper torque) + **VCC 2.7–5.5 V** logic (**3.3 V** from the MCU). Internal thermal shutdown + flyback diodes.
  - **Current:** **1.2 A/ch continuous (3.2 A peak)** — vs the ~19 mA the coils need → huge margin, runs cold.
  - **IO (per chip, 3.3 V logic):** channel A = `AIN1`,`AIN2`,`PWMA`; channel B = `BIN1`,`BIN2`,`PWMB`; plus `STBY`. **Pin-efficient microstep scheme:** tie `PWMA`/`PWMB` **high** and **PWM the four IN pins** for sign-magnitude microstep → **4 GPIO/chip = 8 total** + one shared `STBY` (parity with the DRV8835 it replaces). *(The textbook scheme — PWM on `PWMx`, direction on the IN pins — is 6/chip = 12 GPIO; avoid it here to protect the pin budget.)* Outputs AO1/AO2, BO1/BO2 to the coils. Decouple VM & VCC (0.1 µF min + bulk).
- **Description:** Integrated flyback (protects the MCU), low-drop MOSFET outputs → full 5 V coil drive, thermal shutdown. No internal current chopper → microstepping is PWM/voltage-based (ideal for these high-impedance gauge coils). **Do not** use A4988/DRV8825/DRV8434/TMC-class choppers — they can't regulate ~19 mA into 260 Ω.
- **Interface:** GPIO + PWM (IN/IN sign-magnitude). Homing via **1× ITR8307 reflective optical** (§16, separate — no hand magnets).
- **Released:** long-standing, active (Toshiba Bi-CD).

## 7. WCH CH224K — USB-PD Sink Controller *(resistor-configured)*

- **Product:** autonomous USB Type-C / PD **sink** controller — negotiates a PD contract **with no MCU**; the requested voltage is set by a **single resistor** (CFG), no NVM programming.
- **Refs:** `CH224K` · **WCH / Nanjing Qinheng** · **ESSOP-10** (SOP-10, leaded, exposed-pad-free enough to iron). LCSC # C970725. *(Not on DigiKey — sourced from LCSC/Mouser; the exception to the DigiKey-first rule, since every DigiKey PD sink is QFN.)*
- **Price:** ~$0.40 (active; drop-in successor `CH224A` if EOL).
- **Power / IO:** **VBUS 4–22 V**; VDD off VBUS. Standalone: a resistor on **CFG1** selects the requested PDO (**5 / 9 / 12 / 15 / 20 V**); built-in **OVP + OTP**; **PG** (power-good, open-drain) → 1 GPIO. Supports PD3.0/2.0 + BC1.2.
- **Use:** set **CFG for 15 V** (headroom for the LED string + LT3652 VIN + the audio/rail boosts); auto-requests 15 V, **falls back to 5 V** if the source can't supply it (system still runs, LED dimmer). VBUS → LT3652 VIN + LED CC driver. Read PG to know a high-voltage contract is live.
- **Interface:** USB-PD (CC) + resistor config + PG.

## 8. Analog Devices LT3652 — 1-Cell Buck Battery Charger *(chosen charger)*

- **Product:** monolithic **step-down (buck) battery charger**, 4.95–32 V input, **up to 2 A** charge, constant-current/constant-voltage. Fully **autonomous** (resistor/capacitor-programmed — no I²C).
- **Refs:** `LT3652` (orderable **`LT3652EMSE#PBF`**) · **Analog Devices** · **MSOP-12E** (12-lead MSOP, PowerPAD) *(3×3 DFN-12 also exists — use the MSOP)*. DigiKey # 2225686. Datasheet `3652fe`.
- **Price:** ~$5–6 (active).
- **Power / IO:**
  - **VIN:** **4.95–32 V** operating (abs-max 40 V) → the **15 V VBUS** from CH224K is well inside range.
  - **Float voltage:** set by an **external divider to the 3.3 V feedback reference** → programmed to **4.05 V** (health cap). Optional **4.2 V "full" mode** by switching a parallel FB resistor with a small FET/GPIO.
  - **Charge current:** set by a sense resistor (V_SENSE(chg) 100 mV / R_SENSE), here ~1–1.75 A.
  - **Temperature:** **NTC pin** — connects the holder's 10 k NTC; **charging pauses hot/cold** (temperature-qualified, meets "no charge <0/>45 °C"). *(Single hot/cold window, not multi-zone JEITA — documented downgrade from the BQ25628E it replaces.)*
  - **Safety timer:** programmable via the **CTIMER** cap; C/10 termination; auto-recharge; **CHRG + FAULT** open-drain status → 2 GPIO.
- **System role (power-path):** the **BAT node is the always-on system supply** for the rail converters. When plugged, the LT3652 regulates BAT to 4.05 V and sources load **up to 2 A → runs with no cell**; when unplugged, the cell supplies the rails through the protector FETs. No separate NVDC power-path IC needed.
- **Health cap & telemetry:** the 4.05 V cap is enforced **in hardware by the float divider** (no firmware needed — simpler and safer); SoC/faults read via **ADC + CHRG/FAULT** instead of I²C.
- **Interface:** none (autonomous); status pins + NTC.

## 9. ABLIC S-8261 + 10. Alpha & Omega AO4800 — 1-Cell Protector *(independent safety)*

- **Product:** discrete 1-cell Li-ion **protection IC (S-8261)** + **external dual N-channel MOSFET (AO4800)** — the independent OV/OD/OCD/OCC/SCP cutoff, redundant to the LT3652. Replaces the DFN integrated-FET LC05111 with two **leaded, hand-solderable** parts (the ubiquitous "protector-IC + 8205-style dual-FET" topology).
- **Refs:**
  - Protector: `S-8261` — pick **`S-8261AAxMD`** (**overcharge detect 4.28 V**, matching the old LC05111C02) · **ABLIC** · **SOT-23-6** · [DigiKey S-8261 series](https://www.digikey.com/htmldatasheets/production/9482/0/0/1/s-8261-series.html). *(Confirm the exact suffix's OV/OD thresholds against the datasheet voltage table before ordering.)*
  - FET: `AO4800` — **dual N-channel, 30 V, SO-8**, low-Rds · **Alpha & Omega Semiconductor** · DigiKey-stocked (keyword `AO4800`).
- **Price:** ~$0.4 (S-8261) + ~$0.3 (AO4800).
- **Power / IO:** protector monitors cell voltage/current and drives the two series FETs (charge + discharge) in the cell **−** path. **S-8261:** overcharge **~4.28 V** (variant-fixed), over-discharge ~2.3–2.5 V, overcurrent + short-circuit detection — **autonomous, no config**. **AO4800:** VDS 30 V, low Rds(on), continuous current far above our <2 A.
- **Use:** wire the AO4800 dual FET in the cell **−** path between the 18650 and PACK−, gates from the S-8261; a couple of external R/C set the delays. Belt-and-suspenders cutoff if the LT3652 fails. Overcharge is thus **double-redundant** (LT3652 CV 4.05 V + S-8261 4.28 V).
- **Interface:** none (autonomous).

---

## 11. Hirose FH34SRJ-10S-0.5SH — Display FPC/ZIF Connector *(mates the LS032B7DD02 tail)*

- **Product:** 10-position, **0.5 mm-pitch** FPC/FFC **ZIF** connector, **dual (upper + lower) contact**, SMT right-angle, back-flip actuator, 1.0 mm profile, accepts 0.3 mm FPC, 0.5 A/contact.
- **Refs:** `FH34SRJ-10S-0.5SH(50)` · **Hirose** · **DigiKey # 3880272**. The LS032B7DD02 device spec **Table 8-2-1 lists it as a recommended mating connector** (alternatives it names: HRS **FH28-10S-0.5SH** bottom-contact, **Molex 503480-1000**, **Panasonic AYF531035**).
- **Why this one:** the panel tail is a **10-pin, 0.5 mm-pitch FPC** — pinout (datasheet Table 4-1 / 8-1-1): `1 SCLK · 2 SI · 3 SCS · 4 EXTCOMIN · 5 DISP · 6 VDDA · 7 VDD · 8 EXTMODE · 9 VSS · 10 VSSA`. Dual-contact + reinforced actuator = best retention for a moving/rotating body.
- **Hand-assembly:** a **0.5 mm ZIF FPC connector is explicitly allowed** by the mfg rule (fine-pitch FPC/ZIF) → mounts **on the main PCB** (not a breakout). Slide the FPC in, flip the latch.
- **Power / IO:** passive; carries the display's 3-wire SPI + DISP/EXTCOMIN/EXTMODE. Panel VDD/VDDA **5 V**, logic **3 V** (§1).
- **Price:** ~$0.7.

## 12. Bosch BME688 — Environmental Combo *(chosen env part — one chip = T + RH + pressure + VOC/gas)*

- **Product:** 4-in-1 environmental sensor — **temperature + humidity + barometric pressure + VOC/gas resistance** with the **BSEC** library computing an IAQ/air-quality index. One chip does the whole climate + air-quality job → **one sensor on the bus, one board on the chain**.
- **Refs:** `BME688` · **Bosch Sensortec** · **LGA-8 (3×3 mm)** · datasheet `BST-BME688-DS000` (`sensor_env_bme688.pdf`).
- **Power / IO:** 1.71–3.6 V; I²C (addr 0x76/0x77) **or** SPI; ~3.7 µA @ 1 Hz T/RH/P, gas heater is the power draw (duty-cycle it).
- **Accuracy:** T **±0.5 °C**, RH **±3 %RH**, P ±0.6 hPa — plenty for a room-comfort readout; the win is folding T/RH/VOC/AQI onto **one** part.
- **Hand-assembly:** LGA leadless → **module. 2a board = Adafruit 5046 (BME688, STEMMA QT), ~$19** ([DK # 14313482](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5046/14313482)); on **2b** the bare BME688 is machine-placed on the daughterboard (same chip → firmware unchanged).
- **Firmware:** Bosch **BSEC 2.x** (BME68x) for IAQ; raw T/RH/P/gas without it. Root README §6c/§8.
- **Interface:** I²C (shared) + optional INT.
- **Price:** ~$5 bare / ~$19 board.

## 13. ams-OSRAM TSL2591 — High-Dynamic-Range Ambient Light Sensor *(weak-light sensitive)*

- **Product:** I²C ALS with **full-spectrum + IR photodiodes**, **600,000,000:1 dynamic range → 188 µlux to 88,000 lux** (gain 1×–9876×, integration 100–600 ms). Far more sensitive in near-dark than the VEML7700 (~0.0036 lux floor) → resolves a **very dim bedroom**.
- **Refs:** `TSL25911FN` · **ams-OSRAM** · **WFDFN-6 (2×2 mm)** · **DigiKey # 4162547** · datasheet `TSL2591 (Apr-2013)` (`sensor_light_tsl2591.pdf`).
- **Power / IO:** 2.7–3.6 V, ~90 µA active / ~3 µA sleep; I²C + INT (open-drain, programmable thresholds).
- **Hand-assembly:** WFDFN leadless → **module. 2a board = Adafruit 1980 (TSL2591, STEMMA QT), $6.95** ([DK # 4990786](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1980/4990786)); on **2b** the bare TSL2591 is machine-placed on the daughterboard.
- **Use:** ALS auto-dim (front-light gate, halo brightness) + the "true 0 emission at night" logic — the sub-millilux floor detects a fully dark room. Lux from CH0(full)/CH1(IR).
- **Alt:** **Vishay VEML7700** — outputs lux directly, simpler/cheaper, floor ~0.0036 lux (adequate but less sensitive); Adafruit 4162, ~$5.
- **Price:** ~$3 bare / **$6.95 board**.

## 14. ST LIS3DH — Triple-Axis Accelerometer *(tap-to-snooze + flat/standing, R14)*

- **Product:** 3-axis ±2/4/8/16 g accelerometer, 10-bit, on-chip **tap / double-tap** and low-power modes with two programmable interrupts. I²C/SPI. Low-res is fine here — flat-vs-standing is a static gravity read and tap is an IRQ.
- **Refs:** `LIS3DH` · **STMicroelectronics** · **LGA-16 (3×3 mm)** · datasheet `LIS3DH` (`sensor_accel_lis3dh.pdf`).
- **Power / IO:** 1.71–3.6 V; ~2 µA low-power to ~11 µA normal; two INT pins. **Gravity-vector read → static flat-vs-standing (R14)**; **hardware tap IRQ** wakes the MCU for **tap-to-snooze (R3)** so it can sleep between events.
- **Hand-assembly:** LGA leadless → **module. 2a board = Adafruit 2809 (LIS3DH, STEMMA QT), $4.95** ([DK # 5774319](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2809/5774319)); on **2b** the bare LIS3DH is machine-placed on the daughterboard. Mount rigidly to the body so gravity reads true.
- **Firmware:** Adafruit_LIS3DH / esp-idf-lib. Well-supported; same driver whether 2a board or 2b bare.
- **Interface:** I²C (shared) + INT.
- **Price:** ~$2 bare / ~$5 board.

## 15. Sensor build — option 2a (chosen) vs 2b (future) — *identical sensor set*

**Both paths carry the same three sensors — BME688 + TSL2591 + LIS3DH — so the firmware is identical either way.** Every one is leadless, so the cluster is built as **modules**, never bare on the main PCB:

- **2a — chosen now: STEMMA QT / Qwiic daisy-chain.** Three ready Adafruit boards, one 4-wire I²C chain, zero leadless soldering:

  | Sensor | Board | ~Price | Ref |
  |--------|-------|--------|-----|
  | Env (T/RH/press/VOC) | **Adafruit 5046** (BME688) | ~$19 | [DK 14313482](https://www.digikey.com/en/products/detail/adafruit-industries-llc/5046/14313482) |
  | Light (weak-light) | **Adafruit 1980** (TSL2591) | $6.95 | [DK 4990786](https://www.digikey.com/en/products/detail/adafruit-industries-llc/1980/4990786) |
  | Accel (tap+orient) | **Adafruit 2809** (LIS3DH) | $4.95 | [DK 5774319](https://www.digikey.com/en/products/detail/adafruit-industries-llc/2809/5774319) |

  Chain to the main board over one JST-SH STEMMA QT cable → SDA/SCL/3V3/GND. ≈ **$31** for the three.

- **2b — future: one custom sensor daughterboard.** The **same three bare chips** (BME688 + TSL2591 + LIS3DH — §12/13/14) on a small PCB, **JLCPCB/PCBA machine-placed**; you **hand-solder only its 0.1″ header / castellated edge** to the main board. Never an iron on a leadless pad → mfg rule intact; smallest footprint; one physical board to install. **Same part numbers → same I²C addresses → the 2a firmware runs unchanged.**

## 16. Everlight ITR8307 — Reflective Optical Sensor *(hand homing, root README §5)*

- **Product:** reflective photo-interrupter — GaAs IR-LED + phototransistor mounted **side-by-side** in a tiny plastic package; senses a high-contrast index mark on each hand's underside as it sweeps a **hole punched in the dial**. Short-range (target a few mm away).
- **Refs:** Part # `ITR8307/TR8` · Mfr **Everlight** · **DigiKey # 2693862** · datasheet rev 4.
- **Price / link:** ~**$0.5** — [DigiKey 2693862](https://www.digikey.com/en/products/detail/everlight-electronics-co-ltd/ITR8307-TR8/2693862) (active).
- **Dimensions (L × W × H):** **3.4 × 1.5 × 1.1 mm** (4-SMD) — the **smallest reflective sensor evaluated → smallest dial hole** (the design goal). Gullwing 4-SMD = hand-solderable, mounts **on the main PCB** (not a breakout).
- **Power / IO:** IR-LED forward ~1.2 V at a few mA (external R sets I_F); phototransistor collector to **3.3 V** through a load → an **analog level read by 1 ESP32 ADC pin (or a comparator)**. No magnets, no I²C.
- **Use (§5):** one sensor behind the dial; **sequential per-shaft homing** removes hand-ID ambiguity (park one hand off-index, sweep the other to the edge; repeat). Re-home on boot / after NTP / after long runs; step-count between.
- **Why it (vs TCND5000):** the Vishay **TCND5000** (**6 × 4.3 × 3.75 mm**, peak 6 mm, range 2–25 mm) reaches farther but is **~5× the footprint** → a bigger hole, so it was **dropped**. ITR8307's short range is fine because the hands sweep **<3 mm** off the dial. *(Replaces the earlier 2× DRV5032 Hall + hand magnets.)*
- **Interface:** analog reflective (ADC/comparator edge).

## 17. Timekeeping — no RTC IC *(ESP32-S3 RTC + 32.768 kHz crystal)*

**Decision: no dedicated RTC chip, no coin cell.** A battery-less RTC IC would lose time on total power loss exactly as the S3 does, so it adds parts without buying anything. The S3's *own* RTC is fine **as long as it runs off a good reference** — its internal RC oscillator drifts %-level over temperature (amp/LED heat nearby), so:

- **Add a 32.768 kHz crystal** on the S3's **XTAL32K** pins (GPIO15/16) → the RTC slow clock runs at **~±20 ppm ≈ 1.7 s/day**, disciplined by **SNTP** whenever online (even a daily sync keeps error to a couple seconds). Part now **locked = Abracon ABS07-32.768KHZ-T** (3.2×1.5 mm 2-SMD, **CL 12.5 pF** → size the two load caps to CL, ~9–10 pF each after stray), ~$0.3, hand-solderable — **datasheet filed (row 16, `xtal_32k_abs07.pdf`)**.
- **The S3 is essentially always powered** (USB or the 18650 on the always-on BAT node), so the RTC domain rarely dies. If **both** USB and the 18650 are gone, time is lost → **re-sync via SNTP on next boot**, running a clock animation meanwhile (accepted, keeps HW simple).
- *If you ever wanted true powered-off holdover* it would take an RTC IC **plus** a coin cell/supercap (e.g. RV-3028-C7 45 nA, or DS3231SN SOIC-16 TCXO) — explicitly **out of scope** here (no second battery).

See root README §1/§6c/§8/§10.

---

## 18. Power-path & interconnect parts *(rows 16–25 — locked 2026-07-05)*

The remaining ⚠️ Production-BOM rows, now locked to exact hand-solderable parts + filed datasheets (LEDs deferred — too many opens). All DigiKey-active/orderable unless noted.

- **16 — Abracon `ABS07-32.768KHZ-T`** (32.768 kHz xtal) · DK [1236858](https://www.digikey.com/en/products/detail/abracon-llc/ABS07-32-768KHZ-T/1236858) · 3.2×1.5 mm 2-SMD · **CL 12.5 pF**, ±20 ppm, ESR ≤70 kΩ · S3 XTAL32K (GPIO15/16); load caps ≈ 2·(CL − Cstray). ~$0.3.
- **17 — Alpha & Omega `AO3401A`** (reverse-polarity P-FET) · DK [1855773](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO3401A/1855773) · **SOT-23-3** · −30 V, −4 A, R<sub>DS(on)</sub> ≤50 mΩ @ −10 V. Series P-FET on BAT+ (bare cell can't be keyed). ~$0.24.
- **18 — Murata `NCP18XH103F03RB`** (10 k NTC) · DK [1644665](https://www.digikey.com/en/products/detail/murata-electronics/NCP18XH103F03RB/1644665) · **0603** (⚠️ BOM §16b said "0402" — corrected; NCP18 = 0603, meets the ≥0603 rule) · 10 kΩ ±1 %, **B25/50 = 3380 K**. Wires to the LT3652 NTC pin for hot/cold charge-qualify. ~$0.10.
- **19 — Littelfuse `SMAJ22A` + `SMAJ5.0A`** (TVS) · DK [762286](https://www.digikey.com/en/products/detail/littelfuse-inc/SMAJ22A/762286) / [762250](https://www.digikey.com/en/products/detail/littelfuse-inc/SMAJ5-0A/762250) · **DO-214AC (SMA)** · 400 W uni; SMAJ22A (Vwm 22 V) on VBUS (15 V PD + headroom), SMAJ5.0A (Vwm 5 V) on the BAT node. ~$0.4 ea. *(Filed datasheet is the Bourns-published SMAJ series — identical JEDEC part numbers/specs; Littelfuse's own PDF is Akamai/bot-blocked from direct download.)*
- **20 — TI `TPS55340PWPR`** (12 V audio boost, gated) · DK [3727185](https://www.digikey.com/en/products/detail/texas-instruments/TPS55340PWPR/3727185) · **HTSSOP-14 (PWP, PowerPAD)** · 2.9–32 V in → 3–38 V out, **5 A/40 V** integrated FET, 1.2 MHz. **Replaces the BOM's LM2587S-ADJ** — that part is obsolete-leaded and its active `/NOPB` is **$12.59 & 0 stock**; TPS55340 is leaded/hand-solderable, in-stock, ~5× cheaper, ample for the TAS5760M PVDD. ~$2.5.
- **21 — TI `TPS61023DRLR`** (5 V rail boost) · DK [11310667](https://www.digikey.com/en/products/detail/texas-instruments/TPS61023DRLR/11310667) · **SOT-563** (small but gullwing → hand-solderable) · 0.5–5.5 V in, **3.7 A** valley limit, 94 % @ 3.6→5 V. Chosen over MT3608 (off-DigiKey). ~$1.2.
- **22 — TI `TLV62569DBVR`** (3.3 V rail buck) · DK [7688370](https://www.digikey.com/en/products/detail/texas-instruments/TLV62569DBVR/7688370) · **SOT-23-6** · 2.5–5.5 V in, **2 A** sync buck, 1.5 MHz. Bucks the 5 V rail → 3.3 V logic (over the AMS1117 LDO alt — far better efficiency). ~$0.25.
- **23 — Keystone `1043`** (18650 holder) · DK [2745669](https://www.digikey.com/en/products/detail/keystone-electronics/1043/2745669) · TH leaf-spring PC-pin, UL94V-0 nylon · single-cell, user-replaceable. Mount in the ventilated FR/metal-barriered compartment per `../power.md`. ~$2.9.
- **24 — Hirose `DM3AT-SF-PEJM5`** (microSD) · DK [2533565](https://www.digikey.com/en/products/detail/hirose-electric-co-ltd/DM3AT-SF-PEJM5/2533565) · **push-push SMT R/A**, 8-pos, gold, 10 k cycles · SD over the S3 SPI bus (4 pins). ~$2.85.
- **25 — GCT `USB4105-GF-A`** (USB-C receptacle) · DK [11198441](https://www.digikey.com/en/products/detail/gct/USB4105-GF-A/11198441) · USB 2.0 Type-C, **5 A**, SMT body + **TH hold-down tabs** (rugged), 20 k cycles · VBUS/CC to the CH224K PD sink. ~$0.8.

**⚠️ Still open (deferred — LEDs):** halo LEDs (SK6812MINI-E) + LED CC driver (TPS92200) + front-light — *too many opens (count, layout, driver topology, VBUS gating); revisit separately.*

---

## 19. Microchip MCP23017 — I²C 16-Bit IO Expander *(offloads slow control lines)*

- **Refs:** `MCP23017` (I²C; pin-compatible SPI twin `MCP23S17`) · **Microchip** · **SOIC-28 wide / SSOP-28** (both hand-solderable gullwing; **avoid the 28-QFN**). Datasheet **DS20001952** (`expander_mcp23017.pdf`).
- **Price:** ~$1.3 (active, widely stocked).
- **Power / IO:** 1.8–5.5 V (run at **3.3 V** off the logic rail); **16 bidirectional I/O** in two 8-bit ports; **3 hardware address pins** → up to 8 devices per bus; **INTA/INTB interrupt-on-change** (configurable active-hi/lo/open-drain, mirrorable). I²C to **1.7 MHz** (MCP23S17 SPI to 10 MHz).
- **Use:** hangs on the **existing shared I²C bus** (sensors + amp) → **zero new bus GPIO**; costs **1 MCU pin for INT** so button/knob presses wake without polling. Offloads *slow / static* lines off the pin-starved S3: **SPK_SD** (amp mute), **12 V-boost EN**, stepper **STBY**, **rear buttons + encoder push-switch**. **Net −4 GPIO** on the host.
- **Do NOT offload (hard limits):** the 8 stepper **AIN/BIN are PWM** (>20 kHz carrier for silent microstep) — an expander can't bit-bang them, and a **PCA9685 PWM expander tops out ~1.5 kHz**, ~15× too slow; keep them on **MCPWM**. **Encoder A/B quadrature** stays on native GPIO via the S3 **PCNT** counter (an I²C-polled expander drops steps on a fast spin). I²S, SPI, RMT (SK6812) and the ADC inputs likewise stay on the MCU.
- **Interface:** I²C (2 shared) + 1 INT GPIO. Prefer I²C over the SD-SPI bus — I²C adds **no** MCU pin, SPI would cost a CS.

---

## Reconciliation with root README (v0.15)

All parts match the root [`README.md`](../README.md):

| Item | Status |
|------|--------|
| **Hand-solderable-only** | ✅ every bare-PCB IC is leaded (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23) or a castellated module; no QFN/DFN/BGA/LGA on the board. |
| **Speaker** | ✅ DMA58-4 chosen; PC68-4 kept as documented "bigger-box alt". |
| **Stepper** | ✅ X40.879 (+ X27 companion datasheet); root §5 explains the dependency. |
| **Audio amp** | ✅ **TAS5760M** (HTSSOP) locked, replacing the QFN TAS5825M; firmware does the HPF/limiter DSP. PCM5102A+TPA3116 = analog alt. |
| **Motor driver** | ✅ **2× TB6612FNG** (SSOP-24) locked, replacing the WSON DRV8835. |
| **Power / safety** | ✅ **CH224K** (PD) + **LT3652** (1S buck charger, BAT-node power-path) + **S-8261 + AO4800** protector + reverse P-FET + NTC + TVS. Fuel gauge → **ESP32 ADC**. See [`../power.md`](../power.md). |
| **Charger telemetry** | ⚠️ **downgrade:** LT3652 has **no I²C** and a **single-window NTC** (not multi-zone JEITA). Health-cap 4.05 V is enforced in hardware; SoC/faults via ADC + status pins. Double-redundant overcharge preserved. |
| **Display connector** | ✅ **FH34SRJ-10S-0.5SH** (Hirose, 10-pin 0.5 mm ZIF) — the panel spec's own recommended mate; **on-board** (FPC/ZIF is hand-solderable). |
| **Sensors (v0.15 consolidated)** | ✅ env **BME688** (T/RH/press/**VOC** — one chip = climate + air-quality) · light **TSL2591** (weak-light) · accel **LIS3DH** (tap + orient). All leadless → **I²C modules**, **same set on both paths**: **2a** = Adafruit STEMMA QT chain (5046 + 1980 + 2809); **2b** = future custom daughterboard (same 3 bare chips → firmware unchanged). Dropped TMP117/SHT45/SGP41/BMA400 datasheets. |
| **Hand homing** | ✅ **1× Everlight ITR8307** reflective optical (§16, 4-SMD, on-board) — single sensor + punched dial hole, sequential, **no hand magnets**. Replaces 2× DRV5032 Hall; **TCND5000 evaluated & dropped** (6×4.3×3.75 mm ≈ 5× bigger → bigger hole). |
| **Timekeeping** | ✅ **No RTC IC / no battery** — S3 internal RTC off a **32.768 kHz crystal** (XTAL32K) + SNTP (~±20 ppm ≈ 1.7 s/day). Total power loss → re-sync on boot (clock animation). RV-3028-C7/DS3231SN datasheet removed. Xtal now locked = **ABS07-32.768KHZ-T** (row 16). |
| **IO expander (new)** | ✅ **MCP23017** (SOIC/SSOP-28) on the shared I²C + INT — offloads SPK_SD / 12 V EN / stepper STBY / buttons / enc-SW (**net −4 GPIO**). Steppers stay on **MCPWM**, encoder on **PCNT** — *not* the expander (PWM/quadrature can't be offloaded). See §19. |
| **Power-path & interconnect** | ✅ **rows 16–25 locked + datasheeted** (2026-07-05): xtal ABS07-32.768KHZ-T · reverse P-FET AO3401A · NTC NCP18XH103F03RB (**0603**) · TVS SMAJ22A+SMAJ5.0A · **12 V boost TPS55340PWPR** (replaces LM2587S-ADJ) · 5 V boost TPS61023DRLR · 3.3 V buck TLV62569DBVR · holder Keystone 1043 · microSD DM3AT-SF-PEJM5 · USB-C USB4105-GF-A. All hand-solderable, DigiKey-active. **LEDs deferred.** |

**No open discrepancies.** Corrections this pass: NTC size **0402 → 0603** (NCP18 is 0603; BOM cell fixed), and audio boost **LM2587S-ADJ → TPS55340PWPR** (the former's active `/NOPB` is $12.59 & out of stock). Intentional non-"matches": **PC68-4** (speaker *alternative*), **CH224K off-DigiKey** (no hand-solderable DigiKey PD sink), and the filed **TVS datasheet is the Bourns-published SMAJ** series (identical JEDEC part; Littelfuse's own PDF is Akamai/bot-blocked). **Still open:** LED subsystem (halo SK6812MINI-E + CC driver + front-light) — deferred.
