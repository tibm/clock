# Power

Power tree + battery safety + bring-up. Supersedes `README.md` §10 (kept in sync).

## TL;DR
- **HAND-SOLDERABLE PARTS ONLY** — every IC leaded (SOIC/SOP/SSOP/TSSOP/HTSSOP/MSOP/SOT-23); no QFN/DFN/BGA. See the root README mfg note.
- **USB-PD in** (CH224K, resistor-set 15 V; fallback 5 V) → **LT3652** 1-cell buck charger (VIN ≤32 V, resistor-set 4.05 V + NTC + timer) → **BAT node = system supply** → rail converters.
- Rails: **3.3 V** (MCU, from 5 V) · **5 V** (stepper + **NeoPixels** + knob encoder) · **12 V boost** (audio + **wake LEDs**, **plugged-only**) · **15 V VBUS** (charger in, plugged).
- Battery: **user-supplied 18650, Li-ion ONLY** (labeled). **Safe for any 18650 that fits.** Runs with **no cell** on USB (LT3652 holds the BAT node at 4.05 V, sources ≤2 A) — but it **cannot cold-start into a 0 V BAT node**: see *Cold-start with no cell* under [How to use it](#how-to-use-it-config--bring-up).
- **48 h backup**, health-cap **~80 % (4.05 V)** — fixed by the float divider; no I²C, SoC via ADC.
- Safety = **double-redundant** (charger CV + one independent protector) + reverse-polarity + NTC temp-qual — **simple, industry-standard for 1S** (not laptop-pack triple-redundant).

## Architecture
```
USB-C ─ CH224K (PD sink, resistor-set 15 V) ─ VBUS 5–15 V ── LT3652 VIN (buck charger)
   cell path:  holder ─ reverse P-FET ─ [HY2111 + dual-FET: AOSD32334C→AO4838 v0.4] ─ 18650(+)   ; Vbat → ESP32 ADC divider
                        LT3652 ── BAT node ─┬─ TPS61023 → 5 V   ─┬─ stepper VM + 7x SK6812 NeoPixels + EM14 encoder
                                            │                   ├─ TLV62569 → 3.3 V  (MCU/logic, always)
                                            │                   └─ (PVDD mux aux) ── amp PVDD on battery
                                            └─ TPS55340 → 12 V  (plugged-only) ─┬─ TAS5760M PVDD (via mux, priority)
                                                                               └─ wake LED strips (warm+cool)
        amp PVDD = LTC4412 mux: 12 V boost when plugged, else 5 V rail (quieter alarm). NeoPixels = 5 V (always).
```
- **1S ≤4.2 V < input** always → buck charger is enough (no buck-boost). The **BAT node is the always-on system rail**: plugged, LT3652 regulates it to 4.05 V (runs with **no cell** — *once the node is above 2.84 V; it cannot start there, see Cold-start below*); unplugged, the cell supplies it → the **alarm works on battery**. 3.3 V is bucked off the 5 V rail (avoids a leadless buck-boost).

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
- **Overcharge — 2 independent cutoffs:** LT3652 CV at 4.05 V (float divider + safety timer) **and** HY2111 OV 4.28 V → opens the protector dual-FET (`AOSD32334C` as built; **AO4838** from v0.4, **V9**).
- **Over-discharge — layered:** firmware shutdown ~3.2 V (ADC) → HY2111 OD 2.90 V → discharge-FET off.
- **Over-current / short:** HY2111 OCD/SCP → protector FET pair + LT3652 current limit. ⚠ **v0.4 V9 deliberately raises the discharge-OC trip** from ~1.9–3.0 A to **5.3–8.7 A** (`-HB` `V_DIP` over the AO4838's 26–33 mΩ pair). This is a loosening of a safety threshold and is recorded as such: it is forced by the TPS61023's 3.7 A startup surge, which sets a floor under any usable trip point until **V8** gates the boost. It stays defensible because the board's own worst case is a **2.3 A** sunrise alarm (2.3× margin), short circuits are still caught by SCP (`V_SIP` 0.85 V ≈ 26–33 A, `T_SIP` 500 µs), and a sustained fault between those two bands is covered by the **NTC** and the **77 °C TCO**. Revisit the trip point once V8 lands.
- **Reverse insertion:** P-FET on BAT+ (bare cell can't be keyed).
- **Temperature:** NTC on holder → LT3652 NTC pin (charge paused <0/>45 °C — single hot/cold window, *not* multi-zone JEITA) + firmware monitor.
- **One-shot thermal cutoff (TCO ~77 °C):** non-resettable thermal fuse in series with the cell (in the cell − / PACK− path with the protector FETs), mounted against the holder. Independent of the NTC/charger — trips on any runaway heat (charge *or* discharge) and permanently opens the pack. *(Added 2026-07-12 for extra abuse margin.)*
- **Transient/ESD:** TVS on VBUS + BAT.
- **Physical (wood/bedroom):** ventilated cell compartment, FR/metal barrier vs wood, spacing from amp/charger heat, vent path, secure retention.
- *Dropped as over-engineering for 1S:* secondary OVP, PPTC.

**Residual:** an internally-shorted/damaged cell can't be fully prevented — mitigated by NTC cutoff, compartment, FR barrier, venting.

## How to use it (config + bring-up)
**CH224K (PD sink)** — set the **CFG1 resistor for 15 V** (no NVM/MCU). Auto-requests 15 V; if the source has no 15 V PDO it **falls back to the highest PDO below the request**, *not* to 5 V — bench-proven 2026-09-11, a 5 V/9 V brick yielded 9 V, below the LT3652's 11.2 V UVLO so nothing downstream came up (`FIRMWARE.md` §12.0.12). **The brick must advertise 15 V.** VBUS feeds **LT3652 VIN only** — the LEDs run off the internal boosts (wake = 12 V, NeoPixels = 5 V), **not VBUS**. Read **PG** to confirm a high-V contract. (LT3652 VIN max 32 V → 15 V has ample margin; 20 V would also be safe, but 15 V is chosen for headroom over the 12 V audio/wake boost.)

**LT3652 (charger)** — autonomous, resistor/cap-programmed (no I²C): **float divider → 4.05 V** (health cap; a **976 k ∥ R_FB2 switched by a 2N7002** gives a **4.2 V "full" mode** — gate `FULLCHG_EN` on the IO expander), **R_SENSE → ICHG ≈ 1–1.75 A** (0.3–0.5 C, gentle/cool), **CTIMER cap → safety timer**, **NTC** on the holder for temp-qualified charge. **CHRG/FAULT** open-drain pins → 2 GPIO. The **BAT node feeds the rail converters** and is regulated to 4.05 V when plugged (runs with no cell, ≤2 A).

**⚠ Cold-start with no cell — the precondition trap (bench, 2026-09-11).** The "runs with no cell" claim is true only once the BAT node is *already* above the precondition threshold. **From 0 V it is not.**

The LT3652 enters precondition whenever `V_FB` < `V_FB(PRE)` = **2.3 V** — BAT node < **2.84 V**, 70 % of the 4.05 V float — and clamps charge current to `V_SENSE(PRE)` = **15 mV** across `R18` (0.1 Ω) = **150 mA**, 15 % of the programmed 1 A. But `U5`'s `EN` is **hard-tied to VBAT**, so the 5 V boost, the 3.3 V buck and a booting ESP32-S3 all switch on the moment the node has any voltage at all. The board's idle draw exceeds what 150 mA can supply, and it settles into a **stable collapsed operating point**:

| node | measured | should be | feedback pin |
|---|---|---|---|
| BAT node | **1.3 V** | 4.05 V | `R15` pin 1 = 1.1 V ✓ (1.3 × 200/245.3) |
| +5 V | **2.9 V** | 4.99 V | `R41` = 350 mV ✓ (2.9 × 100/832) |
| +3V3 | **3.0 V** | 3.32 V | buck at 100 % duty, passing 2.9 V through |

Every divider reads exactly right *for those voltages* — nothing downstream is broken.

**It is a startup lockout, not a steady-state deficit** (refined 2026-09-11 with the board running: idle draw measured **90 mA at 3.5 V = 315 mW**). At the 4.05 V float, 150 mA is 608 mW — the charger has ample margin *once it gets there*. The trap is that the boost is a **constant-power sink**: it needs ~370 mW in (315 mW at ~85 %) regardless of input voltage, so below **~2.5 V** (0.15 A × 2.47 V = 370 mW) the 150 mA clamp cannot feed it, and it must also charge ~220 µF of BAT-node bulk (`C107` + `C129` + `C106` + `C120`) at the same time. The system is **bistable** — a stable low well at 1.3 V and a stable high point at 4.05 V — and a cold start from 0 V lands in the low one and stays.

- **It latches.** After `t_PRE` = `t_EOC`/8 ≈ **33 min** (`C104` = 1 µF) the LT3652 declares **bad battery**, stops charging and pulls `FAULT` low. Unplug ~10 s to clear it before each attempt.
- **Confirm it in one measurement:** ~**15 mV** across `R18` (pin 1 → pin 2) = 150 mA = precondition. 100 mV would be full CC and a different fault.
- **Same trap in the field:** a deeply-discharged cell on USB. The 150 mA goes into the cell *and* the load draws from it — net negative, and the pack never recovers. `FIRMWARE.md` §12.0.12 fixed the **timer** half of this in review (`C104` 100 nF → 1 µF, so precondition is not *timed* out prematurely); this is the **current vs. load** half, and it is still open in hardware.
- **Raising `I_CHG` is not the fix.** 150 mA would have to exceed the idle load, putting `I_CHG` near **2.7 A** — over the LT3652's 2 A ceiling and far too much for a 3 Ah 1S cell (0.9 C).
- **Gate the load, not the charger.** Replace `U5`'s hard `EN`-to-VBAT tie with a supervisor (or a comparator off the existing `VBAT_SENSE` divider) that releases at **~3.0 V** and holds off below it. The numbers work: unloaded, 150 mA charges 220 µF from 0 to 3.0 V in **4.4 ms**, and it then releases into a node that can supply 450 mW against a 370 mW demand. Filed as **v0.4 V8** (`kicad/REVIEW.md`).
- **Until then, bring the board up with a cell in the holder** (or inject the BAT node from a current-limited bench supply at ~4.0 V). This inverts `FIRMWARE.md` §12's "prove the charger with no cell first" — the empty-holder path cannot pass steps 3–4.

**⚠ The two protector lockups — and the margin check that was never done (bench, 2026-09-13).** The HY2111 senses *both* discharge and charge current as the voltage across the AOSD32334C pair, on `CS`. Neither threshold was checked against this board's real currents, and **both are crossed in normal operation.**

| | threshold | trips at | board's actual current |
|---|---|---|---|
| **Discharge OC** (`V_DIP`, `-GB`) | 150 mV, `T_DIP` 10 ms | **1.89 A** worst case | TPS61023 startup — valley limit **3.7 A** |
| **Charge OC** (`V_CIP`) | **−100 mV typ, −60 mV worst case**, `T_CIP` 12 ms | **~1.15 A** at best | `I_CHG` = **1.0 A** (`R18` 0.1 Ω) |

- **Discharge-OC.** The 5 V boost's startup current trips it on every cell insertion. §11.4 releases only when the impedance across PB+/PB− exceeds **(150 mV / `V_DIP`) × 450 kΩ = 450 kΩ**, or when a charger is connected. A soldered-down system board is a permanent ~40 Ω, so the first path can never happen — and the second is blocked by the precondition lockout above. Board dead, with no way out but an external bridge from cell − to PACK−.
- **Charge-OC.** `I_CHG` × R_DS(pair) must stay under 60 mV. The AOSD32334C is ≤26 mΩ at **VGS = 4.5 V, its lowest characterised gate drive** — and here VGS *is* the cell voltage, which never exceeds 4.2 V, so the part always runs below that point. Even taking the optimistic 52 mΩ pair, 1 A gives 52 mV against a 60 mV worst-case threshold: **8 mV of margin**. Measured on build #1: charging a 3.4 V cell turned `OC` off inside `T_CIP` and the cell never took any charge at all. §11.5 releases only *"by removing the charger"*, so it re-trips on every replug.
- **Root cause, shared with the cold-start trap.** An ungated constant-power load, and **no margin arithmetic between the protector's sense thresholds and the board's real currents.** The topology — protector + FET pair in the cell − path, PACK− as system ground — is standard and correct. Only the numbers were never checked.
- **The design rule for any respin.** Both inequalities must hold with margin:
  - `I_CHG` × R_DS(pair, at the **lowest cell voltage you must charge from**) < **60 mV**
  - peak discharge × R_DS(pair) < **`V_DIP` − 25 mV**
  - With `V_DIP` = 200 mV (`-HB`), a 2.3 A alarm peak needs R_DS(pair) ≤ **76 mΩ**; `I_CHG` = 1 A needs ≤ **60 mΩ** *at 3.0 V VGS, not 4.5 V*. The **AO4838** (≤13 mΩ @ 4.5 V, pin-identical SOIC-8 drop-in) is the pick — `kicad/REVIEW.md` **V9**. Derated the way `REVIEW.md` #6 derates (gate drive is V_cell, so R_DS ≈ 1.27× the 4.5 V spec) it gives a **26–33 mΩ** pair: **33 mV** on charge (1.8× under `V_CIP`) and **122 mV** on the boost's 3.7 A startup. ⚠ **That last number clears `-HB`'s 175 mV floor by 1.4× but sits 3 mV under `-GB`'s 125 mV — no margin at all. V9 requires V10.** The same arithmetic reproduces both observed failures on the AOSD32334C (244 mV startup, 66 mV charge), which is why it is trusted here.

**⚠ `VBAT_SENSE` cannot see any of this.** The divider taps cell+ against **board GND**, not across the cell. With the charge FET open, cell − floats a full cell-voltage away from PACK− and the ADC reads the BAT node, not the cell — on build #1 it reported ~4.0 V for a cell actually sitting at 3.4 V. Firmware invariant, `FIRMWARE.md` R-BOARD-3.

**HY2111 + AOSD32334C (protector)** — no config (thresholds fixed by the part-number suffix → **HY2111-HB = OV 4.28 V (release 4.08 V) / OD 2.90 V / OC 200 mV**, SOT-23-6). Wire the **AOSD32334C dual N-FET** (charge + discharge FETs) in the cell − path between the 18650 and PACK−, gated by the HY2111 **OC/OD** pins; support network per its datasheet §10: **R1 100 Ω** cell+→VDD, **C1 0.1 µF** VDD–VSS, **R2 2 kΩ** CS→PACK− — all delays are internal. Independent of the charger — the redundant OV/OD/OC/SC cutoff. *(Replaced the NRND/obsolete **AP9101CK6-BX** 2026-07-17 — same pin arrangement (1 OD · 2 CS · 3 OC · 4 NC · 5 VDD · 6 VSS), nets unchanged. The exact-threshold quality twins — ABLIC S-8261ABMMD, Nisshinbo R5478N — are reel-only/3000 MOQ at DigiKey, so the HY2111-GB comes from **LCSC C82747** like the CH224K.)*
> ⚠ **As built, board #1 (2026-08-10): `-GB`, not `-HB`.** The -HB is LCSC **C160793** and was out of stock when PCBWay quoted the assembly, so the **150 mV** OC suffix went on the board instead of the 200 mV one. Everything else (OV 4.28 / OD 2.90 V, pinout, support network) is identical. The cost is a **1.89 A** worst-case discharge trip instead of 2.65 A, which a loud plugged sunrise alarm exceeds — held in check by `FIRMWARE.md` **R-AUDIO-1**'s `-GB` budget until a -HB is fitted. **Verify the marking on the assembled board.** (`kicad/REVIEW.md` #6)

**Reverse P-FET** — P-ch MOSFET (e.g. AO3401A / DMP3013), source = holder +, drain = system +, gate → GND via resistor (+ small zener clamp). Correct polarity → on; reversed cell → blocked.

**Cell gauging (ESP32 ADC)** — no gauge IC (none is hand-solderable). A 100 k/100 k divider off the cell (+ 100 nF, and a **2N7002 in the bottom leg — gate `VBAT_DIV_EN` on the IO expander — to disconnect it in deep-sleep**) feeds an **ADC pin** (IO1 `VBAT_SENSE`, ADC1); firmware maps voltage → SoC% for UI + **low-battery shutdown (~3.2 V / ~10 %)**. The **80 % health cap is enforced in hardware** by the LT3652 float divider, not firmware.
Two hardening additions (2026-07-21): **D13 (BAT42W, SOD-123)** clamps the divider node to ~−0.2 V — a **reversed cell** would otherwise pull the ESP32 pad to ~−2 V through the 100 k; and **`CELL_TEST` (expander GPB7 → Q8 2N7002 → Q9 AO3401A)** briefly lifts reverse-FET Q2's gate to holder+, opening its channel so firmware can tell a **full cell** (reading holds) from an **empty holder** (reading steps down one Q2-body-diode, ~0.3–0.4 V — without this, the charger back-feeds the empty holder to float voltage and both cases read ~4.05 V). Plugged-only; both FETs default off (100 k pulldown/pull-up) so normal operation is untouched at POR.

**Bring-up sequence:** plug → CH224K requests 15 V → LT3652 charges at its resistor-set defaults → MCU boots from the BAT node → reads Vbat (ADC) + CHRG/FAULT → normal run; with `PD_PG` asserted, firmware may enable the 12 V boost (audio at full 12 V + wake sunrise). **Unplugged:** BAT node from cell (through the protector FETs); the **12 V boost stays off** → wake light disabled, and the amp PVDD auto-drops to the **5 V rail** (LTC4412 mux) for a **quieter but audible alarm**. NeoPixels (5 V status + dial light) and the clock run normally on battery.

## BOM — power + safety (verified active on DigiKey unless noted)
| Function | Part | Pkg (hand-solder) | ~$ | Ref |
|---|---|---|---|---|
| PD sink | **CH224K** | ESSOP-10 | ~$0.4 | LCSC C970725 *(not DK)* |
| Charger (1S buck, BAT-node path) | **LT3652EMSE#PBF** | MSOP-12E | ~$9.9 | DK 2225686 ✅ |
| Fuel gauge | *(none — ESP32 ADC divider)* | — | ~$0 | — |
| Cell protector | **HY2111-HB** + **AO4838** dual-N FET *(was `-GB` + AOSD32334C as built; 26 mΩ was the root of two lockups — v0.4 **V9 + V10, which must land together**)* | SOT-23-6 + SO-8 | ~$2.1 | ✅ (HYCON via LCSC / [AO4838 DK 3152401](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO4838/3152401)) |
| Reverse-polarity | P-FET AO3401A / DMP3013 | SOT-23 | ~$0.2 | ✅ |
| Cell temp | 10 k NTC (Murata NCP18XH103) | 0603 | ~$0.1 | ✅ |
| **One-shot TCO (~77 °C)** | thermal fuse in cell − path (e.g. SEFUSE SF/Bourns bimetal ~77 °C) | radial/tab | ~$0.4 | ⚠ pick + file datasheet |
| Transient | TVS SMAJ22CA (VBUS) + SMAJ5.0CA (BAT) — **bidirectional** since 2026-07-21 (schematic uses the bidirectional symbol; CA parts remove the assembly-orientation risk) | SMA | ~$0.4 | ✅ |
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
