# LED System Design for Wooden Alarm Clock

## Overview

Two independent lighting subsystems (v0.19 — the 5 V discrete panel string was
dropped with the display):

| Subsystem | What it lights | Emitter | Rail | Drive |
|------|----------------|---------|------|-----|
| **Wake-up light** | 100–120 mm sunrise diffuser (rear aperture) | **12 V** COB, warm **3000K** + neutral **4000K** (tunable-white pair) | **12 V (plugged-only)** | **2× LEDC PWM** (warm IO45 / cool IO46) via AO3400A |
| **Status + dial NeoPixels** | 5 status holes in the aluminum face **+** dial wash behind the glass | **7× SK6812 RGBW** 5050 (Adafruit 2758), on the main PCB | **5 V (always, incl. battery)** | **1 data GPIO** (IO7 → RMT), daisy-chained |

PWM outputs: **2** (LEDC) + **1 RMT data line**.

> **Two rails by design.** The **wake light is bright** → needs the **12 V** rail, which is
> **plugged-only** (the TPS55340 boost runs only on the USB-PD contract; on battery it's off, so
> firmware gates the wake PWM off too). The **NeoPixels are faint** and ride the always-on **5 V**
> rail → status LEDs and dial illumination work on **battery**. See
> [`power.md`](power.md) §LED for the 12 V-source decision (no barrel jack).

## NeoPixel chain (7× SK6812 RGBW, one wire)

- **Pixels 1–5 = status LEDs** behind the face-plate holes + icons
  (`bell` · `alarm-clock` · `clock` · `volume-1` · `battery`, §12 of the README):
  red = alarm armed, white = disarmed, etc. **Pixel pitch on the PCB must equal the
  face-plate hole pitch** — fix both in the same drawing.
- **Pixels 6–7 = dial illumination**: warm white (use the dedicated W channel) washing the
  walnut dial behind the glass; ALS-gated night dim, **hard-off by default** → 0 emission.
- **One data line is plenty**: a full 7-pixel refresh is 7 × 32 bit @ 800 kHz ≈ **0.3 ms**.
  Brightness control and slow ramps are firmware (Espressif `led_strip`, RMT backend,
  gamma-corrected); no fancy animation needed or planned.
- **Level shift is required**: SK6812 V<sub>IH</sub> = 0.7 × VDD = **3.5 V at a 5 V supply** — a
  3.3 V GPIO is out of spec. One **SN74AHCT1G125** (SOT-23-5, TTL-input buffer powered at 5 V,
  [DigiKey 376028](https://www.digikey.com/en/products/detail/texas-instruments/SN74AHCT1G125DBVR/376028))
  re-drives the data line; **~330 Ω** series into the first DIN; **100 nF at every pixel** +
  **100 µF bulk** at the chain head.
- **Power**: worst-case all-white ≈ 7 × 80 mA ≈ **0.6 A on 5 V** — inside the TPS61023 budget;
  status/dial duty in practice is a few tens of mA.

## Suppliers (2 total)

1.  **DigiKey** — LEDs, MOSFETs, buffer, resistors, connectors (all parts below are DK-stocked).
2.  TAP Plastics (or McMaster-Carr) — diffusers / acrylic.

## Bill of Materials

| Item | Part (DigiKey) | DK # | ~$ | Notes |
|---|---|---|---|---|
| Wake COB — **warm 3000K** | Inspired LED **`12V-COB-3000K-12M`** | 16714316 | **$0.64** /0.98″ seg ($215/12 m reel) | 12 V, 3.6 W/ft, 8 mm, cut @ 0.98″, UL. Wake-warm (IO45). ~1 ft ≈ 12 seg ≈ $7.7. |
| Wake COB — **neutral 4000K** | Inspired LED **`12V-COB-4000K-12M`** | 16714317 | **$0.64** /0.98″ seg | same reel spec; Wake-cool (IO46). *(4000K = neutral; 24 V variant exists if a longer run wants less current.)* |
| NeoPixels ×7 (status 5 + dial 2) | **SK6812 RGBW 5050** — Adafruit **2758** (10-pack) | 6134706 | **$5.95**/10 | on the main PCB (KiCad `LED_SK6812_PLCC4_5.0x5.0mm_P3.2mm`); 3 spares |
| NeoPixel data buffer | **SN74AHCT1G125DBVR** | 376028 | **$0.14** | SOT-23-5, VCC = 5 V, TTL V_IH 2 V ← 3.3 V GPIO OK |
| Data series R | **330 Ω** 0603 | — | ~$0.01 | at the first DIN |
| Pixel decoupling | **100 nF** 0603 ×7 + **100 µF** bulk | — | ~$0.5 | one 100 nF at each pixel VDD |
| Logic MOSFET (×2) | **AO3400A** | — | ~$0.1 ea | one low-side FET per wake PWM channel; 100 Ω gate + 10 kΩ pulldown. *(3rd FET recovered v0.19.)* |
| **Amp PVDD rail-mux** | **LTC4412** (SOT-23-6) + P-FET | — | ~$2 | auto priority-OR: **12 V** boost when plugged, **5 V** rail on battery (quieter alarm). See [`power_values.md`](power_values.md) §8. |
| JST connector | JST-PH 1×03 (J9, wake strips) | — | — | NeoPixels are on-board → no connector. |
| Opal diffuser | 3 mm White Opal Acrylic (TAP Plastics) | — | — | wake mixing chamber. |
| Aluminum tape | Reflective tape | — | — | line the mixing chamber. |

> **No constant-current driver.** The wake strips are **self-ballasted 12 V COB** (integrated series
> resistors) — no TPS92200 / no CC IC. The SK6812s integrate their own drivers — no series R per
> LED, no ballast. The two wake PWM channels are each a single **AO3400A** low-side MOSFET
> (the MCU never sources LED current directly).

> **No 12 V barrel jack.** Dropped — the wake strips run off the shared **TPS55340 12 V boost**
> (plugged-only). See [`power.md`](power.md) §LED.

## Wake Light (12 V, plugged-only)

Recommended geometry:

-   Diffuser diameter: 100--120 mm (rear/bottom aperture of the cube)
-   Mixing chamber depth: 20--30 mm
-   **12 V COB strip** (warm 3000K + neutral 4000K) around the inside perimeter
-   Matte white interior

Drive warm and cool channels independently with PWM (2 channels).

-   **Plugged-only.** The 12 V boost runs only on the USB-PD contract; firmware won't ramp the wake
    light on battery. Keep the **combined wake-strip wattage modest** so *LED + audio ≤ ~12 W* (the
    TPS55340 boost ceiling from 1S) — e.g. ~1 ft/color at 3.6 W/ft ≈ 7 W, leaving headroom for the amp.

## Wiring

**Wake channels:** MCU PWM -> 100 Ω -> AO3400A gate, 10 kΩ gate pulldown; strip+ to **12 V**,
strip− to FET drain (**low-side** switching). Pins: warm = **IO45**, cool = **IO46**
(see [`esp32.md`](esp32.md)).

**NeoPixels:** IO7 -> SN74AHCT1G125 (VCC 5 V) -> 330 Ω -> DIN(1); DOUT(n) -> DIN(n+1);
all VDD to **5 V**, all VSS to GND. On-board — no wiring harness.

## PWM / data

-   Wake: ≈1 kHz LEDC, gamma correction recommended.
-   NeoPixels: `led_strip` (RMT, 800 kHz); apply gamma + slow ramp curves in firmware.

-   Wake sequence:
    -   0--10 min: warm only
    -   10--20 min: increase intensity
    -   20--30 min: blend toward neutral white

## Assembly

1.  Build wooden cube + aluminum face (status-hole pitch = PCB pixel pitch!).
2.  Paint the wake cavity matte white; install acrylic diffuser; install COB strips.
3.  Solder the 7 SK6812 + buffer on the main PCB (hand-solderable PLCC-4 pads).
4.  Tune PWM/ramp curves; verify hotspot-free dial wash through the glass.

## Notes

-   Prefer CRI \>90 for the wake COB.
-   Spend effort on diffuser geometry rather than higher LED power.
-   Dial-wash pixels: use the **W** channel for warm white (better CRI than R+G+B white).
-   **12 V source — RESOLVED (2026-07-12):** **no barrel jack.** Wake strips run off the shared
    **TPS55340 12 V boost**, gated **plugged-only** (USB-PD contract). On battery: wake light off; the
    amp auto-drops to the 5 V rail (quieter alarm) via the PVDD mux; the NeoPixels stay on 5 V. See
    [`power.md`](power.md) §LED / [`power_values.md`](power_values.md) §8.
-   **Panel string — DROPPED (2026-07-19):** the ≤12-LED Cree CLM3C 5 V string + its AO3400A +
    LEDC channel went away with the display; dial lighting moved to NeoPixels 6–7.
