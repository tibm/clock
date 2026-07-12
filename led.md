# LED System Design for Wooden Alarm Clock

## Overview

Two independent lighting modes, **3 PWM channels**, on **two different rails**:

| Mode | What it lights | Emitter | Rail | PWM |
|------|----------------|---------|------|-----|
| **Wake-up light** | 100–120 mm sunrise diffuser | **12 V** COB, warm **3000K** + neutral **4000K** (tunable-white pair) | **12 V (plugged-only)** | **2** (warm + cool) |
| **Panel light** | Sharp display front-light **+** 75 mm analog dial (one shared channel) | **5 V** discrete warm LEDs (Cree CLM3C-MKW ×≤12) | **5 V (always, incl. battery)** | **1** |

Total PWM outputs: **3**

> **Two rails by design.** The **wake light is bright** → needs the **12 V** rail, which is
> **plugged-only** (the TPS55340 boost runs only on the USB-PD contract; on battery it's off, so
> firmware gates the wake PWM off too). The **panel light is faint** → a cheap string of **5 V
> discrete LEDs**, so the reading/panel light (and the clock) still work on **battery**. See
> [`power.md`](power.md) §LED for the resolved 12 V-source decision (no barrel jack).

> **Panel light = one channel.** The display front-light and the dial ring are always driven at the
> **same intensity and enabled together**, so they share a single PWM / MOSFET. The ≤12 LEDs sit in
> **parallel** (each with its own series resistor — can't series two @ 5 V, Vf 3.2 V), all switched
> by one low-side AO3400A.

## Suppliers (2 total)

1.  **DigiKey** — LEDs, MOSFETs, resistors, connectors (all parts below are DK-stocked).
2.  TAP Plastics (or McMaster-Carr) — diffusers / acrylic.

## Bill of Materials

| Item | Part (DigiKey) | DK # | ~$ | Notes |
|---|---|---|---|---|
| Wake COB — **warm 3000K** | Inspired LED **`12V-COB-3000K-12M`** | 16714316 | **$0.64** /0.98″ seg ($215/12 m reel) | 12 V, 3.6 W/ft, 8 mm, cut @ 0.98″, UL. Wake-warm (IO45). ~1 ft ≈ 12 seg ≈ $7.7. |
| Wake COB — **neutral 4000K** | Inspired LED **`12V-COB-4000K-12M`** | 16714317 | **$0.64** /0.98″ seg | same reel spec; Wake-cool (IO46). *(4000K = neutral; 24 V variant exists if a longer run wants less current.)* |
| Panel LED (×**≤12**) | Cree/Wolfspeed **`CLM3C-MKW-CWAXB233`** | 1987465 | **$0.31** ea | PLCC-2, warm **3200K**, **Vf 3.2 V @ 20 mA**, diffused. 4–6 display + 4–6 dial face. |
| Panel LED series R (×≤12) | ~**180 Ω** 0603 (1 per LED) | — | ~$0.01 | (5 V − 3.2 V)/10 mA = 180 Ω → ~10 mA "faint". Drop to 90 Ω for ~20 mA if brighter needed. |
| Logic MOSFET (×3) | **AO3400A** | — | ~$0.1 ea | one low-side FET per PWM channel; 100 Ω gate + 10 kΩ pulldown. |
| **Amp PVDD rail-mux** | **LTC4412** (SOT-23-6) + P-FET | — | ~$2 | auto priority-OR: **12 V** boost when plugged, **5 V** rail on battery (quieter alarm). ⚠ finalize at schematic. See [`power_values.md`](power_values.md) §8. |
| JST connectors | JST-PH series | — | — | per strip / LED string. |
| Opal diffuser | 3 mm White Opal Acrylic (TAP Plastics) | — | — | wake mixing chamber + dial/frontlight. |
| Aluminum tape | Reflective tape | — | — | line the mixing chamber. |

> **No constant-current driver.** The wake strips are **self-ballasted 12 V COB** (integrated series
> resistors) — no TPS92200 / no CC IC. The panel LEDs are **discrete**, ballasted by **one series
> resistor each** off 5 V. Every PWM channel is a single **AO3400A** low-side MOSFET (the MCU never
> sources LED current directly).

> **No 12 V barrel jack.** Dropped — the wake strips run off the shared **TPS55340 12 V boost**
> (plugged-only). See [`power.md`](power.md) §LED.

## Panel Light (display front-light + analog dial — shared channel, 5 V)

-   Reflective LCD requires **front lighting**, never backlighting.
-   **Discrete warm-white LEDs** (Cree CLM3C-MKW), not a strip — faint, low-power, cheap.
    -   **4–6** around the display opening (front-light), 3--5 mm behind a frosted diffuser.
    -   **4–6** around the **75 mm** dial perimeter, frosted acrylic ring, 8--12 mm cavity painted matte white.
    -   **≤12 total.** All in **parallel** off the **5 V** rail, **one ~180 Ω series R per LED**
        (Vf 3.2 V; two-in-series won't fit under 5 V). ~10 mA each ≈ 0.6 W total worst case.
-   **One PWM (IO7) drives all** via a single AO3400A low-side FET — display + dial track intensity
    and turn on/off together (ambient-gated reading light).
-   Runs off **5 V**, so the panel/reading light is available on **battery** too.

## Wake Light (12 V, plugged-only)

Recommended geometry:

-   Diffuser diameter: 100--120 mm
-   Mixing chamber depth: 20--30 mm
-   **12 V COB strip** (warm 3000K + neutral 4000K) around the inside perimeter
-   Matte white interior

Drive warm and cool channels independently with PWM (2 channels).

-   **Plugged-only.** The 12 V boost runs only on the USB-PD contract; firmware won't ramp the wake
    light on battery. Keep the **combined wake-strip wattage modest** so *LED + audio ≤ ~12 W* (the
    TPS55340 boost ceiling from 1S) — e.g. ~1 ft/color at 3.6 W/ft ≈ 7 W, leaving headroom for the amp.

## Wiring

MCU PWM -\> 100 Ω -\> AO3400A gate

10 kΩ gate pulldown.

Emitter+ to rail, emitter− to FET drain; MOSFET performs **low-side** switching.

-   **Wake** warm/cool COB strips → **12 V** (plugged-only).
-   **Panel** LED string (≤12, each + own series R) → **5 V** (always).

Pin map (see [`esp32.md`](esp32.md)): wake-warm = **IO45**, wake-cool = **IO46**, panel = **IO7**.

## PWM

-   =1 kHz

-   Gamma correction recommended

-   Wake sequence:
    -   0--10 min: warm only
    -   10--20 min: increase intensity
    -   20--30 min: blend toward neutral white

## Assembly

1.  Build wooden enclosure.
2.  Paint lighting cavities matte white.
3.  Install acrylic diffusers.
4.  Install COB strips.
5.  Wire MOSFET boards.
6.  Tune PWM curves.
7.  Verify hotspot-free illumination before final glue-up.

## Notes

-   Prefer CRI \>90 LEDs.
-   COB strips greatly reduce hotspots.
-   Spend effort on diffuser geometry rather than higher LED power.
-   **12 V source — RESOLVED (2026-07-12):** **no barrel jack.** Wake strips run off the shared
    **TPS55340 12 V boost**, gated **plugged-only** (USB-PD contract). On battery: wake light off; the
    amp auto-drops to the 5 V rail (quieter alarm) via the PVDD mux; panel light stays on 5 V. See
    [`power.md`](power.md) §LED / [`power_values.md`](power_values.md) §8.
