# Power

Power tree + battery safety + bring-up. Supersedes `README.md` §10 (kept in sync).

## TL;DR
- **HAND-SOLDERABLE PARTS ONLY** — every IC leaded (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23); no QFN/DFN/BGA. See the root README mfg note.
- **USB-PD in** (CH224K, resistor-set 15 V; fallback 5 V) → **LT3652** 1-cell buck charger (VIN ≤32 V, resistor-set 4.05 V + NTC + timer) → **BAT node = system supply** → rail converters.
- Rails: **3.3 V** (MCU, from 5 V) · **5 V** (display panel + stepper + **panel LEDs**) · **12 V boost** (audio + **wake LEDs**, **plugged-only**) · **15 V VBUS** (charger in, plugged).
- Battery: **user-supplied 18650, Li-ion ONLY** (labeled). **Safe for any 18650 that fits.** Runs with **no cell** on USB (LT3652 holds the BAT node at 4.05 V, sources ≤2 A).
- **48 h backup**, health-cap **~80 % (4.05 V)** — fixed by the float divider; no I²C, SoC via ADC.
- Safety = **double-redundant** (charger CV + one independent protector) + reverse-polarity + NTC temp-qual — **simple, industry-standard for 1S** (not laptop-pack triple-redundant).

## Architecture
```
USB-C ─ CH224K (PD sink, resistor-set 15 V) ─ VBUS 5–15 V ── LT3652 VIN (buck charger)
   cell path:  holder ─ reverse P-FET ─ [S-8261 + AO4800 dual-FET] ─ 18650(+)   ; Vbat → ESP32 ADC divider
                        LT3652 ── BAT node ─┬─ TPS61023 → 5 V   ─┬─ display panel + stepper VM + panel LEDs
                                            │                   ├─ TLV62569 → 3.3 V  (MCU/logic, always)
                                            │                   └─ (PVDD mux aux) ── amp PVDD on battery
                                            └─ TPS55340 → 12 V  (plugged-only) ─┬─ TAS5760M PVDD (via mux, priority)
                                                                               └─ wake LED strips (warm+cool)
        amp PVDD = LTC4412 mux: 12 V boost when plugged, else 5 V rail (quieter alarm). Panel LEDs = 5 V (always).
```
- **1S ≤4.2 V < input** always → buck charger is enough (no buck-boost). The **BAT node is the always-on system rail**: plugged, LT3652 regulates it to 4.05 V (runs with **no cell**); unplugged, the cell supplies it → the **alarm works on battery**. 3.3 V is bucked off the 5 V rail (avoids a leadless buck-boost).

## Rails & budget
| Rail | Source (leaded) | Loads | On |
|---|---|---|---|
| 3.3 V | TLV62569 buck from 5 V | MCU, sensors, logic | always |
| 5 V | **TPS61023** boost from BAT node | display panel, stepper VM, 3.3 V buck, **panel LEDs**, **amp PVDD (mux aux)** | always |
| 12 V | **TPS55340** boost from BAT | TAS5760M PVDD (priority, via mux) **+ wake LED strips** | **plugged-only** |
| 15 V VBUS | PD input (CH224K) | LT3652 VIN | plugged |

Exact FB/comp/magnetics values for every converter above are in [`power_values.md`](power_values.md).

48 h backup (LEDs off): Wi-Fi modem-sleep ~4 Wh · deep-sleep ~1.3 Wh · 2× alarm ~0.33 Wh. **18650 3500 mAh ≈ 9 Wh usable → ≥2× margin.**

## LED subsystem (three PWM channels, two rails — full spec in [`led.md`](led.md))
Two lighting modes, PWM-dimmed via **AO3400A low-side MOSFETs** (`MCU PWM → 100 Ω → gate`, 10 kΩ gate
pulldown, ~1 kHz, gamma-corrected). SK6812 halo + TPS92200 CC driver **dropped**. **Two rails:** the
bright wake light is **12 V** (plugged-only, self-ballasted COB); the faint panel light is **5 V**
(always-on, discrete LEDs → works on battery).

| Mode | Emitter | Rail | PWM ch |
|---|---|---|---|
| **Wake-up** (sunrise) | 12 V COB, warm 3000K + neutral 4000K (Inspired LED) | **12 V (plugged-only)** | **2** (warm IO45, cool IO46) |
| **Panel** — dial (75 mm) **+** LCD frontlight, one shared channel (always equal) | ≤12 discrete warm LEDs (Cree CLM3C-MKW, 5 V, series-R each) | **5 V (always)** | **1** (IO7) |

Values + wiring in [`power_values.md`](power_values.md) §8. **Off-by-default at night**, ALS-gated;
warm→neutral sunrise ramp over ~30 min. Dial ring + display frontlight run off **one** PWM (same
intensity, enabled together); merging them freed the 4th channel and returned IO43 to a hardware UART log.

> ✅ **12 V source — RESOLVED (2026-07-12).** **No barrel jack.** The wake strips run off the shared
> **TPS55340 boost**, gated **plugged-only** (enabled only on the USB-PD contract). Rationale: the
> 1S→12 V boost tops out ~12 W, and a bright sunrise on a draining backup cell is pointless — so bright
> LEDs are a mains feature. **On battery:** wake light off; the **amp PVDD auto-drops to the 5 V rail**
> (LTC4412 mux, ~3 W → quieter but audible alarm); the **panel light stays on 5 V**. Firmware keeps
> **wake-LED + audio ≤ ~12 W** when plugged.

## Battery (18650 holder, user-supplied)
- **Li-ion ONLY.** PCB silkscreen + holder label: **"Li-ion 18650 only · 2.5–4.2 V"**. Firmware qualifies cell voltage on insert; refuses out-of-window.
- **Recommend to user:** protected Li-ion 18650, 3000–3500 mAh (Samsung/LG/Panasonic), ~$8.
- Health: charge to **~80 % (4.05 V)**; optional user "top to 100 %".

## Safety (board-level, MANDATORY — simple + redundant)
Assume any 18650: unprotected, wrong SoC, reversed, hot. **If it fits, it must be safe.** Don't rely on the cell's PCM.
- **Overcharge — 2 independent cutoffs:** LT3652 CV at 4.05 V (float divider + safety timer) **and** S-8261 OV ~4.28 V → opens the AO4800 dual-FET.
- **Over-discharge — layered:** firmware shutdown ~3.2 V (ADC) → S-8261 OD ~2.5 V → AO4800 discharge-FET off.
- **Over-current / short:** S-8261 OCD/SCP → AO4800 FET + LT3652 current limit.
- **Reverse insertion:** P-FET on BAT+ (bare cell can't be keyed).
- **Temperature:** NTC on holder → LT3652 NTC pin (charge paused <0/>45 °C — single hot/cold window, *not* multi-zone JEITA) + firmware monitor.
- **Transient/ESD:** TVS on VBUS + BAT.
- **Physical (wood/bedroom):** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- *Dropped as over-engineering for 1S:* secondary OVP, one-shot TCO, PPTC. (TCO ~77 °C remains a cheap optional if extra abuse margin is wanted.)

**Residual:** an internally-shorted/damaged cell can't be fully prevented — mitigated by NTC cutoff, compartment, FR barrier, venting.

## How to use it (config + bring-up)
**CH224K (PD sink)** — set the **CFG1 resistor for 15 V** (no NVM/MCU). Auto-requests 15 V, **falls back to 5 V** if unavailable. VBUS passes to the LED CC driver + LT3652 VIN. Read **PG** to confirm a high-V contract. (LT3652 VIN max 32 V → 15 V has ample margin; 20 V would also be safe, but the LED string is sized ~12–15 V.)

**LT3652 (charger)** — autonomous, resistor/cap-programmed (no I²C): **float divider → 4.05 V** (health cap; a **976 k ∥ R_FB2 switched by a 2N7002** gives a **4.2 V "full" mode** — gate `FULLCHG_EN` on the IO expander), **R_SENSE → ICHG ≈ 1–1.75 A** (0.3–0.5 C, gentle/cool), **CTIMER cap → safety timer**, **NTC** on the holder for temp-qualified charge. **CHRG/FAULT** open-drain pins → 2 GPIO. The **BAT node feeds the rail converters** and is regulated to 4.05 V when plugged (runs with no cell, ≤2 A).

**S-8261 + AO4800 (protector)** — no config (thresholds variant-fixed → pick **S-8261AAxMD = OV 4.28 V**; verify the OD/OCD suffix against the datasheet table). Wire the **AO4800 dual N-FET** (charge + discharge FETs) in the cell − path between the 18650 and PACK−, gated by the S-8261, with a couple of R/C for delays. Independent of the charger — the redundant OV/OD/OC/SC cutoff.

**Reverse P-FET** — P-ch MOSFET (e.g. AO3401A / DMP3013), source = holder +, drain = system +, gate → GND via resistor (+ small zener clamp). Correct polarity → on; reversed cell → blocked.

**Cell gauging (ESP32 ADC)** — no gauge IC (none is hand-solderable). A 100 k/100 k divider off the cell (+ 100 nF, and a **2N7002 in the bottom leg — gate `VBAT_DIV_EN` on the IO expander — to disconnect it in deep-sleep**) feeds an **ADC pin** (IO1 `VBAT_SENSE`, ADC1); firmware maps voltage → SoC% for UI + **low-battery shutdown (~3.2 V / ~10 %)**. The **80 % health cap is enforced in hardware** by the LT3652 float divider, not firmware.

**Bring-up sequence:** plug → CH224K requests 15 V → LT3652 charges at its resistor-set defaults → MCU boots from the BAT node → reads Vbat (ADC) + CHRG/FAULT → normal run; with `PD_PG` asserted, firmware may enable the 12 V boost (audio at full 12 V + wake sunrise). **Unplugged:** BAT node from cell (through the protector FETs); the **12 V boost stays off** → wake light disabled, and the amp PVDD auto-drops to the **5 V rail** (LTC4412 mux) for a **quieter but audible alarm**. Panel light (5 V) and the clock run normally on battery.

## BOM — power + safety (verified active on DigiKey unless noted)
| Function | Part | Pkg (hand-solder) | ~$ | Ref |
|---|---|---|---|---|
| PD sink | **CH224K** | ESSOP-10 | ~$0.4 | LCSC C970725 *(not DK)* |
| Charger (1S buck, BAT-node path) | **LT3652EMSE#PBF** | MSOP-12E | ~$5–6 | DK 2225686 ✅ |
| Fuel gauge | *(none — ESP32 ADC divider)* | — | ~$0 | — |
| Cell protector | **S-8261AAxMD** + **AO4800** dual-N FET | SOT-23-6 + SO-8 | ~$0.7 | ✅ (ABLIC / AOS) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ |
| Cell temp | 10 k NTC (Murata NCP18XH103) | 0603 | ~$0.1 | ✅ |
| Transient | TVS SMAJ22A (VBUS) + SMAJ5.0A (BAT) | SMA | ~$0.4 | ✅ |
| Audio+wake-LED boost BAT→12 V (plugged-only) | **TPS55340PWPR** | HTSSOP-14 | ~$2.5 | ✅ |
| 5 V boost (from BAT) | **TPS61023DRLR** | SOT-563 | ~$1.2 | ✅ |
| 3.3 V buck (from 5 V) | TLV62569 / AMS1117-3.3 | SOT-23-6 / SOT-223 | ~$0.6 | ✅ |
| Amp PVDD rail-mux (12 V↔5 V) | **LTC4412** + P-FET | SOT-23-6 | ~$2 | ⚠ finalize (see `power_values.md` §8) |
| LED low-side switches (3×) | **AO3400A** + 100 Ω/10 kΩ | SOT-23 | ~$0.1 ea | ✅ (2× wake 12 V + 1× panel 5 V; see `led.md`) |
| 18650 holder | Keystone/MPD | — | ~$1 | ✅ |
| Cell (user-supplied) | protected Li-ion 18650 3–3.5 Ah | — | ~$8 | user adds |

**Power + safety subtotal ≈ $16–18** (excl. cell) — the LT3652 is the priciest line. Safety core (protector + reverse P-FET + NTC + TVS) ≈ $1.4.

## Open decisions
- ~~**LED 12 V source**~~ — **RESOLVED 2026-07-12:** no barrel jack; shared TPS55340 boost, plugged-only; amp PVDD auto-mux (LTC4412) to 5 V on battery; panel LEDs on 5 V. Bench: finalize the mux FET network + confirm wake-COB wattage keeps LED + audio ≤ ~12 W.
- Backup firmware mode: Wi-Fi modem-sleep (responsive, ~2.5–5 days) vs deep-sleep sync (1–2 weeks).
- Optional one-shot TCO (~77 °C) for extra abuse margin — in or out (default out).
