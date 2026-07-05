# Datasheet Summary

Quick-reference for the datasheets in this folder. Prices are single-unit USD and approximate — click through to verify live stock/price. All parts are **currently active/orderable** (verified 2026-07-04) and **match the root [`README.md`](../README.md)** (v0.12).

> 🔩 **HAND-SOLDERABLE PARTS ONLY (hard requirement).** The bare PCB is fab'd externally; **every part is soldered by hand** with an iron. So **no QFN / DFN / WSON / BGA / WLP / LGA** parts sit bare on the board — every active IC here is a **leaded/gullwing** package (SOIC / SOP / SSOP / TSSOP / **HTSSOP** / **MSOP** / SOT-23) or a **castellated/edge module**. Two power parts (amp, charger) are HTSSOP/MSOP **PowerPAD**: the leads are iron-solderable and the belly pad is grounded through a thermal-via array (back-side hot-air optional). This trades **board area** for hand-assembly — accepted. Parts that *only* exist leadless (env/MEMS sensors, fuel gauge) are pushed onto **pre-made breakout modules** or **dropped**; see the root README manufacturing section.

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
| 12 | `sensor_temp_tmp117.pdf` | **TMP117** (`TMP117AIDRVR`) | Texas Instruments | WSON-6 → **breakout** | ✅ | ~$1.9 | I²C (±0.1 °C) |
| 13 | `sensor_humidity_temp_sht4x.pdf` | **SHT45** (`SHT45-AD1B-R2`) | Sensirion | DFN-4 → **breakout** | ✅ | ~$4.5 | I²C (±0.1 °C / ±1 %RH) |
| 14 | `sensor_light_tsl2591.pdf` | **TSL2591** (`TSL25911FN`) | ams-OSRAM | WFDFN-6 → **breakout** | ✅ | ~$3 | I²C (188 µlx–88 klx) |
| 15 | `sensor_accel_bma400.pdf` | **BMA400** | Bosch Sensortec | LGA-12 → **breakout** | ✅ | ~$1.6 | I²C/SPI (tap + orient) |

**Sensors ride on I²C breakout modules** (rows 12–15): every one is leadless (WSON/DFN/WFDFN/LGA), so per the hand-solder rule they mount on **Qwiic/STEMMA breakouts** on a sensor flex, away from the main-PCB heat — not bare silicon. The **display connector (row 11) is the exception**: a 0.5 mm FPC/ZIF is explicitly hand-solderable, so it sits **on the main PCB**. **No RTC IC** — timekeeping is the **ESP32-S3 internal RTC off a 32.768 kHz crystal** (XTAL32K, GPIO15/16) + SNTP; no coin cell (see root README §6/§8/§10).

**Fuel gauge dropped** (no hand-solderable equivalent — every 1-cell gauge is TDFN/WLP): SoC is now **voltage-based via the ESP32-S3 ADC** on a divider off the cell. See root README §10 / `../power.md`.

**Removed datasheets (leadless, replaced above):** `amp_tas5825m.pdf` (VQFN-32), `motor_driver_drv8835.pdf` (WSON-12), `pd_sink_stusb4500.pdf` (QFN-24), `charger_bq25628e.pdf` (WQFN-18), `fuel_gauge_max17048.pdf` (µDFN/WLP), `battery_protector_lc05111cmt.pdf` (WDFN-6).

---

## System IO & power domains

The pin/rail picture is getting busy, so track it here. The **ESP32-S3 (3.3 V logic)** is the host; everything else hangs off it. Rails (see [`../power.md`](../power.md) for the full tree + battery safety): **3.3 V** (logic, from the 5 V rail) · **5 V** (display panel + stepper VM) · **12 V boost** (amp PVDD, gated) · **15 V VBUS** (LED, plugged). Source: **USB-PD 15 V** (CH224K) → LT3652 buck charger, 1S Li-ion 18650; the charger **BAT node is the always-on system supply**.

| Part | Power rail(s) (abs-max) | Logic level | ESP32-S3 signals (GPIO count) |
|------|-------------------------|-------------|-------------------------------|
| **ESP32-S3-WROOM-1** (host) | 3.0–3.6 V (max 3.6) | 3.3 V | — drives everything below; ≤36 GPIO to budget |
| **LS032B7DD02** display | Panel VDD/VDDA **5 V** (4.8–5.5; abs 5.8) | **3 V** inputs | SPI SCLK · SI · SCS + DISP + EXTCOMIN → **~4–5** (EXTMODE tied to VDD) |
| **TAS5760M** amp | PVDD **12 V** (boost; range 4.5–26.4) + DVDD **3.3 V** | 3.3 V (DVDD-ref) | I²S BCLK · LRCLK · SDIN + SPK_SD (mute/PDN) + I²C(shared) → **~4 + shared I²C** |
| **DMA58-4** speaker | — (passive, from amp OUT) | — | none (analog off TAS5760M PBTL output) |
| **TB6612FNG × 2** driver | VM **5 V** (0–13.5, abs 15) + VCC **3.3 V** (2.7–5.5) | 3.3 V | AIN1·AIN2·BIN1·BIN2 (PWM'd) + STBY → **4 / chip = 8** + shared STBY |
| **X40.879** stepper | coils fed from TB6612FNG VM **5 V** | — | none direct (via TB6612FNG); 2 shafts × 2 coils |
| **CH224K** PD sink | VBUS **4–22 V**; VDD off VBUS | 3.3 V (PG open-drain) | CFG (resistors, no GPIO) + PG → **~1** |
| **LT3652** charger | VIN (VBUS) **≤32 V**; BAT (system node) | 3.3 V (open-drain status) | CHRG + FAULT status → **~2**; NTC/timer/float = passives |
| **Cell voltage sense** | off cell via divider | ADC | 1 ADC pin → **1** |
| **S-8261 + AO4800** protector | across cell (≤12 V) | — | none (autonomous OV/OD/OC/SC) |

**Shared bus:** the **I²C bus** (SDA/SCL, 2 GPIO) is shared by the amp + all sensors (**SHT45** T/RH, **TMP117** temp *(opt)*, **SGP41** VOC, **TSL2591** light, **BMA400** accel — all now datasheeted, rows 12–15) — **kept small** (no charger, no fuel gauge, **no RTC** on I²C). Timekeeping = S3 RTC + a **32.768 kHz crystal** on XTAL32K (not on I²C). Homing uses **2× DRV5032** Hall (SOT-23, 3.3 V, open-drain → 2 GPIO). Rough running total: I²S 3 + I²C 2 + display SPI ~5 + steppers 8 + Halls 2 + amp mute 1 + SD (**SPI 4** to save pins) + charger status 2 + Vbat ADC 1 + PD PG 1 + LEDs ~2 + encoder/buttons ~4 ≈ **31–35 GPIO** → fits ≤36, but reserve a strapping-safe map early. **The steppers are still 8 pins** — the TB6612FNG is driven PWM-on-the-inputs (PWMA/PWMB tied high) to keep parity with the DRV8835 it replaces (the conventional PWM-pin scheme would cost 12).

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
- **Description:** 1/3° per step (60°/step rotor), up to 600°/s, 1/180 gear. Base X27 has a 315° internal stop — for a clock, buy/reuse the **360°/no-stop** variant + external Hall homing.
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
- **Interface:** GPIO + PWM (IN/IN sign-magnitude). Homing via 2× DRV5032 Hall (SOT-23, separate).
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

## 12. TI TMP117 — ±0.1 °C Precision Temperature Sensor *(dedicated ambient temp)*

- **Product:** 16-bit digital temp sensor, **±0.1 °C (max) over −20…+50 °C with no calibration**, 0.0078 °C resolution, 48-bit EEPROM, PT100/PT1000 RTD-class. I²C/SMBus, up to 4/bus, programmable ALERT.
- **Refs:** `TMP117AIDRVR` · **Texas Instruments** · **WSON-6 (2×2 mm)** · **DigiKey # 9685284** · datasheet `SNIS189`.
- **Power / IO:** 1.8–5.5 V, **3.5 µA typ**; I²C (3.3 V-compatible) + optional ALERT.
- **Hand-assembly:** WSON is leadless → **breakout** (Adafruit 4821, Qwiic/STEMMA) on the sensor flex, away from self-heating parts.
- **Role:** meets "ambient temp to **0.1 °C**." **Optional / redundant if the SHT45 (§13) is fitted** (SHT45 temp is also ±0.1 °C) — add TMP117 only for a fast, RH-decoupled, traceable temp reference.
- **Price:** ~$1.9 bare / ~$5 breakout.

## 13. Sensirion SHT45 — ±1.0 %RH / ±0.1 °C Humidity + Temp *(accurate ambient RH — also covers temp)*

- **Product:** 4th-gen capacitive **RH + temp** sensor; **±1.0 %RH** and **±0.1 °C** — the ultra-high-accuracy grade of the SHT4x family (tighter than the SHT40's ±1.8 %RH / ±0.2 °C). On-chip heater; I²C.
- **Refs:** `SHT45-AD1B-R2` · **Sensirion** · **DFN-4 (1.5×1.5 mm)** · **DigiKey # 16360966** · datasheet `Datasheet_SHT4x`. (`SHT45-AD1F-R2`, DK # 17180856 = with PTFE membrane/filter.)
- **Power / IO:** 1.08–3.6 V, ~0.4 µA avg; I²C addr 0x44.
- **Hand-assembly:** DFN leadless → **breakout** (Adafruit 5665 / SparkFun) on the vented sensor flex. **Also supplies the RH/T compensation the SGP41 VOC sensor requires.**
- **Role:** satisfies **humidity (accurate)** *and* **temperature (0.1 °C)** in one part → can stand in for the TMP117.
- **All-in-one alt:** **Bosch BME688** — adds pressure + gas/AQI, but looser T/RH (±0.5 °C / ±3 %RH); pick it only to fold VOC/AQI onto one chip (§8, root README).
- **Price:** ~$4.5 bare / ~$6 breakout.

## 14. ams-OSRAM TSL2591 — High-Dynamic-Range Ambient Light Sensor *(weak-light sensitive)*

- **Product:** I²C ALS with **full-spectrum + IR photodiodes**, **600,000,000:1 dynamic range → 188 µlux to 88,000 lux** (gain 1×–9876×, integration 100–600 ms). Far more sensitive in near-dark than the VEML7700 (~0.0036 lux floor) → resolves a **very dim bedroom**.
- **Refs:** `TSL25911FN` · **ams-OSRAM** · **WFDFN-6 (2×2 mm)** · **DigiKey # 4162547** · datasheet `TSL2591 (Apr-2013)`.
- **Power / IO:** 2.7–3.6 V, ~90 µA active / ~3 µA sleep; I²C + INT (open-drain, programmable thresholds).
- **Hand-assembly:** WFDFN leadless → **breakout** (Adafruit 1980, DK # 4990786, Qwiic) at the light window.
- **Use:** ALS auto-dim (front-light gate, halo brightness) + the "true 0 emission at night" logic — the sub-millilux floor detects a fully dark room. Lux from CH0(full)/CH1(IR).
- **Alt:** **Vishay VEML7700** — outputs lux directly, simpler/cheaper, floor ~0.0036 lux (adequate but less sensitive).
- **Price:** ~$3 bare / ~$3–5 breakout.

## 15. Bosch BMA400 — Ultra-Low-Power Accel + Orientation *(tap-to-snooze + flat/standing, R14)*

- **Product:** 3-axis ±2/4/8/16 g accelerometer, **< 14.5 µA at full performance** (auto-wake/low-power modes), on-chip **tap/double-tap, orientation-change, activity & step** engines with interrupts. I²C/SPI.
- **Refs:** `BMA400` · **Bosch Sensortec** · **LGA-12 (2×2 mm)** · **DigiKey # 8634935** · datasheet `BST-BMA400-DS000` (AES-encrypted PDF — opens, no text-extract).
- **Power / IO:** 1.71–3.6 V; two INT pins. **Gravity-vector read → static flat-vs-standing (R14)**; **hardware tap IRQ** wakes the MCU for **tap-to-snooze (R3)** so it can sleep between events.
- **Hand-assembly:** LGA leadless → **breakout** (SparkFun Qwiic BMA400 / Adafruit) mounted rigidly to the body so gravity reads true.
- **Interface:** I²C (shared) + INT.
- **Price:** ~$1.6 bare / ~$5 breakout.

## 16. Timekeeping — no RTC IC *(ESP32-S3 RTC + 32.768 kHz crystal)*

**Decision: no dedicated RTC chip, no coin cell.** A battery-less RTC IC would lose time on total power loss exactly as the S3 does, so it adds parts without buying anything. The S3's *own* RTC is fine **as long as it runs off a good reference** — its internal RC oscillator drifts %-level over temperature (amp/LED heat nearby), so:

- **Add a 32.768 kHz crystal** on the S3's **XTAL32K** pins (GPIO15/16) → the RTC slow clock runs at **~±20 ppm ≈ 1.7 s/day**, disciplined by **SNTP** whenever online (even a daily sync keeps error to a couple seconds). Part = a jellybean passive (**Epson FC-135 / Abracon ABS07**, 3.2×1.5 mm 2-pad SMD or a TH cylinder), ~$0.3, hand-solderable — no datasheet filed (like the NTC/TVS/caps).
- **The S3 is essentially always powered** (USB or the 18650 on the always-on BAT node), so the RTC domain rarely dies. If **both** USB and the 18650 are gone, time is lost → **re-sync via SNTP on next boot**, running a clock animation meanwhile (accepted, keeps HW simple).
- *If you ever wanted true powered-off holdover* it would take an RTC IC **plus** a coin cell/supercap (e.g. RV-3028-C7 45 nA, or DS3231SN SOIC-16 TCXO) — explicitly **out of scope** here (no second battery).

See root README §1/§6c/§8/§10.

---

## Reconciliation with root README (v0.12)

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
| **Sensors (precise)** | ✅ temp **TMP117** (±0.1 °C, opt) · T/RH **SHT45** (±0.1 °C / ±1 %RH, replaces SHT40) · light **TSL2591** (weak-light, replaces VEML7700) · accel **BMA400** (tap + orient). All leadless → **I²C breakouts**. |
| **Timekeeping** | ✅ **No RTC IC / no battery** — S3 internal RTC off a **32.768 kHz crystal** (XTAL32K) + SNTP (~±20 ppm ≈ 1.7 s/day). Total power loss → re-sync on boot (clock animation). RV-3028-C7/DS3231SN datasheet removed. |

**No open discrepancies.** Intentional non-"matches": **PC68-4** (retained as a speaker *alternative*), and **CH224K sourced off-DigiKey** (no hand-solderable DigiKey PD sink exists).
