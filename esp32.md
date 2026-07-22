# ESP32-S3 IO Map

Pin-level allocation for **ESP32-S3-WROOM-1-N16R8** (host). Working draft.
All IO are **3.3 V LVCMOS** (module VDD = 3.3 V). Peripheral pins are routed through the
S3 **GPIO matrix**, so most assignments below are movable — the *fixed* ones are ADC1
(GPIO1–10), XTAL32K (15/16), native USB (19/20), and the PSRAM-reserved pads (35/36/37).

## Design decisions (flip any of these and the map shifts)

1. **Flash + debug + console = native USB-Serial-JTAG** on IO19/IO20 → USB-C D±.
   One USB-C cable = firmware download, **JTAG (OpenOCD/GDB) debug**, and a bidirectional
   USB-CDC console — no USB-UART bridge, no external probe (see **Debug & programming**).
   Logging is **USB-CDC only** (`RXD0`/IO44 is the expander IRQ → no UART RX; USB-CDC carries console
   in+out). The former aux UART-log pad **`TXD0` (IO43) is repurposed to `I2S_MCLK`** — the TAS5760M
   **requires** an MCLK (128–512 × f_S) that BCLK/LRCLK (≤64 f_S) can't supply (see Debug & programming).
2. **SPI2 = microSD only** (v0.19: the Sharp display + its shared-bus arrangement are gone).
   SD runs MSB-first, CS active-low, ~25 MHz; **IO17 (ex `LCD_CS`) now carries `ENC_SW`**, the
   knob push-switch interrupt.
3. **Knob on the host, not the expander:** A/B quadrature → **PCNT** (IO47/48, through 100k/200k
   dividers — the EM14 optical encoder is a 5 V part), press → **IO17 GPIO IRQ** (10 k PU + 100 nF
   + ~5 ms firmware debounce). The 7-pixel **SK6812 chain rides one RMT pin (IO7)**.
4. **Slow/static lines live on the MCP23017** (I²C), not the host — see the expander map.

**Result: 30 signals (incl. USB D±, 2 wake PWM, NeoPixel data, I²S MCLK) + 2 XTAL = 32 pads;
IO0 (boot strap) is the only uncommitted pad.** The N16R8 module exposes 36 GPIO pads; 3 are eaten
by octal PSRAM → **33 usable**, so this design sits at **32/33** — fully allocated. A genuine spare
GPIO now needs a lever: push a static line onto the expander, or free ENC_SW to the expander
(costs press latency).

## Legend
Dir/config: **IN**/**OUT** · **OD** open-drain · **PU/PD** pull-up/down (ext = external R) ·
**AF** alt-function (peripheral via GPIO matrix) · **ANA** analog · **IRQ** edge interrupt.

## Main IO table (all module-exposed pads)

**Pin** = the ESP32-S3-WROOM-1 pad name from the module datasheet (the silkscreen net). Every GPIO
pad is `IOxx` **except IO43/IO44**, which the module names **`TXD0`/`RXD0`** for their default UART0
role — plus the `EN`/`3V3`/`GND` pads.

| Pin (WROOM-1) | GPIO | Signal (label) | Peripheral | Dir / config | Level | Connects to (module · feature) | Notes |
|---|---|---|---|---|---|---|---|
| `3V3` | — | `3V3` | — | PWR IN | 3.3 V | module supply | ≥0.5 A; bulk + 0.1 µF decap |
| `GND` | — | `GND` | — | — | 0 V | ground (+ EPAD belly pad) | tie EPAD to GND plane |
| `EN` | — | `EN` (reset) | — | IN, PU (ext 10k) | 3.3 V | reset · prog header | +1 µF to GND; DTR-toggled for UART download |
| `IO0` | 0 | `BOOT` | — (strap) | IN, PU (ext 10k) | 3.3 V | boot/download strap · **only spare pad** | must be HIGH at boot; on prog header |
| `IO1` | 1 | `VBAT_SENSE` | ADC1_CH0 | ANA IN | 3.3 V | **cell/holder node (pre-Q2)** · SoC/voltage | ADC1 (Wi-Fi-safe); ÷2 divider + RC + **BAT42W clamp to GND** (reversed-cell negative bound); holder-side tap is what makes `CELL_TEST` work |
| `IO2` | 2 | `HOME_OPTO` | ADC1_CH1 | ANA IN | 3.3 V | QRE1113GR · hand-home reflect | ADC1; phototransistor load (or comparator) |
| `IO3` | 3 | `STEP_M_BIN2` | MCPWM0 | OUT / AF | 3.3 V | TB6612 #1 · minute coil B− | strap (JTAG) floating-OK; motor idle at boot (STBY low) |
| `IO4` | 4 | `STEP_M_AIN1` | MCPWM0_0A | OUT / AF | 3.3 V | TB6612 #1 · minute coil A+ | |
| `IO5` | 5 | `STEP_M_AIN2` | MCPWM0_0B | OUT / AF | 3.3 V | TB6612 #1 · minute coil A− | |
| `IO6` | 6 | `STEP_M_BIN1` | MCPWM0_1A | OUT / AF | 3.3 V | TB6612 #1 · minute coil B+ | |
| `IO7` | 7 | `NEOPIX_DATA` | RMT | OUT / AF | 3.3 V | SN74AHCT1G125 → 7× SK6812 RGBW · status+dial pixels | one data line for the whole chain (5 status + 2 dial); buffer lifts 3V3→5 V (V_IH 3.5 V @ 5 V) |
| `IO8` | 8 | `I2C_SDA` | I2C0 | OD, PU (ext 4.7k) | 3.3 V | shared bus · data | sensors + amp + MCP23017 |
| `IO9` | 9 | `I2C_SCL` | I2C0 | OD, PU (ext 4.7k) | 3.3 V | shared bus · clock | |
| `IO10` | 10 | `I2S_BCLK` | I2S0 | OUT / AF | 3.3 V | TAS5760M · bit clock | |
| `IO11` | 11 | `I2S_LRCLK` | I2S0 | OUT / AF | 3.3 V | TAS5760M · word select | |
| `IO12` | 12 | `I2S_DOUT` | I2S0 | OUT / AF | 3.3 V | TAS5760M · SDIN (audio) | **TAS5760M also needs MCLK (128–512 f_S)** → on **IO43** (see the I²S MCLK note below the table) |
| `IO13` | 13 | `SD_SCLK` | SPI2 | OUT / AF | 3.3 V | microSD · clock | ~25 MHz (SPI2 is SD-only since v0.19) |
| `IO14` | 14 | `SD_MOSI` | SPI2 | OUT / AF | 3.3 V | microSD · DI | MSB-first |
| `IO15` | 15 | `XTAL32K_P` | RTC | ANA / AF | — | ABS07 32.768 kHz · RTC ref | **dedicated** — no other use |
| `IO16` | 16 | `XTAL32K_N` | RTC | ANA / AF | — | ABS07 32.768 kHz · RTC ref | **dedicated** |
| `IO17` | 17 | `ENC_SW` | GPIO IRQ | IN, PU (ext 10k) | 3.3 V | knob push switch (EM14, via J10 ZH 1×06) | falling-edge; 100 nF + ~5 ms FW debounce; was `LCD_CS` |
| `IO18` | 18 | `SD_CS` | SPI2 CS | OUT, PU | 3.3 V | microSD · chip select | active-low; ext PU keeps card idle at boot |
| `IO19` | 19 | `USB_D−` | USB-Serial-JTAG | AF (USB) | 3.3 V | USB-C · D− | flash + CDC console + **JTAG debug** |
| `IO20` | 20 | `USB_D+` | USB-Serial-JTAG | AF (USB) | 3.3 V | USB-C · D+ | |
| `IO21` | 21 | `SD_MISO` | SPI2 | IN / AF | 3.3 V | microSD · DO | |
| `IO35` | 35 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available on this module SKU |
| `IO36` | 36 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available |
| `IO37` | 37 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available |
| `IO38` | 38 | `STEP_H_AIN1` | MCPWM1_0A | OUT / AF | 3.3 V | TB6612 #2 · hour coil A+ | |
| `IO39` | 39 | `STEP_H_AIN2` | MCPWM1_0B | OUT / AF | 3.3 V | TB6612 #2 · hour coil A− | also **JTAG MTCK** (ext probe — see below) |
| `IO40` | 40 | `STEP_H_BIN1` | MCPWM1_1A | OUT / AF | 3.3 V | TB6612 #2 · hour coil B+ | also **JTAG MTDO** |
| `IO41` | 41 | `STEP_H_BIN2` | MCPWM1_1B | OUT / AF | 3.3 V | TB6612 #2 · hour coil B− | also **JTAG MTDI** |
| `IO42` | 42 | `SENSOR_INT` | GPIO IRQ | IN, PU | 3.3 V | LIS3DH tap / TSL2591 · INT | falling-edge; also **JTAG MTMS** |
| `TXD0` | 43 | `I2S_MCLK` | I2S0 MCLK | OUT / AF | 3.3 V | TAS5760M · master clock | **required** by the amp (128–512 f_S; BCLK/LRCLK can't supply it). Repurposed from the aux UART log → **logging is USB-CDC only** (incl. boot log) |
| `RXD0` | 44 | `EXPANDER_INT` | GPIO IRQ | IN, PU | 3.3 V | MCP23017 · INTA/B (mirrored) | any-IO change → read INTF/INTCAP; UART RX not free |
| `IO45` | 45 | `WAKE_WARM_PWM` | LEDC | OUT / AF (PWM) | 3.3 V | wake-warm AO3400A gate · dim | 3000K COB, **12 V (plugged-only)**; strap VDD_SPI: LOW at boot ✓ |
| `IO46` | 46 | `WAKE_COOL_PWM` | LEDC | OUT / AF (PWM) | 3.3 V | wake-cool AO3400A gate · dim | 4000K COB, **12 V (plugged-only)**, low-side; strap: LOW at boot ✓ |
| `IO47` | 47 | `ENC_A` | PCNT | IN | 3.3 V | EM14 optical encoder · phase A | hardware quadrature — **plain GPIO, not IRQ**; 5 V output → **100k/200k divider (~3.2 V)** |
| `IO48` | 48 | `ENC_B` | PCNT | IN | 3.3 V | EM14 optical encoder · phase B | same divider |

**Not exposed on WROOM-1:** GPIO26–34 (internal SPI0/1 to in-package flash + PSRAM).

> **I²S MCLK (resolved 2026-07-12).** The **TAS5760M requires an MCLK** (128–512 × f_S; it is *not*
> MCLK-less like the MAX98357A), and BCLK/LRCLK can't supply it (BCLK ≤ 64 f_S, MCLK ≥ 128 f_S — no
> overlap, no on-chip PLL). So **`IO43` carries `I2S_MCLK`** (reassigned from the aux UART log): I²S0
> drives BCLK/LRCLK/DOUT **+ MCLK** to the amp. Logging is **USB-CDC only** (the UART log was secondary);
> `IO0`/boot stays the sole uncommitted pad.
>
> **HW-routed, not bit-banged.** All four I²S signals (MCLK/BCLK/WS/DOUT) are driven by the **hardware
> I²S0 peripheral through the S3 GPIO matrix**, so any output-capable GPIO works — including IO43. This
> is unlike the *original* ESP32, where MCLK was limited to GPIO0/1/3 (CLK_OUT); the S3 lifted that.
> Target **MCLK = 256 × f_S ≈ 12.288 MHz @ 48 kHz** (BCLK ≈ 3 MHz) — both far below the GPIO-matrix
> ceiling (Espressif: standard audio rates are "well within" it). The S3 has **no input-only pins**, so
> IO10/11/12/43 are all valid HW I²S outputs.

## MCP23017 expander port map (rides shared I²C; INT → GPIO44)
Weak pull-ups + interrupt-on-change on the inputs; `IOCON.MIRROR=1` ORs INTA+INTB to one line.

| Port | Signal | Dir | IOC | Connects to |
|---|---|---|---|---|
| GPA0 | `SPK_SD` | OUT | — | TAS5760M mute/shutdown |
| GPA1 | `STEP_STBY` | OUT | — | both TB6612 STBY (idle-low at boot → motors off) |
| GPA2 | `BOOST12_EN` | OUT | — | TPS55340 12 V gate (**plugged-only**: enable gated by `PD_PG`; feeds amp PVDD + wake LEDs) |
| GPA3 | `RADIO_OFF` | IN, PU | ✔ | rear radio-disable toggle (J11) — firmware hard-kills Wi-Fi/BLE |
| GPA4–6 | *free* | — | — | 3 spare (BTN1–3 dropped v0.19; ENC_SW moved to host IO17) |
| GPB0 | `PD_PG` | IN, PU | ✔ | CH224K power-good (OD) |
| GPB1 | `CHRG` | IN, PU | ✔ | LT3652 charge status (OD) |
| GPB2 | `FAULT` | IN, PU | ✔ | LT3652 fault status (OD) |
| GPB3 | *free* | — | — | spare (`LCD_DISP` gone with the display, v0.19) |
| GPB4 | `FULLCHG_EN` | OUT | — | LT3652 4.2 V full-charge FET gate |
| GPB5 | `VBAT_DIV_EN` | OUT | — | Vbat-divider disconnect FET gate |
| GPB6 | `SPK_FAULT` | IN, PU | ✔ | TAS5760M fault (OD, 10 k PU) |
| GPB7 | `CELL_TEST` | OUT | — | full-cell vs no-cell discriminator (2026-07-21): high → Q8 (2N7002) pulls Q9's (AO3401A) gate low → Q9 lifts reverse-FET Q2's gate to holder+ → Q2 **off**. ADC then reads V_cell (cell present, no step) vs a ~0.3–0.4 V drop (empty holder = Q2 body-diode back-feed). **Plugged-only, brief**; defaults safe (R26/R27 hold both FETs off at POR) |

Hard-safety (OV/OC/SC) is autonomous in the LT3652 + HY2111 — nothing time-critical rides the expander.

## TB6612FNG stepper-driver pinout (×2, SSOP-24) + X40.879 coil map
PWM-on-IN microstepping: **PWMA (23) + PWMB (15) tied HIGH to Vcc** (direct 0 Ω / 10 k pull-up); the four
AIN/BIN inputs carry the MCPWM microstep waveforms. **STBY (19)** ← expander `STEP_STBY` (idle-low → both
drivers off at boot). VM (13/14/24) = **5 V**, Vcc (20) = **3.3 V**, GND (18) + PGND (3/4/9/10) = GND.

| TB6612 pin | Signal | Driver #1 (minute) → net | Driver #2 (hour) → net |
|---|---|---|---|
| 21 AIN1 | coil A + | `IO4` STEP_M_AIN1 | `IO38` STEP_H_AIN1 |
| 22 AIN2 | coil A − | `IO5` STEP_M_AIN2 | `IO39` STEP_H_AIN2 |
| 17 BIN1 | coil B + | `IO6` STEP_M_BIN1 | `IO40` STEP_H_BIN1 |
| 16 BIN2 | coil B − | `IO3` STEP_M_BIN2 | `IO41` STEP_H_BIN2 |
| 23 PWMA · 15 PWMB | ch enable | → **Vcc (HIGH)** | → **Vcc (HIGH)** |
| 19 STBY | standby | expander `STEP_STBY` (shared) | expander `STEP_STBY` (shared) |
| 1,2 AO1 · 5,6 AO2 | coil A out ± | ext-shaft coil 1 (below) | int-shaft coil 1 |
| 11,12 BO1 · 7,8 BO2 | coil B out ± | ext-shaft coil 2 | int-shaft coil 2 |
| 13,14,24 VM | motor supply | **5 V** | **5 V** |
| 20 Vcc | logic supply | **3.3 V** | **3.3 V** |
| 18 GND · 3,4,9,10 PGND | ground | GND | GND |

**X40.879 dual-shaft coil map** (contacts per the pinout addendum; `e` = external shaft, `i` = internal):
one TB6612 per shaft, chA = motor coil 1, chB = motor coil 2. Which shaft is minute vs hour, and each
coil's polarity/phase order, are **firmware-trimmable** (reverse the step sequence) — just wire consistently:

| Bridge → output → coil | External shaft (driver #1) | Internal shaft (driver #2) |
|---|---|---|
| chA: AO1 (+) / AO2 (−) → coil 1 | 1e / 2e | 1i / 2i |
| chB: BO1 (+) / BO2 (−) → coil 2 | 4e / 3e | 4i / 3i |

## I²C address map (shared bus, 3.3 V, 4.7 k pull-ups)
No collisions. Strappable addresses get a **0 Ω** footprint for a build-time choice; fixed-address and
breakout parts need no main-board strap.

| Device | Addr (7-bit) | Set by | Main-board strap |
|---|---|---|---|
| **MCP23017** expander | **0x20** | A2/A1/A0 | **3× 0 Ω → GND** (000 → 0x20; 0x20–0x27 free) |
| **TAS5760M** amp | **0x6C** | SPK_SLEEP/ADR (13) | **1× 0 Ω → GND** (HIGH → 0x6D) |
| **TSL2591** light | **0x29** | fixed | — |
| **LIS3DH** accel | **0x18** | SDO/SA0 | — (breakout jumper; Adafruit 2809 default) |
| **BME688** env | **0x77** | SDO | — (breakout jumper; Adafruit 5046 default) |

Also strapped (not an address): **TAS5760M SPK_GAIN0/1 → DVDD** selects software/I²C control mode. Sensor
breakouts carry ~10 k pull-ups in ∥ with the 4.7 k — lift their jumpers if the bus gets too strong.

## Peripheral budget (vs. S3 capacity)

| Peripheral | Capacity | Used | Assigned |
|---|---|---|---|
| **MCPWM** | 2 units × 6 = 12 out | 8 | steppers (unit0 = minute, unit1 = hour) |
| **LEDC** | 8 ch | 2 | wake-warm + wake-cool PWM dimming (panel ch recovered v0.19) |
| **PCNT** | 4 units | 1 | knob A/B (hardware quadrature, glitch-filtered) |
| **RMT** | 4 TX | 1 | **IO7 → 7× SK6812 RGBW** (status + dial NeoPixels, `led_strip`) |
| **I²C** | 2 | 1 | shared bus (sensors + amp + expander) |
| **I²S** | 2 | 1 | I²S0 → amp: BCLK/LRCLK/DOUT **+ MCLK (IO43)** |
| **SPI (GP)** | 2 (SPI2/3) | 1 | SPI2: microSD (display gone → sole device) |
| **ADC1** | 10 ch (GPIO1–10) | 2 | VBAT, homing opto (**ADC2 unusable w/ Wi-Fi**) |
| **XTAL32K** | 1 | 1 | 32.768 kHz crystal |
| **GPIO IRQ** | any GPIO | 3 | SENSOR_INT, EXPANDER_INT, ENC_SW |
| **UART** | 3 | 0 | USB-CDC is the console; IO43 reassigned to I²S MCLK → UART0 free |
| **USB-Serial-JTAG** | 1 | 1 | flash + CDC console + JTAG debug (IO19/20) |

## Notes
- **Knob A/B → PCNT, plain GPIO (not interrupt pins).** Hardware quadrature counts with zero
  CPU / no ISR and won't drop steps on fast spins (64 CPR ×4 = 256 counts/rev); add the PCNT glitch
  filter. The EM14 is optical → no contact bounce on A/B at all. Interrupt-capable pins would only
  matter for a software decoder, which we don't use.
- **Every S3 GPIO is interrupt-capable** — no dedicated "interrupt pins," so INT count is never the
  limiter. 3 IRQ lines are used (sensor + expander + knob press), and the expander funnels its
  slow inputs (radio toggle, PG, CHRG, FAULT, …) into its single line.
- **Strapping pins** (0, 3, 45, 46) carry only signals whose boot state matches the required strap
  level (0 = free/high, 3 = motor-idle, 45/46 = idle-low). Do not repurpose without re-checking.
- **Wake channels (2×, per `led.md`)** PWM-dimmed via **AO3400A low-side MOSFETs** (100 Ω gate +
  10 k pulldown, ~1 kHz): wake-**warm** (IO45, 3000K) + wake-**cool** (IO46, 4000K) are **12 V COB
  strips** (tunable-white pair, **plugged-only** — firmware gates their PWM off on battery, since the
  12 V boost is plugged-only and bright LED + audio would exceed its ~12 W ceiling). The logic-level
  FET gates take 3.3 V PWM directly.
- **NeoPixels (IO7, RMT):** 7× SK6812 RGBW on the 5 V rail — 5 status + 2 dial pixels, `led_strip`
  driver. The chain's V_IH is 0.7·VDD = 3.5 V → **one SN74AHCT1G125** (VCC 5 V, TTL input) lifts the
  3.3 V data line; ~330 Ω into DIN(1), 100 nF per pixel + 100 µF bulk.
- **5 V → 3.3 V inputs:** the EM14 encoder outputs (5 V ASIC) come in through **100k/200k dividers**
  (IO47/48). Everything else is 3.3 V-native (TB6612 VCC, TAS5760M DVDD, microSD, I²C devices)
  → no other level shifting.

## Debug & programming
- **The S3 debug port is JTAG, not SWD** (Xtensa LX7 — SWD is ARM-only). Two ways in:
  - **USB-Serial-JTAG (recommended, default).** Built into the S3 on **IO19/IO20** — already the
    USB-C D−/D+. One USB-C cable gives **firmware download + OpenOCD/GDB JTAG debug + a
    bidirectional USB-CDC console**, with **zero extra pins**. This is the primary path.
  - **External JTAG probe** (ESP-Prog / J-Link) uses **MTCK=IO39 · MTDO=IO40 · MTDI=IO41 ·
    MTMS=IO42** — here those carry stepper #2 + `SENSOR_INT`, so an external probe only works on a
    bring-up board where they're lifted. Not needed once USB-JTAG is up.
- **Logging = USB-CDC only.** The console (in+out) is USB-CDC over USB-C. `TXD0 (IO43)` is **no longer a
  UART log** — it now carries **`I2S_MCLK`** for the amp, so route the boot-ROM/bootloader banner and app
  logs to **USB-CDC** (S3 config/eFuse selects USB-CDC as the boot console). `RXD0 (IO44)` is
  `EXPANDER_INT`. No hardware UART is exposed; USB-CDC is sufficient for headless bring-up once enumerated.
- **Programming/debug header (4-pin):** `3V3 · GND · EN · IO0` + a USB-C tap on `D±(IO19/20)`.
  Flashing/debug ride USB-JTAG (it drives reset itself). EN + IO0 are broken out for manual
  reset/boot and as strap test points.

## Reconciliation with `datasheet/README.md` §IO
⚠ That table still describes the **pre-v0.19** (display-era) design — `datasheet/` was deliberately
left untouched in the 2026-07-19 cube redesign (kept for future reuse). The current allocation is
this file: display SPI/CS/DISP gone (SPI2 = SD only), `PANEL_PWM` → `NEOPIX_DATA` (IO7, RMT),
`LCD_CS` → `ENC_SW` (IO17), knob A/B on IO47/48 through 5 V dividers, BTN1–3 dropped, `RADIO_OFF`
on expander GPA3 — still **32/33 pads** (IO0/boot is the only spare).
