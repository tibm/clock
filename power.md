# Power

Power tree + battery safety + bring-up. Supersedes `README.md` §10 (kept in sync).

## TL;DR
- **USB-PD in** (STUSB4500, request 15/9 V; fallback 5 V) → **BQ25628E** 1-cell buck charger (integrated FETs, NVDC power-path, JEITA, ship-mode) → **SYS** → rail converters.
- Rails: **3.3 V** (MCU) · **5 V** (panel + stepper) · **12 V boost** (audio, gated) · **15 V VBUS** (LED, plugged only).
- Battery: **user-supplied 18650, Li-ion ONLY** (labeled). **Safe for any 18650 that fits.** Runs with **no cell** on USB.
- **48 h backup**, health-cap **~80 % (4.05 V)**.
- Safety = **double-redundant** (charger + one independent protector) + reverse-polarity + NTC/JEITA — **simple, industry-standard for 1S** (not laptop-pack triple-redundant).

## Architecture
```
USB-C ─ STUSB4500 (PD sink, VBUS gate) ─ VBUS 5–15 V ┬─ TPS92200 → LED string   (plugged only)
                                                     └─ BQ25628E VIN (buck charger + NVDC path)
   cell path:  holder ─ reverse P-FET ─ LC05111 (protector, integ. FET) ─ 18650(+)   ; MAX17048 on cell
                                          BQ25628E ── SYS ─┬─ TPS63900 → 3.3 V  (MCU, always)
                                                           ├─ TPS61023 → 5 V    (panel + stepper, always)
                                                           └─ TPS61088 → 12 V   (TAS5825M PVDD, gated)
```
- **1S ≤4.2 V < input** always → buck charger is enough (no buck-boost). Audio 12 V boosts from SYS so the **alarm works on battery**.

## Rails & budget
| Rail | Source | Loads | On |
|---|---|---|---|
| 3.3 V | TPS63900 (buck-boost, 75 nA Iq) | MCU, sensors, logic | always |
| 5 V | TPS61023 boost | display panel, stepper VM | always |
| 12 V | TPS61088 boost (gated) | TAS5825M PVDD | audio |
| 15 V VBUS | PD input | LED CC driver | plugged |

48 h backup (LEDs off): Wi-Fi modem-sleep ~4 Wh · deep-sleep ~1.3 Wh · 2× alarm ~0.33 Wh. **18650 3500 mAh ≈ 9 Wh usable → ≥2× margin.**

## LED wake light
- ~300–600 lm emitted; diffuser loss → size LEDs **5–10 W**. Warm-white string (~4 series ≈12–15 V) + **TPS92200 CC driver off VBUS (plugged only)**. SK6812 RGBW (5 V) = halo accent. **Off on battery.**

## Battery (18650 holder, user-supplied)
- **Li-ion ONLY.** PCB silkscreen + holder label: **"Li-ion 18650 only · 2.5–4.2 V"**. Firmware qualifies cell voltage on insert; refuses out-of-window.
- **Recommend to user:** protected Li-ion 18650, 3000–3500 mAh (Samsung/LG/Panasonic), ~$8.
- Health: charge to **~80 % (4.05 V)**; optional user "top to 100 %".

## Safety (board-level, MANDATORY — simple + redundant)
Assume any 18650: unprotected, wrong SoC, reversed, hot. **If it fits, it must be safe.** Don't rely on the cell's PCM.
- **Overcharge — 2 independent cutoffs:** BQ25628E CV at 4.05 V (+ input OVP, safety timer) **and** LC05111 OV ~4.28 V (integrated FET).
- **Over-discharge — layered:** firmware shutdown ~3.2 V → BQ25628E BATFET → LC05111 OD ~2.7 V.
- **Over-current / short:** LC05111 OCD/SCP (integrated FET) + BQ25628E limits.
- **Reverse insertion:** P-FET on BAT+ (bare cell can't be keyed). *(BQ25628E also has an input RBFET on the USB side.)*
- **Temperature:** NTC on holder → BQ25628E JEITA (no charge <0/>45 °C) + firmware monitor.
- **Transient/ESD:** TVS on VBUS + BAT.
- **Physical (wood/bedroom):** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- *Dropped as over-engineering for 1S:* secondary OVP, one-shot TCO, PPTC. (TCO ~77 °C remains a cheap optional if extra abuse margin is wanted.)

**Residual:** an internally-shorted/damaged cell can't be fully prevented — mitigated by NTC cutoff, compartment, FR barrier, venting.

## How to use it (config + bring-up)
**STUSB4500 (PD sink)** — program NVM once (I²C or preset): PDO1 = 5 V (dead-battery), PDO2 = 9 V, **PDO3 = 15 V (max — do NOT set 20 V; BQ25628E VIN max 18 V)**. Auto-negotiates highest match, falls back to 5 V. On-chip PMOS gates pass VBUS to the LED driver + charger.

**BQ25628E (charger)** — over I²C at boot set: **VBATREG = 4.05 V** (health; 4.2 V for user "full"), **ICHG ≈ 1–1.75 A** (0.3–0.5 C, gentle/cool), input-current limit per negotiated PD, **JEITA** thresholds via the TS/NTC pin. Poll status/fault regs. Use **ship mode** (BATFET off, 1.5 µA) for shipping/deep storage. SYS (NVDC) feeds the three rail converters.

**LC05111CMT (protector)** — no config (thresholds are variant-fixed → use **LC05111C02** = OV 4.28 V / OD 2.70 V). Wire in the cell − path with its integrated FETs between the 18650 and PACK−; independent of the charger. This is the redundant OV/OD/OC/SC cutoff.

**Reverse P-FET** — P-ch MOSFET (e.g. AO3401A / DMP3013), source = holder +, drain = system +, gate → GND via resistor (+ small zener clamp). Correct polarity → on; reversed cell → blocked.

**MAX17048 (gauge)** — I²C on the cell; issue quick-start on insert. Firmware uses SoC% for UI, **low-battery shutdown (~3.2 V / ~10 %)**, and to stop charge at the **80 % health cap**.

**Bring-up sequence:** plug → STUSB4500 negotiates → BQ25628E charges at defaults → MCU boots from SYS → MCU writes BQ25628E (VBATREG/ICHG/JEITA) + quick-starts MAX17048 → normal run. Unplugged: SYS from cell via BATFET; LED rail dead; audio 12 V boost still available for the alarm.

## BOM — power + safety (verified active on DigiKey unless noted)
| Function | Part | Pkg | ~$ | Ref |
|---|---|---|---|---|
| PD sink | **STUSB4500QTR** | QFN-24 4×4 | ~$2 | DK 9092189 ✅ |
| Charger + path (1S buck, integ. FETs) | **BQ25628ERYKR** | WQFN-18 3×2.5 | ~$1.5 | DK 21298592 ✅ |
| Fuel gauge | **MAX17048G+T10** | µDFN/WLP | ~$1.5 | ✅ |
| Cell protector (integ. FET) | **LC05111C02MTTTG** | WDFN-6 2.6×4 | ~$0.6 | ✅ (onsemi) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ |
| Cell temp | 10 k NTC (Murata NCP18XH103) | 0402 | ~$0.1 | ✅ |
| Transient | TVS SMAJ22A (VBUS) + SMAJ5.0A (BAT) | SMA | ~$0.4 | ✅ |
| Audio boost SYS→12 V | TPS61088RHLR | VQFN-20 | ~$2 | ✅ |
| 5 V boost | TPS61023 | SOT-563 | ~$1 | ✅ |
| 3.3 V buck-boost (low Iq) | TPS63900 | VQFN | ~$2 | ✅ |
| LED CC driver (off VBUS) | TPS92200D1 | SOT-23 | ~$1.5 | ✅ |
| 18650 holder | Keystone/MPD | — | ~$1 | ✅ |
| Cell (user-supplied) | protected Li-ion 18650 3–3.5 Ah | — | ~$8 | user adds |

**Power + safety subtotal ≈ $15–16** (excl. cell) — down from ~$23–26. Safety core (protector + reverse P-FET + NTC + TVS) ≈ $1.3.

## Open decisions
- Backup firmware mode: Wi-Fi modem-sleep (responsive, ~2.5–5 days) vs deep-sleep sync (1–2 weeks).
- Optional one-shot TCO (~77 °C) for extra abuse margin — in or out (default out).
