# Power

Power tree + battery safety + bring-up. Supersedes `README.md` §10 (kept in sync).

## TL;DR
- **HAND-SOLDERABLE PARTS ONLY** — every IC leaded (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23); no QFN/DFN/BGA. See the root README mfg note.
- **USB-PD in** (CH224K, resistor-set 15 V; fallback 5 V) → **LT3652** 1-cell buck charger (VIN ≤32 V, resistor-set 4.05 V + NTC + timer) → **BAT node = system supply** → rail converters.
- Rails: **3.3 V** (MCU, from 5 V) · **5 V** (panel + stepper) · **12 V boost** (audio, gated) · **15 V VBUS** (LED, plugged only).
- Battery: **user-supplied 18650, Li-ion ONLY** (labeled). **Safe for any 18650 that fits.** Runs with **no cell** on USB (LT3652 holds the BAT node at 4.05 V, sources ≤2 A).
- **48 h backup**, health-cap **~80 % (4.05 V)** — fixed by the float divider; no I²C, SoC via ADC.
- Safety = **double-redundant** (charger CV + one independent protector) + reverse-polarity + NTC temp-qual — **simple, industry-standard for 1S** (not laptop-pack triple-redundant).

## Architecture
```
USB-C ─ CH224K (PD sink, resistor-set 15 V) ─ VBUS 5–15 V ┬─ TPS92200 → LED string   (plugged only)
                                                          └─ LT3652 VIN (buck charger)
   cell path:  holder ─ reverse P-FET ─ [S-8261 + AO4800 dual-FET] ─ 18650(+)   ; Vbat → ESP32 ADC divider
                        LT3652 ── BAT node ─┬─ MT3608  → 5 V   ─┬─ panel + stepper VM
                                            │                  └─ TLV62569 → 3.3 V  (MCU/logic, always)
                                            └─ LM2587  → 12 V   (TAS5760M PVDD, gated, audio)
```
- **1S ≤4.2 V < input** always → buck charger is enough (no buck-boost). The **BAT node is the always-on system rail**: plugged, LT3652 regulates it to 4.05 V (runs with **no cell**); unplugged, the cell supplies it → the **alarm works on battery**. 3.3 V is bucked off the 5 V rail (avoids a leadless buck-boost).

## Rails & budget
| Rail | Source (leaded) | Loads | On |
|---|---|---|---|
| 3.3 V | TLV62569 buck from 5 V (or AMS1117 LDO) | MCU, sensors, logic | always |
| 5 V | MT3608 boost from BAT node | display panel, stepper VM, 3.3 V buck | always |
| 12 V | LM2587 / MT3608 boost from BAT (gated) | TAS5760M PVDD | audio |
| 15 V VBUS | PD input (CH224K) | LED CC driver | plugged |

48 h backup (LEDs off): Wi-Fi modem-sleep ~4 Wh · deep-sleep ~1.3 Wh · 2× alarm ~0.33 Wh. **18650 3500 mAh ≈ 9 Wh usable → ≥2× margin.**

## LED wake light
- ~300–600 lm emitted; diffuser loss → size LEDs **5–10 W**. Warm-white string (~4 series ≈12–15 V) + **TPS92200 CC driver off VBUS (plugged only)**. SK6812 RGBW (5 V) = halo accent. **Off on battery.**

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

**LT3652 (charger)** — autonomous, resistor/cap-programmed (no I²C): **float divider → 4.05 V** (health cap; add a GPIO-switched parallel FB resistor for a 4.2 V "full" mode), **R_SENSE → ICHG ≈ 1–1.75 A** (0.3–0.5 C, gentle/cool), **CTIMER cap → safety timer**, **NTC** on the holder for temp-qualified charge. **CHRG/FAULT** open-drain pins → 2 GPIO. The **BAT node feeds the rail converters** and is regulated to 4.05 V when plugged (runs with no cell, ≤2 A).

**S-8261 + AO4800 (protector)** — no config (thresholds variant-fixed → pick **S-8261AAxMD = OV 4.28 V**; verify the OD/OCD suffix against the datasheet table). Wire the **AO4800 dual N-FET** (charge + discharge FETs) in the cell − path between the 18650 and PACK−, gated by the S-8261, with a couple of R/C for delays. Independent of the charger — the redundant OV/OD/OC/SC cutoff.

**Reverse P-FET** — P-ch MOSFET (e.g. AO3401A / DMP3013), source = holder +, drain = system +, gate → GND via resistor (+ small zener clamp). Correct polarity → on; reversed cell → blocked.

**Cell gauging (ESP32 ADC)** — no gauge IC (none is hand-solderable). A divider off the cell (series R + cap, ideally a MOSFET to disconnect it in deep-sleep) feeds an **ADC pin**; firmware maps voltage → SoC% for UI + **low-battery shutdown (~3.2 V / ~10 %)**. The **80 % health cap is enforced in hardware** by the LT3652 float divider, not firmware.

**Bring-up sequence:** plug → CH224K requests 15 V → LT3652 charges at its resistor-set defaults → MCU boots from the BAT node → reads Vbat (ADC) + CHRG/FAULT → normal run. Unplugged: BAT node from cell (through the protector FETs); LED rail dead; audio 12 V boost still available for the alarm.

## BOM — power + safety (verified active on DigiKey unless noted)
| Function | Part | Pkg (hand-solder) | ~$ | Ref |
|---|---|---|---|---|
| PD sink | **CH224K** | ESSOP-10 | ~$0.4 | LCSC C970725 *(not DK)* |
| Charger (1S buck, BAT-node path) | **LT3652EMSE#PBF** | MSOP-12E | ~$5–6 | DK 2225686 ✅ |
| Fuel gauge | *(none — ESP32 ADC divider)* | — | ~$0 | — |
| Cell protector | **S-8261AAxMD** + **AO4800** dual-N FET | SOT-23-6 + SO-8 | ~$0.7 | ✅ (ABLIC / AOS) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ |
| Cell temp | 10 k NTC (Murata NCP18XH103) | 0402 | ~$0.1 | ✅ |
| Transient | TVS SMAJ22A (VBUS) + SMAJ5.0A (BAT) | SMA | ~$0.4 | ✅ |
| Audio boost BAT→12 V (gated) | LM2587S-ADJ / MT3608 | TO-263 / SOT-23-6 | ~$2 | ✅ |
| 5 V boost (from BAT) | MT3608 / TPS61023 | SOT-23-6 / SOT-563 | ~$1 | ✅ |
| 3.3 V buck (from 5 V) | TLV62569 / AMS1117-3.3 | SOT-23-6 / SOT-223 | ~$0.6 | ✅ |
| LED CC driver (off VBUS) | TPS92200D1 | SOT-23 | ~$1.5 | ✅ |
| 18650 holder | Keystone/MPD | — | ~$1 | ✅ |
| Cell (user-supplied) | protected Li-ion 18650 3–3.5 Ah | — | ~$8 | user adds |

**Power + safety subtotal ≈ $16–18** (excl. cell) — the LT3652 is the priciest line. Safety core (protector + reverse P-FET + NTC + TVS) ≈ $1.4.

## Open decisions
- Backup firmware mode: Wi-Fi modem-sleep (responsive, ~2.5–5 days) vs deep-sleep sync (1–2 weeks).
- Optional one-shot TCO (~77 °C) for extra abuse margin — in or out (default out).
