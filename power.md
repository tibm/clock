# Power

Power tree + battery safety + bring-up. Supersedes `README.md` §10 (kept in sync).

## TL;DR
- **HAND-SOLDERABLE PARTS ONLY** — every IC leaded (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23); no QFN/DFN/BGA. See the root README mfg note.
- **USB-PD in** (CH224K, resistor-set 15 V; fallback 5 V) → **LT3652** 1-cell buck charger (VIN ≤32 V, resistor-set 4.05 V + NTC + timer) → **BAT node = system supply** → rail converters.
- Rails: **3.3 V** (MCU, from 5 V) · **5 V** (stepper + **NeoPixels** + knob encoder) · **12 V boost** (audio + **wake LEDs**, **plugged-only**) · **15 V VBUS** (charger in, plugged).
- Battery: **user-supplied 18650, Li-ion ONLY** (labeled). **Safe for any 18650 that fits.** Runs with **no cell** on USB (LT3652 holds the BAT node at 4.05 V, sources ≤2 A).
- **48 h backup**, health-cap **~80 % (4.05 V)** — fixed by the float divider; no I²C, SoC via ADC.
- Safety = **double-redundant** (charger CV + one independent protector) + reverse-polarity + NTC temp-qual — **simple, industry-standard for 1S** (not laptop-pack triple-redundant).

## Architecture
```
USB-C ─ CH224K (PD sink, resistor-set 15 V) ─ VBUS 5–15 V ── LT3652 VIN (buck charger)
   cell path:  holder ─ reverse P-FET ─ [HY2111 + AOSD32334C dual-FET] ─ 18650(+)   ; Vbat → ESP32 ADC divider
                        LT3652 ── BAT node ─┬─ TPS61023 → 5 V   ─┬─ stepper VM + 7x SK6812 NeoPixels + EM14 encoder
                                            │                   ├─ TLV62569 → 3.3 V  (MCU/logic, always)
                                            │                   └─ (PVDD mux aux) ── amp PVDD on battery
                                            └─ TPS55340 → 12 V  (plugged-only) ─┬─ TAS5760M PVDD (via mux, priority)
                                                                               └─ wake LED strips (warm+cool)
        amp PVDD = LTC4412 mux: 12 V boost when plugged, else 5 V rail (quieter alarm). NeoPixels = 5 V (always).
```
- **1S ≤4.2 V < input** always → buck charger is enough (no buck-boost). The **BAT node is the always-on system rail**: plugged, LT3652 regulates it to 4.05 V (runs with **no cell**); unplugged, the cell supplies it → the **alarm works on battery**. 3.3 V is bucked off the 5 V rail (avoids a leadless buck-boost).

## Rails & budget
| Rail | Source (leaded) | Loads | On |
|---|---|---|---|
| 3.3 V | TLV62569 buck from 5 V | MCU, sensors, logic | always |
| 5 V | **TPS61023** boost from BAT node | stepper VM, 3.3 V buck, **7× SK6812 NeoPixels** (≤~0.6 A all-white), **EM14 knob encoder** (~30 mA), **amp PVDD (mux aux)** | always |
| 12 V | **TPS55340** boost from BAT | TAS5760M PVDD (priority, via mux) **+ wake LED strips** | **plugged-only** |
| 15 V VBUS | PD input (CH224K) | LT3652 VIN | plugged |

Exact FB/comp/magnetics values for every converter above are in [`power_values.md`](power_values.md).

48 h backup (LEDs off): Wi-Fi modem-sleep ~4 Wh · deep-sleep ~1.3 Wh · 2× alarm ~0.33 Wh. **18650 3500 mAh ≈ 9 Wh usable → ≥2× margin.**

## LED subsystem (2 PWM channels + 1 RMT data line, two rails — full spec in [`led.md`](led.md))
Two subsystems (v0.19: the 5 V discrete panel string was dropped with the display):

| Mode | Emitter | Rail | Drive |
|---|---|---|---|
| **Wake-up** (sunrise) | 12 V COB, warm 3000K + neutral 4000K (Inspired LED), via **AO3400A** low-side (`PWM → 100 Ω → gate`, 10 k pulldown, ~1 kHz) | **12 V (plugged-only)** | **2× LEDC** (warm IO45, cool IO46) |
| **Status + dial NeoPixels** — 5 status pixels behind the face + 2 dial-wash pixels | **7× SK6812 RGBW** on-PCB, one data chain via **SN74AHCT1G125** 3V3→5 V buffer | **5 V (always)** | **1 RMT data GPIO** (IO7) |

Values + wiring in [`power_values.md`](power_values.md) §8. **Off-by-default at night**, ALS-gated;
warm→neutral sunrise ramp over ~30 min. NeoPixels work on battery (status LEDs + dial light);
worst-case all-white ≈ 0.6 A on 5 V, real status/dial duty ≪.

> ✅ **12 V source — RESOLVED (2026-07-12).** **No barrel jack.** The wake strips run off the shared
> **TPS55340 boost**, gated **plugged-only** (enabled only on the USB-PD contract). Rationale: the
> 1S→12 V boost tops out ~12 W, and a bright sunrise on a draining backup cell is pointless — so bright
> LEDs are a mains feature. **On battery:** wake light off; the **amp PVDD auto-drops to the 5 V rail**
> (LTC4412 mux, ~3 W → quieter but audible alarm); the **NeoPixels stay on 5 V** (status + dial light). Firmware keeps
> **wake-LED + audio ≤ ~12 W** when plugged.

## Battery (18650 holder, user-supplied)
- **Li-ion ONLY.** PCB silkscreen + holder label: **"Li-ion 18650 only · 2.5–4.2 V"**. Firmware qualifies cell voltage on insert; refuses out-of-window.
- **Recommend to user:** protected Li-ion 18650, 3000–3500 mAh (Samsung/LG/Panasonic), ~$8.
- Health: charge to **~80 % (4.05 V)**; optional user "top to 100 %".

## Safety (board-level, MANDATORY — simple + redundant)
Assume any 18650: unprotected, wrong SoC, reversed, hot. **If it fits, it must be safe.** Don't rely on the cell's PCM.
- **Overcharge — 2 independent cutoffs:** LT3652 CV at 4.05 V (float divider + safety timer) **and** HY2111 OV 4.28 V → opens the AOSD32334C dual-FET.
- **Over-discharge — layered:** firmware shutdown ~3.2 V (ADC) → HY2111 OD 2.90 V → AOSD32334C discharge-FET off.
- **Over-current / short:** HY2111 OCD/SCP → AOSD32334C FET + LT3652 current limit.
- **Reverse insertion:** P-FET on BAT+ (bare cell can't be keyed).
- **Temperature:** NTC on holder → LT3652 NTC pin (charge paused <0/>45 °C — single hot/cold window, *not* multi-zone JEITA) + firmware monitor.
- **One-shot thermal cutoff (TCO ~77 °C):** non-resettable thermal fuse in series with the cell (in the cell − / PACK− path with the protector FETs), mounted against the holder. Independent of the NTC/charger — trips on any runaway heat (charge *or* discharge) and permanently opens the pack. *(Added 2026-07-12 for extra abuse margin.)*
- **Transient/ESD:** TVS on VBUS + BAT.
- **Physical (wood/bedroom):** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- *Dropped as over-engineering for 1S:* secondary OVP, PPTC.

**Residual:** an internally-shorted/damaged cell can't be fully prevented — mitigated by NTC cutoff, compartment, FR barrier, venting.

## How to use it (config + bring-up)
**CH224K (PD sink)** — set the **CFG1 resistor for 15 V** (no NVM/MCU). Auto-requests 15 V, **falls back to 5 V** if unavailable. VBUS feeds **LT3652 VIN only** — the LEDs run off the internal boosts (wake = 12 V, NeoPixels = 5 V), **not VBUS**. Read **PG** to confirm a high-V contract. (LT3652 VIN max 32 V → 15 V has ample margin; 20 V would also be safe, but 15 V is chosen for headroom over the 12 V audio/wake boost.)

**LT3652 (charger)** — autonomous, resistor/cap-programmed (no I²C): **float divider → 4.05 V** (health cap; a **976 k ∥ R_FB2 switched by a 2N7002** gives a **4.2 V "full" mode** — gate `FULLCHG_EN` on the IO expander), **R_SENSE → ICHG ≈ 1–1.75 A** (0.3–0.5 C, gentle/cool), **CTIMER cap → safety timer**, **NTC** on the holder for temp-qualified charge. **CHRG/FAULT** open-drain pins → 2 GPIO. The **BAT node feeds the rail converters** and is regulated to 4.05 V when plugged (runs with no cell, ≤2 A).

**HY2111 + AOSD32334C (protector)** — no config (thresholds fixed by the part-number suffix → **HY2111-GB = OV 4.28 V (release 4.08 V) / OD 2.90 V / OC 150 mV**, SOT-23-6). Wire the **AOSD32334C dual N-FET** (charge + discharge FETs) in the cell − path between the 18650 and PACK−, gated by the HY2111 **OC/OD** pins; support network per its datasheet §10: **R1 100 Ω** cell+→VDD, **C1 0.1 µF** VDD–VSS, **R2 2 kΩ** CS→PACK− — all delays are internal. Independent of the charger — the redundant OV/OD/OC/SC cutoff. *(Replaced the NRND/obsolete **AP9101CK6-BX** 2026-07-17 — same pin arrangement (1 OD · 2 CS · 3 OC · 4 NC · 5 VDD · 6 VSS), nets unchanged. The exact-threshold quality twins — ABLIC S-8261ABMMD, Nisshinbo R5478N — are reel-only/3000 MOQ at DigiKey, so the HY2111-GB comes from **LCSC C82747** like the CH224K.)*

**Reverse P-FET** — P-ch MOSFET (e.g. AO3401A / DMP3013), source = holder +, drain = system +, gate → GND via resistor (+ small zener clamp). Correct polarity → on; reversed cell → blocked.

**Cell gauging (ESP32 ADC)** — no gauge IC (none is hand-solderable). A 100 k/100 k divider off the cell (+ 100 nF, and a **2N7002 in the bottom leg — gate `VBAT_DIV_EN` on the IO expander — to disconnect it in deep-sleep**) feeds an **ADC pin** (IO1 `VBAT_SENSE`, ADC1); firmware maps voltage → SoC% for UI + **low-battery shutdown (~3.2 V / ~10 %)**. The **80 % health cap is enforced in hardware** by the LT3652 float divider, not firmware.

**Bring-up sequence:** plug → CH224K requests 15 V → LT3652 charges at its resistor-set defaults → MCU boots from the BAT node → reads Vbat (ADC) + CHRG/FAULT → normal run; with `PD_PG` asserted, firmware may enable the 12 V boost (audio at full 12 V + wake sunrise). **Unplugged:** BAT node from cell (through the protector FETs); the **12 V boost stays off** → wake light disabled, and the amp PVDD auto-drops to the **5 V rail** (LTC4412 mux) for a **quieter but audible alarm**. NeoPixels (5 V status + dial light) and the clock run normally on battery.

## BOM — power + safety (verified active on DigiKey unless noted)
| Function | Part | Pkg (hand-solder) | ~$ | Ref |
|---|---|---|---|---|
| PD sink | **CH224K** | ESSOP-10 | ~$0.4 | LCSC C970725 *(not DK)* |
| Charger (1S buck, BAT-node path) | **LT3652EMSE#PBF** | MSOP-12E | ~$9.9 | DK 2225686 ✅ |
| Fuel gauge | *(none — ESP32 ADC divider)* | — | ~$0 | — |
| Cell protector | **HY2111-GB** + **AOSD32334C** dual-N FET | SOT-23-6 + SO-8 | ~$0.9 | ✅ (HYCON via LCSC / AOS via DigiKey) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ |
| Cell temp | 10 k NTC (Murata NCP18XH103) | 0603 | ~$0.1 | ✅ |
| **One-shot TCO (~77 °C)** | thermal fuse in cell − path (e.g. SEFUSE SF/Bourns bimetal ~77 °C) | radial/tab | ~$0.4 | ⚠ pick + file datasheet |
| Transient | TVS SMAJ22A (VBUS) + SMAJ5.0A (BAT) | SMA | ~$0.4 | ✅ |
| Audio+wake-LED boost BAT→12 V (plugged-only) | **TPS55340PWPR** | HTSSOP-14 | ~$2.5 | ✅ |
| 5 V boost (from BAT) | **TPS61023DRLR** | SOT-563 | ~$1.2 | ✅ |
| 3.3 V buck (from 5 V) | TLV62569 / AMS1117-3.3 | SOT-23-6 / SOT-223 | ~$0.6 | ✅ |
| Amp PVDD rail-mux (12 V↔5 V) | **LTC4412** + P-FET | SOT-23-6 | ~$2 | ⚠ finalize (see `power_values.md` §8) |
| LED low-side switches (2×) | **AO3400A** + 100 Ω/10 kΩ | SOT-23 | ~$0.1 ea | ✅ (2× wake 12 V; panel FET recovered v0.19; see `led.md`) |
| NeoPixel data buffer | **SN74AHCT1G125** (3V3→5 V) | SOT-23-5 | ~$0.14 | ✅ DK 376028 |
| 18650 holder | Keystone/MPD | — | ~$1 | ✅ |
| Cell (user-supplied) | protected Li-ion 18650 3–3.5 Ah | — | ~$8 | user adds |

**Power + safety subtotal ≈ $20–22** (excl. cell) — the LT3652 is the priciest line. Safety core (protector + reverse P-FET + NTC + TVS + ~77 °C TCO) ≈ $1.8.

## Open decisions
- ~~**LED 12 V source**~~ — **RESOLVED 2026-07-12:** no barrel jack; shared TPS55340 boost, plugged-only; amp PVDD auto-mux (LTC4412) to 5 V on battery; NeoPixels on 5 V *(panel string dropped v0.19)*. Bench: finalize the mux FET network + confirm wake-COB wattage keeps LED + audio ≤ ~12 W.
- ~~**Backup firmware mode**~~ — **RESOLVED 2026-07-12: adaptive.** Wi-Fi modem-sleep while SoC is healthy (responsive to app/BLE/alarm) → **drop to deep-sleep at low battery** to stretch runtime; alarm always fires. Threshold TBD in firmware (~30 % SoC start point).
- ~~**One-shot TCO (~77 °C)**~~ — **RESOLVED 2026-07-12: in.** Non-resettable thermal fuse in the cell − path (see §Safety + BOM). Bench/BOM: pick the exact part + file its datasheet.
