# CLAUDE.md

Wooden smart clock: circular MCU-driven analog dial + reflective mono info panel. Single ESP32-S3. USB-C powered, Li-ion backup. Alarm w/ quality audio + sunrise LED.

## Docs map
- `README.md` — full hardware spec (source of truth). Verbose; skim the tables/§ headers.
- `datasheet/README.md` — chosen parts: price, dims, power, **IO & power-domain table**.
- `datasheet/*.pdf` — full datasheets.
- `power.md` — power tree: USB-C/PD, rails, LED, battery, safety.
- `esp32.md` — ESP32-S3 pin-level IO map + MCP23017 expander port map (schematic-ready).
- `led.md` — LED subsystem: wake COB + panel LEDs, 3 PWM channels, AO3400A drivers.
- `power_values.md` — schematic-ready FB/comp/passive values per converter + support networks.

## Locked BOM (core)
| Block | Part | Rail | Notes |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | 3.3 V | Wi-Fi+BLE, 16/8 MB, ~33 usable GPIO (octal PSRAM claims 3) |
| Display | Sharp LS032B7DD02 | 5 V panel / 3 V logic | 3-wire SPI, reflective MIP; software VCOM (EXTMODE=L) |
| Movement | Juken X40.879 (dual-shaft) | 5 V (via driver) | + X27 base spec; optical homing (QRE1113GR, no magnets); solders to PCB, shafts through-board (custom footprint) |
| IO expander | Microchip MCP23017 | 3.3 V | SOIC/SSOP-28; on shared I²C + INT; offloads slow lines, net −7 GPIO |
| Motor driver | 2× TB6612FNG | VM 5 V / VCC 3.3 V | SSOP-24; PWM-on-IN microstep, 8 GPIO |
| Amp | TI TAS5760M | PVDD 12 V / DVDD 3.3 V | HTSSOP-32; I²S+I²C, PBTL mono (firmware DSP) |
| Speaker | Dayton DMA58-4 (2″, 4 Ω) | — | off amp; ~100–250 cc sealed |

## Rails
- **3.3 V** logic (from 5 V) · **5 V** panel + stepper + **panel LEDs** + amp-PVDD mux aux · **12 V** boost = audio PVDD + **wake LEDs** (gated, **plugged-only**) · **15 V VBUS** = **LT3652 charger VIN** (plugged). USB-PD in (15 V, **CH224K**) → **LT3652** 1-cell buck charger (BAT-node power-path); amp PVDD auto-muxes 12 V↔5 V (**LTC4412**) → quieter alarm on battery. All converters leaded. See `power.md`.

## Safety (NON-NEGOTIABLE)
Wood enclosure, bedroom, user-replaceable **18650 in a holder**. Board must be **safe for ANY 18650** ("if it fits, it must be safe") — assume unprotected/reversed/wrong-SoC/hot cell; do **not** rely on the cell's PCM.
- **Double-redundant** OV/OD/OC/SC = charger **LT3652** (CV 4.05 V via FB divider, NTC temp-qual, safety timer) **+ independent protector HY2111-GB (SOT-23-6, LCSC — AP9101C family went NRND 2026-07-17) + AOSD32334C dual-FET (SO-8)** (OV 4.28 V). Industry-standard for 1S — simple, not over-built.
- **Reverse-polarity P-FET** on BAT+ (bare cell can't be keyed).
- **NTC temp-qualified charge** (LT3652 NTC pin: no charge <0/>45 °C — single hot/cold window, not multi-zone JEITA); cell voltage-qualified on insert (ADC).
- **Charge-cap ~80 % (4.05 V)** — fixed in HW by the LT3652 float divider (no I²C; SoC/faults via ADC + CHRG/FAULT); **runs with no cell** on USB (BAT node feeds rails).
- **One-shot TCO (~77 °C)** in series with the cell — non-resettable thermal fuse against a hot/internally-shorted cell (added 2026-07-12; belt-and-suspenders to the NTC).
- **Physical:** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- **Li-ion ONLY** (labeled). Never trim safety parts. Full wiring/config in `power.md`.
- *Dropped as over-engineering for 1S:* secondary OVP, PPTC.

## Conventions
- Units mm; prices single-unit USD; source **DigiKey** (verify active + link).
- Dates absolute (e.g. 2026-07-04).
- Verify part status/specs against the datasheet before recommending.
- Flag README ↔ datasheet discrepancies.
- **Hand-solderable parts ONLY** (hard constraint): leaded pkgs (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23) or castellated modules — **no QFN/DFN/WSON/BGA/WLP/LGA** bare on the board. PowerPAD (amp/charger) OK with a thermal-via array. Leadless-only functions → breakout modules or dropped (e.g. fuel gauge → ADC). ≥0603 passives; hand-solderable connectors (USB-C/FPC/JST/SD).

## Stack
- ESP-IDF v5.x (C/FreeRTOS). Libs: LVGL 1-bit, AccelStepper/SwitecX25, NimBLE, esp_netif_sntp; **firmware biquad HPF+limiter for the amp** (TAS5760M has no on-chip DSP). See `README.md` §6c.

---

### BE CONCISE AND DIRECT
1. Skip all conversational filler, preambles, and pleasantries (e.g., "Certainly," "I hope this helps," "Great question").
2. Start every response with the direct answer or the first line of requested content.
3. Use bullet points, numbered lists, or sentence fragments instead of full paragraphs where possible.
4. If asked for code, provide only the specific snippet requested without explanatory text unless necessary for function.
5. Do not apologize, explain your reasoning, or provide "background info" unless explicitly asked.
6. Adopt a "Caveman" or "Executive Summary" style: provide the maximum information with the minimum number of tokens.
7. If a task is complex, provide the result immediately; do not talk about your process.
