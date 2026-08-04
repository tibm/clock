# Design review — `clock.kicad_sch` / `clock.kicad_pcb`

**Date:** 2026-08-02 · **Reviewed at commit:** `7ec2534` ·
`clock.kicad_sch` md5 `923072dddf48824cfdb7de3341dcab70` ·
`clock.kicad_pcb` md5 `8620b590d079f6193cd99e7b7b5c9549`

**Scope:** functional/electrical correctness of the main-board schematic and PCB,
cross-checked against the datasheets in `datasheet/`. Mechanical/3D fit, the custom
footprints vs. vendor drawings, silkscreen legibility, and the sensor board's own
internals are **not** covered.

## Verification baseline

```
kicad-cli sch erc --severity-all clock.kicad_sch   → 0 violations
kicad-cli pcb drc --severity-all clock.kicad_pcb   → 0 violations, 0 unconnected
```

Everything below is what those tools structurally cannot see. Findings were derived
from the exported netlist, a parse of the `.kicad_pcb` (pads / tracks / vias / zone
fills), and the vendor datasheets.

## ⚠ Workflow note before touching anything

`gen/pcb_build.py` calls `pcbnew.CreateEmptyBoard()` and overwrites
`clock.kicad_pcb`. The board now contains **1884 track segments, 555 vias and four
filled GND zones** that the generator never produced (added by the autorouter +
manual passes, commits `91c5087` … `129a757`). **Re-running `pcb_build.py` destroys
all routing.**

- **Schematic** — still generator-driven: edit `gen/b_*.py`, then
  **`build.py` followed by `stamp_bom.py`**. `build.py` alone drops the 684
  MPN/Manufacturer/Package/Notes properties that `stamp_bom.py` writes; the BOM
  fields come back only when it is re-run (its own docstring says so).
- **PCB** — now hand-owned: edit in pcbnew, propagate schematic changes with
  *Tools → Update PCB from Schematic* (F8). `pcb_build.py` is a one-shot placement
  tool that has been superseded; `PCB_NOTES.md` ("no traces are routed") is stale.

---

## Progress

- **2026-08-03 — Phase 0** (branch `review-fixes`): destructive-run guard added to
  `gen/pcb_build.py`; `gen/review_check.py` added as a standing regression check
  (`python3 review_check.py`).
- **2026-08-03 — Phase 1**: findings **#1, #2, #3 fixed in the schematic**, ERC 0,
  lint clean, netlist diff vs. the pre-fix schematic is exactly the 6 intended node
  moves and nothing else. **PCB side still pending** — needs *Update PCB from
  Schematic* (F8) plus a re-route of the affected traces at Q4, U9 and C181/C182.
- **2026-08-03 — Phase 1b** (cheap schematic follow-ons): **#11, #12, #15, #17
  fixed**. #11 lands J7.6 on the expander's free GPB3 as `ALS_INT`; #15 adds **D14**
  (same BAT42W/SOD-123 as D13, so no new BOM line) clamping the ADC node to +3V3;
  #17 drops the encoder divider to 10 k/20 k (same 1:2 ratio, 6.7 kΩ instead of
  66.7 kΩ). ERC 0; netlist diff is exactly these changes. **D14 still has to be
  placed and routed on the PCB.**
  - **#13 deliberately left out** of the batch: fixing the 137 mW in `R1`'s 100 mW
    0603 needs either a 1206 land (0.25 W, and R1 has 4.6 mm of clear space) or a
    high-power 0603 part — both need an MPN sourced and verified on DigiKey, which
    is your call, not a mechanical edit. `parts_db` keys resistor MPNs by *value
    only*, so it also needs a `_REF["R1"]` override either way.
- **2026-08-03 — Phase 2 prep**: `C181`/`C182` swapped positions in
  `cosmetics.py` so each bootstrap cap sits on the column of the leg it belongs
  to. This keeps **both caps on their original U9 pins** (C181-U9.25 BSTRPA-,
  C182-U9.19 BSTRPB+), which the existing PCB copper already implements — cutting
  the PCB rework for #2 from *two long crossing pin-side routes* down to
  **two short straight far-pad runs**. Schematic is now final for #1/#2/#3.

  ### Phase 2 work order (PCB) — measured, not estimated

  The U9 fan-out is genuinely full: in the 5.7 × 8.0 mm box around it,
  **B.Cu 47.5 mm / 8 nets, In1 60.3 mm / 5 nets, In2 24.7 mm / 3 nets, 8 vias,
  8 pads — and F.Cu completely empty (0 mm)**. F.Cu is the escape layer.

  | # | connection | from | to | note |
  |---|---|---|---|---|
  | 2 | `U9.20` → leg B | (100.15, 75.63) | leg B at (101.55, 77.58) | delete segs at pad; PVDD via at (101.56, 76.81) blocks the direct diagonal |
  | 2 | `U9.26` → leg A | (100.15, 79.53) | leg A via at (101.36, 81.05) | PVDD knot at (101.16–101.58, 80.08–80.52) blocks B.Cu |
  | 2 | `C181.2` → leg A | (103.50, 81.83) | `C180.2` (103.50, 78.18) | straight vertical, x = 103.50 |
  | 2 | `C182.2` → leg B | (106.00, 78.18) | `C183.2` (106.00, 81.83) | straight vertical, x = 106.00 |
  | 1 | `Q4.3` → `+5V` | (100.92, 71.10) | +5V at (100.91, 68.26) | **must not** go straight up: pads 1/2 leave a 0.43 mm gap and a 0.25 mm track needs 0.45 mm. Hop on F.Cu with vias at (100.91, 68.26) and (100.92, 70.16) — both verified clear of all three Q4 pads |
  | 1 | `Q4.2` → `PVDD` | (101.88, 69.22) | PVDD at (103.74, 71.10) | direct B.Cu diagonal, clear |
  | 15 | `D14` | — | `VBAT_SENSE` + `+3V3` | new SOD-123 to place near D13 (99.09, 104.30) |
  | 11 | `ALS_INT` | `J7.6` | `U13.4` | new net, both parts already placed |

  Segments to delete first (they sit on pads whose net changed): `Q4` 281, 934;
  `U9` 539, 540, 541, 543, 1743, 1744, 1745, 1747.

- Fixing #1 surfaced a latent generator bug: `sch2.py::_xf()` mirrored **before**
  rotating while KiCad mirrors **after**, which silently swaps the two mirror axes
  at rot 90/270. Harmless until now (BT1 was the only mirrored part, at rot 0);
  fixed, and verified not to move any other net.

## Status tracker

| # | Finding | Sev | Fix location | Done |
|---|---|---|---|---|
| 1 | Q4 PVDD-mux P-FET source/drain reversed | 🔴 | sch + pcb | sch ☑ · pcb ☐ |
| 2 | TAS5760M PBTL outputs paralleled wrong pairs | 🔴 | sch + pcb | sch ☑ · pcb ☐ |
| 3 | LT3652 `C104` 100 nF → 26 min charge timeout | 🔴 | value only | ☑ (1 µF, same 0603 land) |
| 4 | All tracks 0.25 mm — no power net class | 🟠 | pcb | ☐ |
| 5 | No thermal vias in U7 / U9 / U2 exposed pads | 🟠 | pcb | ☐ |
| 6 | Protector OC trips below peak battery current | 🟠 | BOM or firmware | ☐ |
| 7 | Battery IR drop starves the 12 V boost | 🟠 | pcb + BOM | ☐ |
| 8 | Switcher hot loops 5–28 mm | 🟠 | pcb | ☐ |
| 9 | `C238` is 50 mm from U15 | 🟠 | pcb | ☐ |
| 10 | J7/J10 identical connectors, incompatible pinouts | 🟡 | sch + pcb | ☐ |
| 11 | J7 pin 6 mismatch vs. sensor board (`ALS_INT`) | 🟡 | sch + pcb | sch ☑ · pcb ☐ |
| 12 | J7 value string says LIS3DH, board has BNO085 | 🟡 | doc | ☑ |
| 13 | `R1` (CH224K VDD) 137 mW in a 0603 | 🟡 | BOM/footprint | ☐ |
| 14 | Ungated always-on loads (~70 mA idle) | 🟡 | sch + pcb | ☐ |
| 15 | `VBAT_SENSE` floats above +3V3 when divider off | 🟡 | sch | sch ☑ (D14) · pcb ☐ |
| 16 | `CELL_TEST` on battery = power cut / boot loop | 🟡 | sch or firmware | ☐ |
| 17 | Encoder A/B divider 66.7 kΩ source impedance | 🟡 | value only | ☑ (10k/20k) |
| 18 | ~3 m of signal routing on the inner GND planes | 🟡 | pcb | ☐ |
| 19 | PVDD bulk marginal (100 µF, 300 mA ripple) | 🟡 | BOM | ☐ |
| 20 | Reverse-cell protection leans on protector FETs | 🟡 | note | ☐ |
| 21 | MCP23017 INTA/INTB tied — needs `IOCON.MIRROR` | 🟡 | firmware | ☐ |
| 22 | No UART console (IO43/44 consumed) | 🟡 | note | ☐ |
| 23 | U9 exposed-pad land needs fab cross-check | 🟡 | verify | ☐ |
| 24 | 32.768 kHz load caps 5.9 mm from Y1 | 🟡 | pcb | ☐ |

---

# 🔴 Stop — fix before fab

## 1. Q4 (PVDD mux P-FET) is source/drain reversed → 12 V back-feeds the 5 V rail

**Evidence.** `Q4` pad 2 (**S**) = `+5V`, pad 3 (**D**) = `PVDD` — verified in both
`.kicad_sch` and `.kicad_pcb`. AO3401A SOT-23 is 1 = G, 2 = S, 3 = D
(`datasheet/reverse_pfet_ao3401a.pdf`, top-view figure).

**Symptom.** A P-FET's body diode conducts **drain → source**. With the drain on
PVDD, the moment `BOOST12_EN` goes high the diode is forward-biased with ~7 V across
it and dumps 12 V into `+5V`. LTC4412 datasheet, *Operation*:

> "Note that the external MOSFET is wired so that the drain to source diode will
> momentarily forward bias when power is first applied to VIN and will become
> **reverse biased when an auxiliary supply is applied**."

Correct wiring is **drain → +5V, source → PVDD**.

**Blast radius** — abs-max of everything on `+5V`:

| part | V(abs max) | at ~11.4 V |
|---|---|---|
| U6 TLV62569 (VIN/EN) | 6 V | destroyed |
| U5 TPS61023 (VIN/VOUT/SW) | 7 V | destroyed |
| U15 SN74AHCT1G125 | 7 V | destroyed |
| EM14 encoder (VCC) | 5.25 V | destroyed |
| D40/D41 SK6812 | ~6 V | destroyed |
| U11/U12 TB6612 (VM) | 15 V | survives |

**Fix.** Swap the nets on Q4 pads 2 and 3 — i.e. in `gen/b_audio.py` the pin numbers
in the two `s.pw(Q4, …)` calls trade places, so pad **3** goes left to `+5V` and pad
**2** goes right to `PVDD`. Mirror the symbol (`rot=270` → `rot=90`) at the same time
so the two wires don't have to cross.

On the PCB this is two short traces at Q4 (100.92, 70.16) — rip up and re-route.
(A 180° footprint rotation does *not* achieve the swap on SOT-23: pads 1+2 share one
side and pad 3 is alone on the other.)

## 2. TAS5760M PBTL outputs are paralleled in the wrong pairs

**Evidence.** PCB pad nets:

```
U9.29 (OUTA+) -> Net-(U9-SPK_OUTA+)   U9.20 (OUTB+) -> Net-(U9-SPK_OUTA+)
U9.26 (OUTA-) -> Net-(U9-SPK_OUTA-)   U9.23 (OUTB-) -> Net-(U9-SPK_OUTA-)
```

TI SLOS772F **Figure 64** (*Mono PBTL using Software Control, 32-pin DAP*) and
**Figure 65** both tie **OUTA+ ∥ OUTA−** as one leg and **OUTB+ ∥ OUTB−** as the
other. The datasheet calls it *"pre-filter Parallel Bridge Tied Load"* — in PBTL the
two A half-bridges switch **in phase** as one leg, and the two B half-bridges as the
other.

**Symptom.** As wired, two **anti-phase** half-bridges are shorted together: a hard
PVDD → PGND path through 2 × 120 mΩ every switching cycle at 384/768 kHz. OCP will
latch a fault (`SPK_FAULT` low); the amp never produces audio, and the output stage
may not survive the first cycles.

**Fix — net swap only, no re-placement.** Swap pin 20 ↔ pin 26, and swap
C182 ↔ C181, giving:

```
node A = { U9.29, U9.26, C180 (BSTRPA+ p30), C181 (BSTRPA- p25) } -> L5
node B = { U9.20, U9.23, C182 (BSTRPB+ p19), C183 (BSTRPB- p24) } -> L6
```

**Firmware note.** In PBTL the amp takes its source from the **right** channel of
SDIN (or invert LRCK in the I²S master).

## 3. LT3652 `C104` = 100 nF → 26-minute charge timeout, 3.3-minute bad-battery timeout

**Evidence.** LT3652 datasheet: `tEOC(hr) = C_TIMER × 4.4e6`,
`tPRE = C_TIMER × 5.5e5`, *"A 0.68 µF capacitor is typically used, which generates a
timer EOC at three hours, and a precondition limit time of 22.5 minutes."*

| C_TIMER | tEOC | tPRE (bad-battery) |
|---|---|---|
| 0.68 µF (datasheet nominal) | 3.0 h | 22.5 min |
| **0.1 µF (as built)** | **0.44 h = 26 min** | **3.3 min** |

**Symptom.** A 3000 mAh cell needs ~3 h at 1 A, so normal cycles terminate early and
re-trigger repeatedly. Worse: a deeply-discharged cell cannot clear the 2.84 V
precondition threshold at 150 mA in 3.3 min → **latched "bad battery" fault,
charging refused.**

`power_values.md:19` already carries `⚠️ verify vs. TIMER eq.` — this is the result.

**Fix.** `C104 = 0.68 µF` (0603 X7R, ≥16 V). Value change only, same land.

---

# 🟠 Will bite you

## 4. Every trace on the board is 0.25 mm — no power net class

`clock.kicad_pro` has a single `Default` net class (track 0.25 mm, via 0.6/0.3), and
the PCB contains **exactly one track width**. 0.25 mm × 35 µm ≈ **0.9 A** at +10 °C
rise (IPC-2221, external layer).

End-to-end resistances (Dijkstra over the actual routed copper):

| path | R | length | at design peak |
|---|---|---|---|
| `VBAT` cell+ → U7 VIN (12 V boost) | **178 mΩ** | 87.5 mm | 4 A → 0.71 V, 2.8 W in copper |
| `VBAT` cell+ → U5 VIN (5 V boost) | **322 mΩ** | 159.4 mm | |
| `VBAT` U2 BAT → cell+ | 122 mΩ | 58.9 mm | charge path |
| `PVDD` D30 → U9.21 / U9.28 | 61 / 46 mΩ | 28 / 23 mm | 3 A audio peak |
| `+12V` D20 → J9 | 136 mΩ | 68.6 mm | |
| `+5V` U5 → U11 VM | 172 mΩ | 86.2 mm | |
| `+5V` U5 → J12 (pixels) | 169 mΩ | 84.5 mm | |
| `VBUS` J1 → U2 VIN | 61 mΩ | 30.9 mm | ok |

**Fix.** Add a `POWER` net class (≥1.0 mm) covering `VBAT`, `+12V`, `PVDD`, `+5V`,
`VBUS`, and the amp outputs `Net-(U9-SPK_OUTA+/-)`; ≥3 vias per layer crossing on
`VBAT`. F.Cu is 98 % empty (85 mm routed total) — that's the budget.

## 5. No thermal vias in any exposed pad

| part | EP size | vias inside | dissipation |
|---|---|---|---|
| U7 TPS55340 (12 V boost) | 3.4 × 5.0 mm | **0** | ~2 W at 12 W out |
| U9 TAS5760M (amp) | 5.2 × 11 mm | **0** | ~1.5 W at 10 W out |
| U2 LT3652 (charger) | 1.65 × 2.85 mm | **0** | ~0.7 W at 1 A |
| U1 CH224K | 2.1 × 3.3 mm | 1 | low |
| U8 ESP32-S3 | — | 12 ✓ | — |

All sit on B.Cu so they touch the B.Cu pour, but the path to In1/In2/F.Cu is absent.
`CLAUDE.md` explicitly assumes *"PowerPAD (amp/charger) OK **with a thermal-via
array**"*. U7 is the one that will hit thermal shutdown first.

**Fix.** 0.3 mm via arrays (~1.2 mm pitch): 9–12 under U7, 12–15 under U9, 4–6 under
U2. Tented on B.Cu so hand-soldering paste doesn't wick through.

## 6. Battery protector over-current trips below the design's own peak load

HY2111-GB discharge OC threshold = **150 ± 25 mV** measured across the AOSD32334C
pair (CS is at PACK−, VSS at cell−, both FETs in between).

AOSD32334C R_DS(on) = 20 mΩ typ / 26 mΩ max at V_GS = 4.5 V; gate drive here is
V_cell (3.0–4.2 V), so ≈ 25–33 mΩ each → **50–66 mΩ series** → **trip at ~2.3–3.0 A**
(worst case ~1.9 A).

The stated target is *"~12 W ceiling from 1S input: wake LEDs + audio share it"*
⇒ ≈ 4 A from the cell. **A loud alarm on battery trips the protector**, the board
loses power, the load disappears, the protector releases → power-cycle loop.

**The OC delay does not save you.** HY2111 `T_DIP` = 5 / 10 / 15 ms (min/typ/max).
Any bass note held longer than ~10 ms at the trip current trips it, so a
high-crest-factor music asset lowers *average* draw but not the trip risk — only a
real peak limiter does.

**Operating-case split (added 2026-08-03).** This matters much less on battery than
plugged, because the wake LEDs are already firmware-gated to plugged-only and, with
the 12 V boost off, PVDD falls back to the **5 V** rail through Q4:

| case | PVDD | max sine, 4 Ω | peak I (cell) | vs. ~2.3–3.0 A trip |
|---|---|---|---|---|
| plugged, 12 W ceiling | 11.5 V | ~11 W | ~3.3 A | **trips** |
| battery, 4 Ω | ~5 V | ~3.1 W | ~1.9 A | marginal |
| battery, 8 Ω | ~5 V | ~1.6 W | ~1.0 A | comfortable |

**Fix — pick any combination:** cap the firmware battery budget so cell current stays
under ~1.9 A peak (worst-case trip at max R_DS(on) and min V_DIP); move to an 8 Ω
driver (halves audio peak current — also change `L5`/`L6` to 22 µH per TI's filter
table, same XAL40xx land); or swap to **HY2111-HB**, which is the same family with
identical OV 4.28 V / OD 2.90 V thresholds but `V_DIP` = 200 mV instead of 150 mV
(≈ +33 % trip) — a drop-in part change.

## 7. Battery IR drop starves the 12 V boost

Series path cell → boost input:

| element | R |
|---|---|
| Q2 AO3401A @ V_GS ≈ 3.6 V | ~70 mΩ |
| AOSD32334C ×2 | ~50 mΩ |
| `VBAT` trace (finding 4) | 178 mΩ |
| holder contacts + TCO | ~30 mΩ |
| **total** | **≈ 0.33 Ω** |

At 3.5 A that is **1.15 V**, so a 3.6 V cell presents ~2.45 V at U7 — below the
TPS55340's **2.9 V minimum V_IN**. Also Q2 (SOT-23, θJA 100–125 °C/W steady-state)
dissipates 0.86 W at 3.5 A → T_j 120–140 °C with R_DS(on) rising as it heats.

**Corollary — the PD contract is not the ceiling; `R18` is.** `VBAT` is fed only by
U2's BAT pin and by the cell through Q2, and the LT3652 hard-limits its BAT current
to 100 mV / `R18` = **1.0 A**. So however much the PD source offers (15 V × 3 A =
45 W at the connector), the wall can contribute only ≈ 1 A × 4.05 V ≈ **4 W** to the
system — everything above that comes out of the cell *even while plugged in*, and
with no cell installed the system is capped at ~4 W outright. *"Runs with no cell on
USB"* holds at idle only.

Cheapest improvement: **`R18` = 0.05 Ω → 2.0 A**, the LT3652's stated maximum
("*This resistor can be set to program maximum charge current as high as 2A*"),
doubling the wall contribution to ≈ 8 W. `R18` is a 2010 1 W part and only sees
0.2 W at 2 A, but U2's own dissipation roughly doubles to ~1.4 W — which makes
finding #5 (no thermal vias under U2) a prerequisite, not an option. Going beyond
that needs a separate VBUS → rail converter, i.e. a topology change.

**Fix.** Widen `VBAT` (finding 4), and either accept the reduced battery power
ceiling (finding 6) or move Q2 to a larger/lower-R_DS(on) P-FET.

## 8. Switching-converter hot loops are 5–28 mm

| loop | measured |
|---|---|
| LT3652: C100 (10 µF Cin) → VIN pin | **16.2 mm** (C102 100 nF: 9.0 mm) |
| LT3652: D11 anode (GND) → IC GND EP | **14.0 mm** |
| TPS55340: D20 cathode → C130/131/132 | **12.1 / 14.4 / 17.2 mm** |
| TPS55340: C129 (100 µF Cin) → VIN | **27.7 mm** (C128 100 nF: 10.0 mm) |
| TAS5760M: C160 (GVDD_REG 1 µF) → pin 32 | **13.8 mm** |
| TAS5760M: C172 (PVDD bulk) → pin 28 | **17.4 mm** |
| TAS5760M: DVDD / AVDD 100 nF | 10.2 / 11.7 mm |
| TLV62569: C124 (Cin) → VIN | 5.1 mm |
| TPS61023: C120 (Cin) → VIN | 7.6 mm |
| ESP32: C140/C141 bulk → 3V3 pin | 8.7 / 8.8 mm |

The buck's input loop (Cin → VIN → SW → catch diode → GND) and the boost's rectifier
loop (SW → D20 → Cout → PGND) are the two highest-di/dt loops on the board and both
are wide open. Expect SW ringing (LT3652 SW abs max 40 V), radiated EMI, and audible
noise in the amp.

**Fix priority:** LT3652 Cin + D11 ground return, then TPS55340 Cout, then the
TAS5760M GVDD_REG / PVDD caps.

## 9. `C238` — the level shifter's bypass — is 50 mm from U15

U15 (SN74AHCT1G125) at (28.61, 48.88); C238 at (76.50, 39.50) → **50.5 mm**.
The buffer driving the whole NeoPixel chain has no local decoupling.

It slipped through because `qa_locality()` only checks non-rail nets, and C238's two
nets are `+5V` and `GND`.

**Fix.** Move C238 adjacent to U15 pin 5. Also consider extending the locality QA to
2-pad rail-only parts.

---

# 🟡 Worth a pass

**10. J7 / J10 mis-mate hazard.** Both JST ZH 1×06 vertical, same pre-crimped
A06ZR cable, **13.5 mm apart** on the PCB, incompatible pinouts. J10 pin 2 = `+5V`,
J7 pin 2 = `+3V3` → plugging the sensor board into J10 destroys the
BNO085 / BME688 / TSL2591. Key them (different series, or make one a 1×05).

**11. J7 pin 6 mismatch.** The sensor board drives `ALS_INT` (TSL25911 INT, pulled
up by its R12) on J1.6; the main board leaves J7.6 `NC` ("spare wire in the cable").
Harmless electrically, but the light-sensor interrupt is unusable.

**12. J7 value string.** Reads `Sensor board (BME688+TSL2591+LIS3DH)`; the sensor
board actually carries a **BNO085**. Fix the string in `gen/b_io.py`.

**13. `R1` (CH224K VDD feed) is overloaded.** 1 kΩ 0603 from the **15 V** contract
into a shunt-regulated 3.3 V pin ⇒ (15 − 3.3)² / 1 k = **137 mW** in a 100 mW part
(CH224K VDD abs max 3.6 V, internal shunt sinks up to 30 mA). WCH's reference value
is drawn generically. Use an 0805/1206, or 2.2 k (still ≥ 5 mA at 15 V).

**14. Ungated always-on loads.** EM14 encoder **26 mA max** on `+5V`, QRE1113 homing
LED **14 mA** on `+3V3` (R98 = 150 R straight from the rail), plus ~5–7 mA of SK6812
idle — none switchable. ≈ 70 mA from the cell at idle → ~34 h on 2400 mAh usable.
Spare expander pins (GPA4-6, GPB3) exist to gate both through a small FET.

**15. `VBAT_SENSE` floats above the 3.3 V rail** when `VBAT_DIV_EN` is low: R23's
bottom is opened, so R22 (100 k) pulls IO1 to ~4 V through the ESP32's upper ESD
clamp (~3 µA — harmless, but outside abs-max). Clamp to `+3V3`, or switch the top leg
instead of the bottom.

**16. `CELL_TEST` on battery = hard power cut.** Q2's body diode is oriented
VBAT → cell+, so it cannot back-feed. Asserting CELL_TEST unplugged kills the rails,
which drops the MCP23017, which releases CELL_TEST → Q2 back on → boot loop. The
comment in `gen/b_charger.py` ("on battery Q2's body diode keeps the system alive but
drops ~0.4 V") is **wrong**. Consider gating CELL_TEST with `PD_PG` in hardware.

**17. Encoder A/B divider is high-impedance.** 100 k / 200 k ⇒ **66.7 kΩ** source
running ~40 mm past two stepper drivers and a class-D amp into PCNT. 10 k / 20 k
costs 0.25 mA and is 10× stiffer. Levels are fine either way: EM14 V_OH ≥ 4.0 V →
2.67 V worst case vs. S3 V_IH 2.48 V — but only 0.2 V of margin.

**18. The inner "solid GND planes" carry ~3 m of signal routing.**

| layer | routed | distinct non-GND nets | zone islands |
|---|---|---|---|
| F.Cu | 85 mm | 6 | 1 |
| In1.Cu | 1418 mm | 37 | 3 |
| In2.Cu | 1560 mm | 48 | 2 |
| B.Cu | 2064 mm | 127 | 35 |

The pours stay electrically continuous, so this is a return-path-detour problem
rather than a split-plane problem — but it does undercut `PCB_NOTES.md` constraint
#16 for an RF + 2-stepper + class-D board. F.Cu is nearly empty; moving power trunks
there frees inner-layer channels.

**19. PVDD bulk is thin for a 10 W class-D.** `C172` = 100 µF/25 V Rubycon TZV,
**300 mA @ 100 kHz** ripple rating; TI's PBTL reference shows 470 µF. Add
2 × 22 µF/25 V 1210 right at the PVDD pins.

**20. Reverse-cell protection leans entirely on the protector FETs.** Q2's body
diode (drain = VBAT) forward-biases into a reversed cell; only the AOSD32334C being
off breaks the loop. During reverse insertion the HY2111 sees VDD ~3.7 V *below* VSS
through R20 = 100 Ω (~37 mA into its ESD structures). R20 = 100 Ω is HYCON's own
recommendation (§10: R1 100 Ω typ, 200 Ω max), so this is defence-in-depth to note,
not a value error.

**21. MCP23017 INTA/INTB are tied together.** Safe at POR (both deasserted) and safe
with `IOCON.MIRROR = 1`, which the schematic text calls out — but firmware must set
MIRROR (or ODR) **before** enabling any per-bank interrupt, else two push-pull
outputs fight.

**22. No UART console.** IO43/IO44 are consumed by MCLK and the expander INT; the
PROG header J2 is 3V3/GND/EN/IO0 only. All bring-up depends on USB-Serial-JTAG.
(Boot-log TX is still probeable on IO43.)

**23. U9 exposed-pad land needs a fab cross-check.** KiCad's
`HTSSOP-32-1EP_6.1x11mm_P0.65mm_EP5.2x11mm_Mask4.11x4.36mm` declares 5.2 × 11 mm
copper with a solder-mask-defined **4.11 × 4.36 mm** opening; TI's DAP-32 land drawing
is SMD with a (5.2) dimension. Since this is the amp's only heat path, verify against
the TI mechanical drawing. (Same open item as the Juken / Keystone / GCT footprints
already listed in `PCB_NOTES.md`.)

**24. 32.768 kHz crystal loading.** ABS07-32.768KHZ-T (CL 12.5 pF, ESR 70 kΩ max)
with 18 pF loads ⇒ CL_eff ≈ 12 pF ✓. Y1 is 2.9 / 3.2 mm from the XTAL pins ✓, but
C145/C146 sit **5.9 mm** from Y1 and the ESR is right at Espressif's ceiling. Keep
the loop tight if the area is ever re-laid.

---

# ✅ Checked and found correct

- **CH224K** — 56 k on CFG1 = 15 V PDO; CFG2/CFG3 floating (matches WCH fig. 6.1
  single-resistor mode); DP–DM shorted at the chip = WCH's documented PD-only
  configuration (§5.5); 10 k series into the VBUS sense pin; integrated Rd, so no
  external 5.1 k needed.
- **LT3652** — float divider R14/R15 → 4.047 V, with FULLCHG_EN → 4.201 V;
  `R18` 0.1 Ω = 1.0 A; SENSE/BAT correctly straddle R_SENSE; D10 bootstrap polarity;
  D11 catch-diode polarity; NTC 10 k + 909 Ω = 0/45 °C window (the datasheet's exact
  recommendation); VIN_REG 316 k/100 k = 11.2 V foldback; SHDN → VBUS within abs max
  (VIN + 0.5 V), and the 7.5 V min start voltage means no charging at 5 V anyway.
- **FB dividers** — TPS61023 4.99 V, TLV62569 3.32 V, TPS55340 11.87 V.
- **HY2111** — R1 = 100 Ω, R2 = 2 k, C1 = 100 nF: HYCON §10 exactly.
- **TAS5760M** — software control mode via GAIN[1:0] pulled high through R60 (TI
  recommends the series pull-up for slew control); SFT_CLIP → GVDD_REG disables the
  soft clipper; ADR → GND = 0x6C; every regulator bypass present (GVDD_REG, ANA_REG,
  ANA_REF, VCOM, AVDD, DVDD); SPK_SD held low at POR by R63; SPK_FAULT pulled up.
- **SK6812** — symbol / footprint / datasheet mapping is right. KiCad's footprint is
  drawn 180° from the Opsco view, so pad 1 lands on the chamfered VSS corner and the
  `LED:SK6812` symbol (1 = VSS, 2 = DIN, 3 = VDD, 4 = DOUT) is consistent.
- **ESP32-S3** — module pin map correct; IO35-37 left NC for the octal PSRAM;
  IO45 held low through 10.1 k (VDD_SPI = 3.3 V flash) with the internal strap pull-down
  backing it; IO0 pulled up; IO46 is `I/O/T` (output-capable) and boot mode only needs
  IO0 = 1; EN RC 10 k + 1 µF; USB D± straight to IO19/20 with no series parts.
- **Antenna keepout honoured on all four layers** — the footprint's keepout zone
  (tracks/vias/pads/copperpour, all layers) is respected; fill starts at y = 8.0,
  clear of the antenna at y = 1.25–7.5. U8's own EP has its 12-via array.
- **TB6612** — internal 200 kΩ pull-downs on IN1/IN2/PWM/STBY make the un-pulled
  `STEP_STBY` POR-safe; PWM-on-IN retains brake/coast/CW/CCW; VM 5 V and Vcc 3.3 V
  both in range.
- **POR defaults** — pull-downs present on FULLCHG_EN, VBAT_DIV_EN, CELL_TEST,
  BOOST12_EN, SPK_SD and both wake-LED gates.
- **I²C** — one 4.7 k pull-up pair on the main board, 10 k on the sensor board →
  3.2 kΩ effective, t_r ≈ 271 ns (inside Fast Mode); no address collisions
  (0x20 / 0x6C / 0x76 / 0x29 / 0x4A).
- **BOM sanity** — BAT46W-E3-08 really is SOD-123; XGL5050 on the XAL5050 land
  (already verified in `parts_db.py`); TCO rated 250 V/15 A; all four J1 VBUS pads and
  all GND pads netted; cap voltage ratings correct (25 V on the 12 V rail, 50 V on the
  audio LC, 16 V on the 5 V/3.3 V rails).
