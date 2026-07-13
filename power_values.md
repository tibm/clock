# Power & Passive Values (schematic-ready)

Component values derived from the datasheets in [`/datasheet`](datasheet/) for **this design's** rails.
Everything here is computed for our operating points (VBUS 15 V, 1S Li-ion, the rail set in
[`power.md`](power.md)). ⚠️ = verify/validate on bench or against the noted datasheet section.

---

## 1. LT3652 — 1S buck charger (VIN = VBUS 15 V, float 4.05 V, I_CHG 1.0 A)
Datasheet `charger_lt3652.pdf`. V_FB reference = **3.3 V**. Float set by a divider BAT→V_FB→GND.

| Item | Value | Basis |
|---|---|---|
| **R_FB1** (BAT→V_FB, top) | **45.3 k** (1 %) | V_BAT = 3.3·(1+R_FB1/R_FB2); ratio 0.2265 → 4.047 V |
| **R_FB2** (V_FB→GND, bottom) | **200 k** (1 %) | divider drain ≈ 16.5 µA (always-on, even on battery) |
| **C_FF** feedforward | **22 pF** C0G, BAT→V_FB | datasheet: 20–50 pF to reject V_FB stray-C noise |
| **Full-charge FET** (Decision #2) | **976 k** (1 %) in ∥ with R_FB2, switched by a **2N7002** NMOS | ON → 4.20 V "full"; OFF → 4.05 V health cap. Gate = `FULLCHG_EN` (expander) |
| **R_SENSE** (SENSE–BAT) | **0.1 Ω**, ≥0.25 W (2010/2512) | R_SENSE = 0.1 V / I_CHG(max) → I_CHG = 1.0 A (~0.3 C, cool, safe for any cell) |
| **C_TIMER** (TIMER→GND) | **0.1 µF** | ≈3 h EOC; precondition timeout = t_EOC/8 ⚠️ verify vs. TIMER eq. |
| **Inductor** (SW) | **10 µH**, I_sat ≥ 2 A, low DCR | L=(10·R_SENSE/ΔI)·V_FLT·[1−V_FLT/V_IN], ΔI=0.3 → 9.9 µH |
| **Rectifier** (SW→GND) | **Schottky ≥1 A / ≥30 V** (B340A, 40 V/3 A) | I_D > I_CHG·(V_IN−V_PRE)/V_IN = 0.81 A; V_R > V_IN |
| **C_BOOST** (BOOST→SW) | **1 µF** X7R | + refresh Schottky BAT→BOOST (BAT46); no zener (float < 8.4 V) |
| **C_VIN** | **2×10 µF / 25 V** X7R low-ESR | RMS ripple = I_CHG/2 = 0.5 A worst case; TVS SMAJ22A already on VBUS |
| **C_BAT** | **10 µF/10 V X7R + 100 µF/10 V polymer** | 100 µF bulk **required** because we run system load with no cell |
| **VIN_REG UVLO** | R_IN1 = **316 k**, R_IN2 = **100 k** | V_IN(min) = 2.7·(R_IN1/R_IN2 + 1) ≈ 11.2 V → charger backs off if the 15 V contract sags. *(Alt: tie VIN_REG→VIN to disable.)* |
| **NTC** | NCP18XH103 (10 k, B25/50 = 3380) NTC→GND + **bias R ⚠️** | 0/45 °C window per LT3652 NTC section — compute bias R against the threshold table |

> ⚠️ **Constraint:** the LT3652 needs **V_IN ≥ V_BAT + 3.3 ≈ 7.35 V** to charge. So charging (and
> **no-cell operation**) works **only on the 15 V PD contract**. On the 5 V fallback the charger
> idles — the system then requires a cell. Document on silkscreen/firmware.

---

## 2. CH224K — USB-PD sink (request 15 V)
Datasheet `pd_sink_ch224k.pdf` §5.2.1 / §6.1. Single-resistor mode.

| Item | Value | Basis |
|---|---|---|
| **CFG1** (→GND) | **56 kΩ** | table: 6.8 k=9 V · 24 k=12 V · **56 k=15 V** · NC=20 V |
| **CFG2, CFG3** | **NC** (floating) | single-resistor mode |
| **VDD** | **1 kΩ** from VBUS + **1 µF** VDD→GND | datasheet ref schematic |
| **VBUS sense pin** | **10 kΩ** series to VBUS rail | — |
| **PG** (open-drain) | **10 kΩ** pull-up to 3V3 → `PD_PG` (expander) | active-low power-good |
| **CC1 / CC2** | to Type-C CC1 / CC2 | CH224K provides the PD/CC |
| **DP / DM** | **short together at CH224K**; route Type-C D± to **ESP32 IO19/20** | §5.5 PD-only mode → native USB free for the MCU |

---

## 3. TPS61023 — 5 V boost (from BAT, always-on)
Datasheet `boost_5v_tps61023.pdf`. V_REF (FB) ≈ **0.6 V**. Load ≈ 0.4–0.5 A (feeds 3V3 buck + stepper VM + panel).

| Item | Value |
|---|---|
| **R1** (VOUT→FB) | **732 k** (1 %) — datasheet 5 V value |
| **R2** (FB→GND) | **100 k** (1 %) → V_OUT = 4.99 V |
| **C_FF** (∥R1) | **220 pF** *optional* (only if loop rings; f_z ≈ 1 kHz) |
| **L1** | **1 µH**, I_sat ≥ 2 A, low DCR (Coilcraft XEL4030-102ME / Würth 74438357010) |
| **C_IN** | **10 µF / 10 V** X7R |
| **C_OUT** | **2 × 22 µF / 10 V** X7R |
| **EN** | tie to **BAT** (rail is always-on) |

---

## 4. TLV62569 — 3.3 V buck (from 5 V)
Datasheet `buck_3v3_tlv62569.pdf`. V_FB = **0.6 V**. V_OUT = 0.6·(1+R1/R2).

| Item | Value |
|---|---|
| **R1** (VOUT→FB) | **453 k** (1 %) |
| **R2** (FB→GND) | **100 k** (1 %) → V_OUT = 3.32 V |
| **C_FF** C3 (∥R1) | **6.8 pF** (datasheet rec for R2 = 100 k) |
| **L** | **2.2 µH**, I_sat ≥ 2.6 A (Table-4 "++" combo) |
| **C_IN** | **10 µF / 10 V** X7R |
| **C_OUT** | **22 µF / 10 V** X7R |

---

## 5. TPS55340 — 12 V boost (from BAT, **plugged-only**, audio PVDD + wake LEDs)
Datasheet `boost_12v_audio_tps55340.pdf`. V_REF = **1.229 V**. Datasheet worked a 12 V/1 A example — reuse it.

| Item | Value | Basis |
|---|---|---|
| **R1** (VOUT→FB) | **86.6 k** (1 %) | V_OUT = 1.229·(1+R1/R2) = 11.87 V |
| **R2** (FB→GND) | **10 k** (1 %) | — |
| **R4** (FREQ/RT) | **95.3 k** → **500 kHz** | datasheet Eq 1 |
| **C_SS** (soft-start) | **0.047 µF** | datasheet |
| **Comp: R3 / C4 / C5** | **2.55 k / 0.1 µF / 100 pF** ⚠️ | 6 kHz BW example; validate on bench |
| **Inductor** | **4.7 µH**, I_sat ≥ **6 A**, low DCR ⚠️ | boost from 1S: I_IN ≈ 4–4.7 A |
| **Diode** (SW→OUT) | **Schottky ≥3 A / ≥30 V** (B340A/B340B, 40 V/3 A) | peak ~5 A |
| **C_OUT** | **3 × 22 µF / 25 V** X7R (1210) | 60 mV ripple, 30 µF eff. after derating |
| **C_IN** | **10 µF/10 V X7R + 0.1 µF + 100 µF/10 V bulk** | high input current |
| **EN** | `BOOST12_EN` (expander) + **100 k pulldown** (off at boot) | true shutdown; **plugged-only** — firmware asserts only when `PD_PG` is live (off on battery) |

> ⚠️ **Constraint:** boosting a single cell (3.0–4.2 V) to 12 V at ~1 A draws **~4–4.7 A input**,
> near the TPS55340's 5.25 A FET limit at low battery → max ~0.9–1 A (≈10–12 W). Even plugged, the
> input is the BAT node (~4.05 V), so the **~12 W ceiling holds** and is now **shared by wake LEDs +
> audio** → firmware keeps their sum ≤ ~12 W (the amp's power foldback covers any shortfall). On
> battery the boost is **off** (plugged-only); the amp drops to the 5 V rail (§8 mux). Don't spec >12 W.

---

## 6. Small analog / support networks

| Block | Values |
|---|---|
| **32.768 kHz XTAL** (ABS07, C_L 12.5 pF) | 2 × **18 pF** C0G load caps (= 2·(C_L − C_stray), C_stray ≈ 3 pF); no series R; ESR ≤ 70 kΩ |
| **Vbat ADC divider** (+ disconnect FET, Decision #3) | R_top **100 k**, R_bot **100 k** (÷2: 4.2 V→2.1 V, ADC1 11 dB atten) + **100 nF** at node. **2N7002** in series with R_bot→GND, gate = `VBAT_DIV_EN` (expander) → off in deep-sleep (saves ~21 µA) |
| **QRE1113 homing** | IR-LED ballast **150 Ω** (V_F 1.2 V → I_F ≈ 14 mA @ 3V3); phototransistor collector→3V3 via **10 k**, output→ADC (IO2) + **100 nF**. Tune 10 k for distance/reflectivity |
| **Reverse P-FET** (AO3401A) | source=holder+, drain=system BAT+; gate→GND **100 k**; optional 8–10 V Zener gate-source clamp |
| **Protector** (AP9101C + AOSD32334C) | **R1** (batt+→VDD, supply stabilize) **330 Ω** + **C1** (VDD–VSS) **0.1 µF**; **R2** (VM→P−, charger/current sense) **2.7 kΩ** (datasheet Note 4: keep 0.3–4 kΩ, else CO can't fully turn off Q2); AOSD32334C dual-N in cell− path, gates from **DO** (discharge FET) / **CO** (charge FET). **No external delay caps** — AP9101C has a built-in fixed delay. Thresholds fixed by suffix **-BX = OV 4.28 V / OD 2.80 V** (2° fixed OV 8.0 V, VDD–VM). SOT26 pinout: DO1·VM2·CO3·NC4·VDD5·VSS6 |
| **I²C pull-ups** | main board **4.7 kΩ ×2** (SDA/SCL) @ 3V3. Sensor breakouts add 10 k each in ∥ — lift their jumpers if the bus gets too strong |
| **ESP32 EN** | **10 kΩ** PU + **1 µF** to GND (+ optional reset button) |
| **ESP32 straps** | IO0: **10 kΩ** PU (+ boot button); IO46: **10 kΩ** PD (ensure low at boot); IO3/IO45 use internal straps |

---

## 7. Decoupling plan (per IC)

| IC / rail | Decoupling |
|---|---|
| ESP32-S3 3V3 | **10 µF + 22 µF + 0.1 µF** (module bulk) + 0.1 µF per 3V3 pad |
| TAS5760M | PVDD **0.1 µF + 1 µF + 220 µF** bulk; DVDD/AVDD **0.1 µF + 10 µF**; bootstrap + output filter per datasheet |
| TB6612FNG ×2 | VM **0.1 µF + 10 µF**; VCC **0.1 µF** |
| MCP23017 | **0.1 µF** |
| Display (LS032) | VDD **1 µF** + VDDA **1 µF** + panel bypass per Sharp spec; **EXTMODE = GND** (software VCOM) |
| Each regulator | in/out caps per §1–5 + 0.1 µF at each VIN pin |

---

## 8. LED subsystem (two rails — see [`led.md`](led.md) + [`power.md`](power.md) §LED)
**3 channels**, all low-side switched by an **AO3400A** logic-level NMOS, gate driven from an MCU LEDC
PWM through **100 Ω** with a **10 kΩ gate→GND pulldown**, **~1 kHz**, gamma-corrected. No CC driver, no
level shifter. **Two rails:** wake = **12 V** (plugged-only, self-ballasted COB); panel = **5 V**
(always, discrete LEDs).

| Channel | Emitter (per `led.md`) | Rail | Dim pin |
|---|---|---|---|
| **Wake warm** | Inspired LED `12V-COB-3000K-12M` (warm 3000K COB) | **12 V** (plugged-only) | **IO45** |
| **Wake cool** | Inspired LED `12V-COB-4000K-12M` (neutral 4000K COB) | **12 V** (plugged-only) | **IO46** |
| **Panel** (dial + frontlight, shared) | Cree `CLM3C-MKW-CWAXB233` ×≤12 (warm 3200K, Vf 3.2 V) | **5 V** (always) | **IO7** |

- **Wake (12 V COB):** `MCU PWM → 100 Ω → AO3400A gate`; `10 kΩ` gate→GND; strip− to FET drain, strip+
  to **12 V**. Self-ballasted (integrated series R) → no external ballast. Keep combined wattage so
  **LED + audio ≤ ~12 W** (TPS55340 ceiling from 1S).
- **Panel (5 V discrete):** ≤12 LEDs in **parallel**, **one series R per LED**: R = (5 − 3.2 V)/I_F →
  **180 Ω** (0603) at ~10 mA ("faint"), or **90 Ω** at ~20 mA. Common cathode bus → **one AO3400A**
  (100 Ω gate + 10 kΩ pulldown, PWM IO7). ~0.6 W max on the 5 V rail (within the TPS61023 budget).

**SK6812 halo + TPS92200 dropped.** Panel = dial + frontlight on **one shared channel** (same intensity,
enabled together); merging them freed IO43 → UART log. MOSFET **AO3400A** (DigiKey) ×3.

### Amp PVDD rail-mux (12 V ↔ 5 V) — enables the battery alarm
The 12 V boost is **plugged-only**, so on battery the TAS5760M PVDD must fall back to the **5 V rail**
(~3 W, quieter alarm). Auto-select the **higher present** supply into PVDD:

| Item | Value | Basis |
|---|---|---|
| **Mux controller** | **LTC4412** (SOT-23-6) low-loss PowerPath + a small **P-FET** ⚠️ | priority to **12 V** (boost, plugged) via a series P-FET/Schottky; LTC4412 switches the **5 V** aux in when 12 V is absent. Reverse-blocks so 12 V can't back-feed the 5 V rail. |
| **PVDD bulk** | keep the §7 `0.1 + 1 + 220 µF` at the amp | mux output = PVDD node |

> A plain 2-diode OR is **rejected**: 5 V − V_Schottky ≈ 4.6 V ≈ the TAS5760M PVDD 4.5 V min (no
> margin). Use the low-loss FET mux. ⚠️ finalize the FET + LTC4412 CTRL/STAT network at schematic.

---

## 9. Speaker output filter (Decision #4 → **LC, recommended**)

| Option | Per BTL output | ~Cost | Trade |
|---|---|---|---|
| **Full LC** ✅ | **L 10–22 µH + C 0.68–1 µF** (f_c ≈ 30–40 kHz), ×2 legs | **+$1.0–1.5** + ~1 cm² | Clean, EMC-robust, tolerates longer leads; TI recommends it for PBTL |
| Ferrite-bead "filterless" | ferrite bead + small cap ×2 | +$0.10 | Cheapest/smallest, but the wood enclosure doesn't shield and 400 kHz on the speaker leads risks radiated-emissions failure — only OK with very short leads |

**Recommendation: full LC.** $1 of insurance for EMC + clean sound on a quality bedroom alarm.

---

## Still open (needs a decision, datasheet, or bench)
- **LED 12 V source — RESOLVED** (§8, 2026-07-12): no barrel jack; shared TPS55340 boost, plugged-only; amp PVDD auto-mux to 5 V on battery. **Bench items:** finalize the **LTC4412 + P-FET** mux network, and confirm final wake-COB wattage keeps **LED + audio ≤ ~12 W**.
- **TAS5760M** PBTL-mode / I²C-address (ADR) / analog-gain **straps** + output-filter LC exact values — pull from `amp_tas5760m.pdf` (next pass).
- **TB6612** PWMA/PWMB → tie to VCC (10 k or direct); coil→AO/BO order from the X40 pinout addendum.
- **LT3652 NTC bias resistor** and **TPS55340 comp network** — validate against datasheet threshold table / bench.
