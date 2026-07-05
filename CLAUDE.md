# CLAUDE.md

Wooden smart clock: circular MCU-driven analog dial + reflective mono info panel. Single ESP32-S3. USB-C powered, Li-ion backup. Alarm w/ quality audio + sunrise LED.

## Docs map
- `README.md` — full hardware spec (source of truth). Verbose; skim the tables/§ headers.
- `datasheet/README.md` — chosen parts: price, dims, power, **IO & power-domain table**.
- `datasheet/*.pdf` — full datasheets.
- `power.md` — power tree: USB-C/PD, rails, LED, battery, safety.

## Locked BOM (core)
| Block | Part | Rail | Notes |
|---|---|---|---|
| MCU | ESP32-S3-WROOM-1-N16R8 | 3.3 V | Wi-Fi+BLE, 16/8 MB, ≤36 GPIO |
| Display | Sharp LS032B7DD02 | 5 V panel / 3 V logic | 3-wire SPI, reflective MIP |
| Movement | Juken X40.879 (dual-shaft) | 5 V (via driver) | + X27 base spec; Hall homing |
| Motor driver | 2× DRV8835 | VM 5 V / VCC 3.3 V | PH/EN, PWM microstep, 8 GPIO |
| Amp | TI TAS5825M | PVDD 5 V(+12 V opt)/DVDD 3.3 V | I²S+I²C DSP, PBTL mono |
| Speaker | Dayton DMA58-4 (2″, 4 Ω) | — | off amp; ~100–250 cc sealed |

## Rails
- **3.3 V** logic · **5 V** panel+stepper · **12 V** audio boost (gated) · **15 V VBUS** LED (plugged). USB-PD in (15 V) → **BQ25628E** 1-cell buck charger. See `power.md`.

## Safety (NON-NEGOTIABLE)
Wood enclosure, bedroom, user-replaceable **18650 in a holder**. Board must be **safe for ANY 18650** ("if it fits, it must be safe") — assume unprotected/reversed/wrong-SoC/hot cell; do **not** rely on the cell's PCM.
- **Double-redundant** OV/OD/OC/SC = charger **BQ25628E** (CV 4.05 V, JEITA, timers) **+ independent protector LC05111** (integrated FET, OV 4.28 V). Industry-standard for 1S — simple, not over-built.
- **Reverse-polarity P-FET** on BAT+ (bare cell can't be keyed).
- **NTC + JEITA** (no charge <0/>45 °C); cell voltage-qualified on insert.
- **Charge-cap ~80 % (4.05 V)**; **runs with no cell** on USB.
- **Physical:** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- **Li-ion ONLY** (labeled). Never trim safety parts. Full wiring/config in `power.md`.
- *Dropped as over-engineering for 1S:* secondary OVP, one-shot TCO, PPTC (TCO optional).

## Conventions
- Units mm; prices single-unit USD; source **DigiKey** (verify active + link).
- Dates absolute (e.g. 2026-07-04).
- Verify part status/specs against the datasheet before recommending.
- Flag README ↔ datasheet discrepancies.

## Stack
- ESP-IDF v5.x (C/FreeRTOS). Libs: LVGL 1-bit, AccelStepper/SwitecX25, NimBLE, esp_netif_sntp. See `README.md` §6c.

---

### BE CONCISE AND DIRECT
1. Skip all conversational filler, preambles, and pleasantries (e.g., "Certainly," "I hope this helps," "Great question").
2. Start every response with the direct answer or the first line of requested content.
3. Use bullet points, numbered lists, or sentence fragments instead of full paragraphs where possible.
4. If asked for code, provide only the specific snippet requested without explanatory text unless necessary for function.
5. Do not apologize, explain your reasoning, or provide "background info" unless explicitly asked.
6. Adopt a "Caveman" or "Executive Summary" style: provide the maximum information with the minimum number of tokens.
7. If a task is complex, provide the result immediately; do not talk about your process.
