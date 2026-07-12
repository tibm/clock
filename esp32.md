# ESP32-S3 IO Map

Pin-level allocation for **ESP32-S3-WROOM-1-N16R8** (host). Working draft.
All IO are **3.3 V LVCMOS** (module VDD = 3.3 V). Peripheral pins are routed through the
S3 **GPIO matrix**, so most assignments below are movable — the *fixed* ones are ADC1
(GPIO1–10), XTAL32K (15/16), native USB (19/20), and the PSRAM-reserved pads (35/36/37).

## Design decisions (flip any of these and the map shifts)

1. **Flash + debug + console = native USB-Serial-JTAG** on IO19/IO20 → USB-C D±.
   One USB-C cable = firmware download, **JTAG (OpenOCD/GDB) debug**, and a bidirectional
   USB-CDC console — no USB-UART bridge, no external probe (see **Debug & programming**).
   Logging is over USB-CDC, with **`TXD0` (IO43) kept as an auxiliary hardware UART log** (TX-only;
   `RXD0`/IO44 is the expander IRQ → no UART RX, but USB-CDC handles console input). Merging the
   dial + frontlight into one **panel** PWM (`led.md`: 3 LED channels, not 4) freed IO43 back to UART.
2. **Display + microSD share one SPI host (SPI2).** Forced by the pin budget once native USB
   is in. Works because the Sharp panel is **write-only** (no MISO) and each `spi_device` gets
   its own clock / bit-order / CS-polarity: LCD = ≤2 MHz, **LSB-first, CS active-HIGH**; SD =
   ~25 MHz, MSB-first, CS active-low. The host serializes transactions; **8 MB PSRAM buffers
   audio** so SD reads don't glitch during display writes.
   *Alt:* give SD its own SPI3 (no contention) — costs the 2 spare pins below.
3. **Software VCOM** on the Sharp panel: `EXTMODE` tied **LOW**, VCOM toggled by the frame-inversion
   bit in the SPI write (≥1 Hz). → no `EXTCOMIN` GPIO (README recovery lever (b), now applied).
4. **Slow/static lines live on the MCP23017** (I²C), not the host — see the expander map.

**Result: 28 signals (incl. 3 LED PWM + UART log) + 2 XTAL + 2 USB = 32 pads; IO0 (boot strap) is the
only uncommitted pad.** The N16R8 module exposes 36 GPIO pads; 3 are eaten by octal PSRAM →
**33 usable**, so this design sits at **32/33** — fully allocated. A genuine spare GPIO now needs a
lever: push a static line onto the expander, or share the display/SD... (SD already shared).

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
| `IO1` | 1 | `VBAT_SENSE` | ADC1_CH0 | ANA IN | 3.3 V | LT3652 BAT node · SoC/voltage | ADC1 (Wi-Fi-safe); ÷2 divider + RC |
| `IO2` | 2 | `HOME_OPTO` | ADC1_CH1 | ANA IN | 3.3 V | ITR8307 · hand-home reflect | ADC1; phototransistor load (or comparator) |
| `IO3` | 3 | `STEP_M_BIN2` | MCPWM0 | OUT / AF | 3.3 V | TB6612 #1 · minute coil B− | strap (JTAG) floating-OK; motor idle at boot (STBY low) |
| `IO4` | 4 | `STEP_M_AIN1` | MCPWM0_0A | OUT / AF | 3.3 V | TB6612 #1 · minute coil A+ | |
| `IO5` | 5 | `STEP_M_AIN2` | MCPWM0_0B | OUT / AF | 3.3 V | TB6612 #1 · minute coil A− | |
| `IO6` | 6 | `STEP_M_BIN1` | MCPWM0_1A | OUT / AF | 3.3 V | TB6612 #1 · minute coil B+ | |
| `IO7` | 7 | `PANEL_PWM` | LEDC | OUT / AF (PWM) | 3.3 V | panel AO3400A gate · dim | dial **+** frontlight (shared, always equal); warm-white LEDs (CLM3C-MKW), **5 V**, low-side; `LCD_DISP` on expander |
| `IO8` | 8 | `I2C_SDA` | I2C0 | OD, PU (ext 4.7k) | 3.3 V | shared bus · data | sensors + amp + MCP23017 |
| `IO9` | 9 | `I2C_SCL` | I2C0 | OD, PU (ext 4.7k) | 3.3 V | shared bus · clock | |
| `IO10` | 10 | `I2S_BCLK` | I2S0 | OUT / AF | 3.3 V | TAS5760M · bit clock | |
| `IO11` | 11 | `I2S_LRCLK` | I2S0 | OUT / AF | 3.3 V | TAS5760M · word select | |
| `IO12` | 12 | `I2S_DOUT` | I2S0 | OUT / AF | 3.3 V | TAS5760M · SDIN (audio) | no MCLK needed |
| `IO13` | 13 | `SPI_SCLK` | SPI2 | OUT / AF | 3.3 V | LS032 + microSD · shared clock | per-device clk (LCD ≤2 MHz / SD ~25 MHz) |
| `IO14` | 14 | `SPI_MOSI` | SPI2 | OUT / AF | 3.3 V | LS032 (SI) + microSD (DI) · shared | per-device bit order (LCD LSB / SD MSB) |
| `IO15` | 15 | `XTAL32K_P` | RTC | ANA / AF | — | ABS07 32.768 kHz · RTC ref | **dedicated** — no other use |
| `IO16` | 16 | `XTAL32K_N` | RTC | ANA / AF | — | ABS07 32.768 kHz · RTC ref | **dedicated** |
| `IO17` | 17 | `LCD_CS` | SPI2 CS | OUT (active-HIGH) | 3.3 V | LS032 · chip select | **Sharp CS is active-HIGH**; GPIO-driven CS |
| `IO18` | 18 | `SD_CS` | SPI2 CS | OUT, PU | 3.3 V | microSD · chip select | active-low; ext PU keeps card idle at boot |
| `IO19` | 19 | `USB_D−` | USB-Serial-JTAG | AF (USB) | 3.3 V | USB-C · D− | flash + CDC console + **JTAG debug** |
| `IO20` | 20 | `USB_D+` | USB-Serial-JTAG | AF (USB) | 3.3 V | USB-C · D+ | |
| `IO21` | 21 | `SPI_MISO` | SPI2 | IN / AF | 3.3 V | microSD · DO | display is write-only → MISO = SD only |
| `IO35` | 35 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available on this module SKU |
| `IO36` | 36 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available |
| `IO37` | 37 | *reserved* | — | — | — | **octal PSRAM (N16R8)** | not available |
| `IO38` | 38 | `STEP_H_AIN1` | MCPWM1_0A | OUT / AF | 3.3 V | TB6612 #2 · hour coil A+ | |
| `IO39` | 39 | `STEP_H_AIN2` | MCPWM1_0B | OUT / AF | 3.3 V | TB6612 #2 · hour coil A− | also **JTAG MTCK** (ext probe — see below) |
| `IO40` | 40 | `STEP_H_BIN1` | MCPWM1_1A | OUT / AF | 3.3 V | TB6612 #2 · hour coil B+ | also **JTAG MTDO** |
| `IO41` | 41 | `STEP_H_BIN2` | MCPWM1_1B | OUT / AF | 3.3 V | TB6612 #2 · hour coil B− | also **JTAG MTDI** |
| `IO42` | 42 | `SENSOR_INT` | GPIO IRQ | IN, PU | 3.3 V | LIS3DH tap / TSL2591 · INT | falling-edge; also **JTAG MTMS** |
| `TXD0` | 43 | `LOG_TX` | UART0 TX | OUT / AF | 3.3 V | prog header · hardware log (aux to USB-CDC) | TX-only; RX (IO44) taken → console input via USB-CDC; boot-ROM banner prints here |
| `RXD0` | 44 | `EXPANDER_INT` | GPIO IRQ | IN, PU | 3.3 V | MCP23017 · INTA/B (mirrored) | any-IO change → read INTF/INTCAP; UART RX not free |
| `IO45` | 45 | `WAKE_WARM_PWM` | LEDC | OUT / AF (PWM) | 3.3 V | wake-warm AO3400A gate · dim | 3000K COB, **12 V (plugged-only)**; strap VDD_SPI: LOW at boot ✓ |
| `IO46` | 46 | `WAKE_COOL_PWM` | LEDC | OUT / AF (PWM) | 3.3 V | wake-cool AO3400A gate · dim | 4000K COB, **12 V (plugged-only)**, low-side; strap: LOW at boot ✓ |
| `IO47` | 47 | `ENC_A` | PCNT | IN, PU | 3.3 V | rotary encoder · phase A | hardware quadrature — **plain GPIO, not IRQ** |
| `IO48` | 48 | `ENC_B` | PCNT | IN, PU | 3.3 V | rotary encoder · phase B | |

**Not exposed on WROOM-1:** GPIO26–34 (internal SPI0/1 to in-package flash + PSRAM).

## MCP23017 expander port map (rides shared I²C; INT → GPIO44)
Weak pull-ups + interrupt-on-change on the inputs; `IOCON.MIRROR=1` ORs INTA+INTB to one line.

| Port | Signal | Dir | IOC | Connects to |
|---|---|---|---|---|
| GPA0 | `SPK_SD` | OUT | — | TAS5760M mute/shutdown |
| GPA1 | `STEP_STBY` | OUT | — | both TB6612 STBY (idle-low at boot → motors off) |
| GPA2 | `BOOST12_EN` | OUT | — | TPS55340 12 V gate (**plugged-only**: enable gated by `PD_PG`; feeds amp PVDD + wake LEDs) |
| GPA3 | `BTN1` | IN, PU | ✔ | rear tactile |
| GPA4 | `BTN2` | IN, PU | ✔ | rear tactile |
| GPA5 | `BTN3` | IN, PU | ✔ | rear tactile |
| GPA6 | `ENC_SW` | IN, PU | ✔ | encoder push-switch |
| GPB0 | `PD_PG` | IN, PU | ✔ | CH224K power-good (OD) |
| GPB1 | `CHRG` | IN, PU | ✔ | LT3652 charge status (OD) |
| GPB2 | `FAULT` | IN, PU | ✔ | LT3652 fault status (OD) |
| GPB3 | `LCD_DISP` | OUT | — | LS032 display on/off (moved off IO7) |
| GPB4 | `FULLCHG_EN` | OUT | — | LT3652 4.2 V full-charge FET gate |
| GPB5 | `VBAT_DIV_EN` | OUT | — | Vbat-divider disconnect FET gate |
| GPB6–7 | *free* | — | — | 2 spare expander IO |

Hard-safety (OV/OC/SC) is autonomous in the LT3652 + S-8261 — nothing time-critical rides the expander.

## Peripheral budget (vs. S3 capacity)

| Peripheral | Capacity | Used | Assigned |
|---|---|---|---|
| **MCPWM** | 2 units × 6 = 12 out | 8 | steppers (unit0 = minute, unit1 = hour) |
| **LEDC** | 8 ch | 3 | wake-warm + wake-cool + panel (dial+frontlight) PWM dimming |
| **PCNT** | 4 units | 1 | encoder A/B (hardware quadrature, glitch-filtered) |
| **RMT** | 4 TX | 0 | (SK6812 halo dropped — single-colour COB strips) |
| **I²C** | 2 | 1 | shared bus (sensors + amp + expander) |
| **I²S** | 2 | 1 | I²S0 → amp |
| **SPI (GP)** | 2 (SPI2/3) | 1 | SPI2 shared: display + microSD |
| **ADC1** | 10 ch (GPIO1–10) | 2 | VBAT, homing opto (**ADC2 unusable w/ Wi-Fi**) |
| **XTAL32K** | 1 | 1 | 32.768 kHz crystal |
| **GPIO IRQ** | any GPIO | 2 | SENSOR_INT, EXPANDER_INT |
| **UART** | 3 | 1 | UART0 TXD0 (IO43) hardware log, TX-only (RX=IO44 taken); USB-CDC = main console |
| **USB-Serial-JTAG** | 1 | 1 | flash + CDC console + JTAG debug (IO19/20) |

## Notes
- **Encoder A/B → PCNT, plain GPIO (not interrupt pins).** Hardware quadrature counts with zero
  CPU / no ISR and won't drop steps on fast spins; add the PCNT glitch filter. Interrupt-capable
  pins would only matter for a software decoder, which we don't use.
- **Every S3 GPIO is interrupt-capable** — no dedicated "interrupt pins," so INT count is never the
  limiter. Only 2 IRQ lines are needed (sensor + expander), and the expander funnels all its 16
  slow inputs (buttons, PG, CHRG, FAULT, …) into that single line.
- **Strapping pins** (0, 3, 45, 46) carry only signals whose boot state matches the required strap
  level (0 = free/high, 3 = motor-idle, 45/46 = idle-low). Do not repurpose without re-checking.
- **LED channels (3×, per `led.md`)**, all PWM-dimmed via **AO3400A low-side MOSFETs** (100 Ω gate +
  10 k pulldown, ~1 kHz): wake-**warm** (IO45, 3000K) + wake-**cool** (IO46, 4000K) are **12 V COB
  strips** (tunable-white pair, **plugged-only** — firmware gates their PWM off on battery, since the
  12 V boost is plugged-only and bright LED + audio would exceed its ~12 W ceiling); **panel** (IO7 —
  dial + frontlight on one shared channel, always equal) is a string of **5 V discrete LEDs** (CLM3C-MKW,
  each with a series R), so the reading/panel light **works on battery**. The logic-level FET gates take
  3.3 V PWM directly, **no level shifter**, **no SK6812/TPS92200**.
- All logic is 3.3 V-native (Sharp panel accepts 3.0 V-min highs at 5 V panel rail; TB6612
  VCC, TAS5760M DVDD, microSD, I²C devices all 3.3 V) → **no other level shifting**.

## Debug & programming
- **The S3 debug port is JTAG, not SWD** (Xtensa LX7 — SWD is ARM-only). Two ways in:
  - **USB-Serial-JTAG (recommended, default).** Built into the S3 on **IO19/IO20** — already the
    USB-C D−/D+. One USB-C cable gives **firmware download + OpenOCD/GDB JTAG debug + a
    bidirectional USB-CDC console**, with **zero extra pins**. This is the primary path.
  - **External JTAG probe** (ESP-Prog / J-Link) uses **MTCK=IO39 · MTDO=IO40 · MTDI=IO41 ·
    MTMS=IO42** — here those carry stepper #2 + `SENSOR_INT`, so an external probe only works on a
    bring-up board where they're lifted. Not needed once USB-JTAG is up.
- **Logging = USB-CDC + auxiliary UART.** The main console is USB-CDC over USB-C. `TXD0 (IO43)` is
  also kept as a **hardware UART log (TX-only)** on the programming header — the boot-ROM banner and
  app logs come out here even when USB isn't enumerated (headless bring-up). `RXD0 (IO44)` is
  `EXPANDER_INT`, so there is **no UART RX** — console *input* is USB-CDC only, which is sufficient.
  (Freed by merging dial + frontlight into one panel PWM → 3 LED channels, not 4.)
- **Programming/debug header (4-pin):** `3V3 · GND · EN · IO0` + a USB-C tap on `D±(IO19/20)`.
  Flashing/debug ride USB-JTAG (it drives reset itself). EN + IO0 are broken out for manual
  reset/boot and as strap test points.

## Reconciliation with `datasheet/README.md` §IO
That table estimates **~30** host-direct GPIO. This pin-level map realizes **27** by applying the two
documented recovery levers — **(a)** shared display/SD SPI bus and **(b)** software VCOM — and adds
**native USB** (19/20) + **3 LED PWM** + **UART log** (IO43), landing at **32/33 pads** (IO0/boot is the last one).
PG/CHRG/FAULT already moved to the expander in the prior pass; `LCD_EXTCOMIN` is dropped here
(software VCOM).
