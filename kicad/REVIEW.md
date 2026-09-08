# Design review — `clock.kicad_sch` / `clock.kicad_pcb`

**Date:** 2026-08-02 · **Reviewed at commit:** `7ec2534` ·
`clock.kicad_sch` md5 `923072dddf48824cfdb7de3341dcab70` ·
`clock.kicad_pcb` md5 `8620b590d079f6193cd99e7b7b5c9549`

**Scope:** functional/electrical correctness of the main-board schematic and PCB,
cross-checked against the datasheets in `datasheet/`. Mechanical/3D fit, the custom
footprints vs. vendor drawings, silkscreen legibility, and the sensor board's own
internals are **not** covered.

> **Pruned 2026-08-08.** The write-ups of findings that are **fully closed**
> have been deleted from this file — the fix is in the design files, and the
> reasoning that outlived it is in `PCB_NOTES.md`. Gone: **#1, #2, #3** (Q4,
> PBTL pairing, `C104`), **#5** (thermal vias), **#9** (`C238`), **#10–#13,
> #15, #17, #19–#21, #23**, and the Phase-2 PCB work order. The status tables
> below still list every one of them with its commit. What is kept in full is
> the work that is **still live** — #4, #6, #7, #8, #14, #16, #18, #22, #24,
> #25 — plus the "checked and found correct" record.

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

- **PCB sync** — `gen/sync_pcb.py` is the headless stand-in for *Update PCB from
  Schematic* (KiCad 10 exposes no netlist updater to Python and `kicad-cli pcb` has
  no such subcommand). It applies values, footprint swaps, new parts and pad nets,
  and cuts **only** the copper that would otherwise short two nets. It never adds
  copper. Idempotent — a second run reports 0 changes.
- **Schematic** — still generator-driven: edit `gen/b_*.py`, then
  **`build.py` followed by `stamp_bom.py`**. `build.py` alone drops the 684
  MPN/Manufacturer/Package/Notes properties that `stamp_bom.py` writes; the BOM
  fields come back only when it is re-run (its own docstring says so).
- **PCB** — now hand-owned: edit in pcbnew, propagate schematic changes with
  *Tools → Update PCB from Schematic* (F8). `pcb_build.py` is a one-shot placement
  tool that has been superseded; `PCB_NOTES.md` ("no traces are routed") is stale.

---

## 🔵 v0.4 — carried out of rev0.3 bring-up

Everything here was found by **building and running board #1**, not by review. Source: the
bench notes in [`../PCB_v0.3_learnings.md`](../PCB_v0.3_learnings.md) plus what the firmware
bring-up measured (`FIRMWARE.md` §12.0.3–§12.0.6). None of it is urgent — rev0.3 works — but
none of it should be rediscovered.

| # | Change | Cost | Why |
|---|---|---|---|
| V1 | **`M1` lower snap peg: (0, 9.62) → (0, 11.62)** — 2 mm further from the shaft | one footprint edit | **Required hand rework on build #1.** The two top pegs at (±8.49, −8.49) and the Ø4.6 shaft hole at (0, −6) are all correct; only the lone bottom peg is off. Edit `clock.pretty/Juken_X40-879_DualShaft.kicad_mod`, then re-verify against the factory STEP — the peg was derived from it, so a 2 mm error suggests the reference, not the transcription |
| V2 | **`R99` 10k → 22k** (QRE1113 collector pull-up) | one resistor | Doubles the homing signal *and* moves the clear end out of the ADC's clipped region. Measured on rev0.3 (R99 = 10k, 3.3 V rail): nothing 3159 mV\* / minute-hand distance 3010 / hour-hand distance 2600 / covered 2200 — i.e. 14, 29, 70, 110 µA of photocurrent. The weakest signal that matters is the **149 mV** minute-hand step. At 22k it becomes **~330 mV** with the clear end at ~2990 mV, in range. 33k gives ~490 mV but saturates on full cover. *\*3159 is clipped: 12 dB attenuation tops out near 3100* |
| V3 | **`J2` gains a +5 V pin** — bench power and flashing from one header | 1×04 → 1×05/06 header, one net | "Flashing is painful." A Mac cannot power this board at all (§12.0.3: VBUS reaches only the LT3652, which idles below 11.2 V), so bring-up means injecting 5 V at **`J12`** — the status-LED connector — while USB carries data. A labelled 5 V pin on the programming header replaces that hack. **Pin order is the actual design question:** 5 V next to +3V3 or to IO0 on an unkeyed 0.1″ header means one slipped position puts 5 V on a 3.3 V pin or an ESP32 GPIO. `GND · +5V · GND · +3V3 · EN · IO0` (1×06) is slip-tolerant; `+3V3 · GND · EN · IO0 · +5V` keeps the existing four positions but puts 5 V beside IO0, which is the worst neighbour |
| V4 | **Sensor harness: make reversal non-destructive** | 1×08 on both boards + re-route J7 | A reversed cable **actually arrived** for build #1 (2026-09-08) and was caught by a continuity check, not by the design. §4 below rejected a 1×07 twice on cost; the new evidence is that the hazard is real, not theoretical. The insight the earlier rounds missed: **the damage comes from power landing on a signal pin, not from signals being swapped.** A *palindromic* 1×08 — `GND · SDA · SENSOR_INT · +3V3 · +3V3 · ALS_INT · SCL · GND` — survives reversal outright: GND meets GND, +3V3 meets +3V3, and only SDA↔SCL and the two INTs swap. The failure mode becomes "scan finds nothing", which is safe, obvious and diagnosable, instead of a dead $13.57 BNO085 |
| V5 | **Gate the QRE1113 LED** from a spare expander pin (GPA4-6 are free) | one FET + one net | Buys two things with one change. Power: it is a permanent **14 mA** draw, which #14 already wants gated for backup runtime. Accuracy: with the LED switchable, firmware can read LED-on minus LED-off and **cancel ambient IR entirely** — worth having, because bench readings of the same "nothing in front" condition moved ~500 mV between two sessions under different room lighting. In the sealed cube ambient is close to zero, so this is an improvement rather than a fix |

**Not for v0.4, deliberately:** the I²C probe timeouts (§12.0.4) are a `i2c_master_probe`
artifact, not a board problem — 100 addressed reads of a known register came back exact with
zero timeouts. Nothing to change in hardware.

---

## Status overview

**Legend** — ☑ done · ⏳ open · ⏸ deferred (accepted risk, revisit later) · ✔ accepted (no change)

### ☑ Fixed

| # | Finding | Fixed in | Note |
|---|---|---|---|
| 1 | Q4 PVDD-mux P-FET source/drain reversed | `1f3736a` | schematic only — **PCB re-route still open** |
| 2 | TAS5760M PBTL outputs paralleled wrong pairs | `1f3736a`, refined `1cf1629` | schematic only — **PCB re-route still open** |
| 3 | LT3652 `C104` → 26 min charge timeout | `1f3736a` | 1 µF, same 0603 land, existing BOM line |
| 10 | J7/J10 identical connectors, incompatible pinouts | `637be57` | connector kept; **`SENSOR` + `KNOB` silkscreen added** on B.SilkS. **Re-opened and closed again 2026-08-08**: a 1×07 J7, which would have made the mis-mate physically impossible, was proposed in `kicad-sensor/REVIEW.md` and **rejected** — a footprint swap on both boards plus re-routing J7's fan-out here, for a connector plugged once at build time inside a sealed box. **The silk is the keying**; label both harnesses to match it |
| 11 | J7 pin 6 (`ALS_INT`) was NC | `727dbfd` | → expander GPB3; **PCB trace still open** |
| 12 | J7 value string said LIS3DH | `727dbfd` sch · `3cc15da` fw | schematic **and** `FIRMWARE.md` now say BNO085 — see note below |
| 13 | `R1` 137 mW in a 100 mW 0603 | `637be57` | → **1206**, `RC1206FR-071KL` (¼ W); **PCB land swap still open** |
| 15 | `VBAT_SENSE` floats above +3V3 | `727dbfd` | **D14** added; **PCB place + route still open** |
| 17 | Encoder divider 66.7 kΩ source impedance | `727dbfd` | → 10k/20k, same ratio, value-only |
| 20 | Reverse-cell fault current into the protector | `637be57` | `R20` 100R → **200R** (HYCON's max), halves it to ~18 mA |
| 19 | PVDD bulk under-rated for ripple | `35ca174` | → hybrid polymer, same D6.3 land: **BOM-only, no PCB impact**. **MPN corrected 2026-08-10** — `EEH-ZA1E101P` → **`EEH-ZA1E101XP`** (the typo'd number does not exist; PCBWay could not quote it). Stock verified: LCSC C264047 / DigiKey 3088115 |
| 21 | MCP23017 INTA/INTB tied | `637be57` | firmware requirement **R-BOARD-1** in `FIRMWARE.md` §6.5 |
| — | `FIRMWARE.md` sensor was LIS3DH, board is BNO085 | `3cc15da` | new §6.5.1 + **R-BOARD-3**; `SENSOR_INT` is BNO085-only now that `ALS_INT` is on GPB3 |

> ✅ **#12's firmware consequence is now handled.** `FIRMWARE.md` had specified an
> **LIS3DH @ 0x18** throughout. Corrected to **BNO085 @ 0x4A** in `3cc15da`, including a new
> §6.5.1 covering what actually changes: SHTP/SH-2 transport instead of a register map,
> `SENSOR_INT` meaning "packet available" rather than "tap happened", asynchronous boot,
> enabling only `SH2_TAP_DETECTOR`, clock stretching, and **R-BOARD-3** — `NRST` has no host
> line, so firmware cannot reset the hub and must degrade gracefully instead.

### 🔌 PCB sync — done 2026-08-04 (`4f00d68`)

The board was one part, one footprint, seven values and eleven pad-nets behind the
schematic. `gen/sync_pcb.py` closed that gap:

| | applied |
|---|---|
| New part | **D14** (BAT42W, SOD-123) placed at (76.50, 80.00) B.Cu |
| Footprint | **R1** 0603 → **1206** (position/rotation/side/nets preserved) |
| Values | `C104` 1µF · `R20` 200R · `R111`/`R112` 10k · `R114`/`R115` 20k · `J7` label |
| Pad nets | 11 — Q4.2/Q4.3 swap, U9.20/23/26, C181.2/C182.2/C183.2, L6.1, new `ALS_INT` on J7.6 + U13.4 |
| Copper cut | 11 stubs that would have shorted two nets, + 11 stale fragments |
| Zones | refilled (headless `ZONE_FILLER` **works** in KiCad 10 — `PCB_NOTES.md` was out of date) |

**Verification:** schematic ↔ PCB now agree on every part, footprint, value and pad
net (0 mismatches). DRC: **0 errors**, 7 warnings, **12 unconnected**.

D14's placement was chosen against **courtyards, tracks and vias** — a first pass
that only checked pad bounding boxes put it inside J3's courtyard and on top of a
`CELL_TEST` track. The chosen slot is 0.9 mm from `VBAT_SENSE` copper and 7.6 mm
from `+3V3` (it carries a few µA, so the longer leg is free).

**The 12 unconnected are the routing work**, and they are the whole of it:

| net | connection |
|---|---|
| `+5V` | Q4.3 → +5V (had to hop on F.Cu: Q4 pads 1/2 leave a 0.43 mm gap and a 0.25 mm track needs 0.45) |
| `PVDD` | Q4.2 → PVDD |
| `Net-(U9-SPK_OUTA+)` | U9.26 → leg A · C181.2 → leg A · C180.2 → leg A |
| `Net-(U9-SPK_OUTB+)` | U9.20 ↔ U9.23 · C182.2 · C183.2 · L6.1 |
| `+3V3` / `VBAT_SENSE` | D14 both ends |
| `ALS_INT` | J7.6 → U13.4 |

7 remaining warnings, all cosmetic and for the routing/silk pass: 3 dangling vias on
the old audio nets (they get consumed when those legs are re-routed) and 4 silkscreen
collisions introduced by the two new/changed footprints (D14 vs C131's reference,
R1's reference vs J1's shield pad).

### 🛣 Routing — done 2026-08-04 (`f480ced`)

All 12 ratsnest connections are routed. **DRC: 0 errors, 0 unconnected**, 4 silkscreen
warnings (cosmetic, see below).

| connection | how |
|---|---|
| `Q4.3` → `+5V` | B.Cu stub + **F.Cu hop** (vias at 100.92,70.16 and 97.61,68.26) — B.Cu cannot pass between Q4 pads 1/2 (0.43 mm gap vs 0.45 mm needed) |
| `Q4.2` → `PVDD` | direct B.Cu |
| `D14` → `VBAT_SENSE` | B.Cu + via onto the In1 run |
| `D14` → `+3V3` | B.Cu stub + F.Cu across + via |
| `ALS_INT` J7.6 → U13.4 | B.Cu in the 0.65 mm GPB lane, exiting to F.Cu before FULLCHG_EN's diagonal |
| leg A ×3, leg B ×4 | escape vias + F.Cu (the only free layer in that 46 mm² box) |

**The old leg-B trunk was recovered rather than re-routed.** After the net swap it survived
as *orphaned* copper on `Net-(U9-SPK_OUTA-)` (4 segs, 3 vias, U9.23→In2→In1→L6.1) — exactly
the path new leg B needed. Re-netting it to `Net-(U9-SPK_OUTB+)` and restoring three cut
stubs did most of leg B for free.

Also swept **350 zero-length track segments** (336 on `+5V`) that had been in the board since
the autorouter run in `91c5087` — 19 % of all segments, carrying no connectivity.

Three things the clearance checker had to learn, each caught by DRC disagreeing with it:
`pad.GetLayer()` lies for flipped pads · pads must be modelled as **rectangles** (a
circumscribed circle round U9's 1.90 × 0.40 pads on 0.65 mm pitch swallows both neighbouring
lanes) · **U9 carries a local clearance override of 0.2 mm**, twice the board rule.

Remaining 4 warnings are all silkscreen around D14, which landed in the tight
C131/C180-C183 cluster: its reference field and outline overlap C131's reference. Cosmetic —
for the silk pass, alongside the `#24` crystal-cap move.

### 🔥 Thermal vias (#5) — done 2026-08-04 (`c8dcf86`)

**49 GND vias added** inside the three exposed pads. DRC 0 errors / 0 warnings /
0 unconnected. Vias are tented (board setting), so solder cannot drain through during
hand assembly.

| part | EP | vias | ≈ via thermal path | dissipation |
|---|---|---|---|---|
| **U7** TPS55340 (12 V boost) | 3.4 × 5.0 mm | **14** | ~12 K/W | ~2 W at 12 W out |
| **U9** TAS5760M (amp) | 5.2 × 11 mm | **33** | ~5 K/W | ~1.5 W at 10 W out |
| **U2** LT3652 (charger) | 1.65 × 2.85 mm | **2** | ~84 K/W | ~0.7 W at 1 A |

(0.3 mm drill through 1.6 mm FR4 ≈ 169 K/W each, in parallel — indicative, not a
substitute for a thermal sim. Previously all three had **zero**, so the EPs could only
spread heat sideways through 35 µm of B.Cu.)

**U2 is the exception and it is finding #18's fault.** Its pad is 1.65 mm wide and two
inner-layer *signals* cross it: `Net-(U8-IO11_LRCLK)` runs vertically on In1 at x = 95.94,
right through the pad, and `+5V` closes the right side on In2. That leaves one usable
corner, hence 2 vias instead of the 4-6 the pad would otherwise take. **Rerouting those two
inner-layer traces clear of the pad is the fix**, and it is worth doing if `R18` is ever
changed to 2 A (#7), which roughly doubles U2's dissipation to ~1.4 W.

A checker bug is worth recording: an exposed pad's **paste-mask apertures are separate pads
with no copper layer**, and treating them as copper falsely blocked most of every EP —
U7 went 6 → 14 and U9 20 → 33 once they were excluded. U2 went 0 → 2 for the same reason.

### ⏳ Open — PCB / fab work (deferred to the routing pass)

| # | Finding | Sev | What it needs |
|---|---|---|---|
| ~~1, 2, 11, 15~~ | PCB side of the fixes above | ✅ | **done** — synced and routed, 0 errors / 0 unconnected |
| 4 | Power widths — **mostly done** | 🟠 | `POWER` net class + widening + **4 trunks moved to F.Cu** (*Fixed in a0031c9*); **`+3V3` widened 2026-08-06** (837 → 555 mΩ, ESP32 feed 258 → **169 mΩ**). **`VBAT` is the remainder**: 99.7 mm still ≤0.3 mm and in-place widening is exhausted — **re-verified 2026-08-06**, every thin segment on every power net has ≤0.10 mm of headroom and the three worst (21.5/14.9/6.7 mm on In2) have **0.00**. Needs hand re-routing, not a width change — see below |
| ~~5~~ | Thermal vias | ✅ | **done** — 49 GND vias: U7 **14**, U9 **33**, U2 **2**. U2 is capped by #18; **re-verified 2026-08-06 with the paste-aperture bug fixed: exactly 0 free 0.6/0.3 slots remain inside U2's EP**, so 2 is the honest maximum until #18 moves. Fine at `R18` = 1 A (~0.7 W); revisit if `R18` goes to 2 A |
| 8 | Switcher hot loops — **ground return done, placement not** | 🟠 | **Return half fixed** (*Fixed in `f55b15f`*): 23 vias tie every hot-loop return pad into the GND planes, 7.64 mm worst case → 0.62–2.20 mm. **Placement half needs pcbnew by hand** — every candidate slot for `C100`/`C102`/`C127`/`C128`/`C130`–`C132` fails routing or DRC; measured target coordinates are in §8 |
| ~~9~~ | `C238` 50 mm from U15 | ✅ | **done** (*Fixed in `8441d29`*) — moved to (26.73, 46.18); pad 1 → U15 pin 5 now **1.75 mm** (was 50.50), plus 2 GND vias 0.62 mm from its return pad |
| 18 | ~3 m of signal routing on the inner GND planes | 🟡 | **power copper on In1/In2 halved, 299.6 → 160.9 mm** (*Fixed in a0031c9*) — the four longest slots are gone. Signal routing on the inner layers is untouched, and still **blocks #5**: two inner-layer signals cross U2's exposed pad, capping it at 2 thermal vias instead of 4-6 |
| ~~25~~ | `AN-JST-001` (Juken mounting app-note) not on file | ✅ | **Closed 2026-08-08 — the footprint was validated against the vendor drawing by the board owner.** The 3× Ø3.0 mm peg holes and Ø4.6 mm shaft hole are confirmed, so the app note is no longer a gate. It remains the reference for **insertion force** and for snap-peg length vs board thickness, so re-open this if the 1.6 mm stackup is ever changed |
| ~~23~~ | U9 exposed-pad land vs TI drawing | ✅ | **checked 2026-08-06 against DAP0032C sheet 4223691/A.** EP copper **5.2 × 11 mm = TI exactly**. Pin lands 1.90 × 0.40 vs TI's 1.50 × 0.45 — an IPC-7351 alternate, which TI's note 6 permits, and the extra toe/heel + wider gap is *better* for hand soldering. **The one deviation is the mask/paste window: 4.11 × 4.36 mm vs TI's SMD-defined 3.04 × 3.74** (≈58 % more area). **Reviewed and confirmed correct by the board owner 2026-08-06 — no change.** See the pre-fab audit below |
| ~~—~~ | PCB had no title block | ✅ | **fixed 2026-08-06** — gerbers carried no title/rev/company/date. Now `rev "0.3"`, matching the schematic and the silkscreen. The old "title block reads rev A" finding was **wrong and has been withdrawn** |
| 26 | `R1` pad 2 hung on a 0.049 mm sliver | ✅ | **found and fixed 2026-08-06** — latent open left by the #13 0603→1206 land swap. See the pre-fab audit below |
| 24 | 32.768 kHz load caps 5.9 mm from Y1 | 🟡 | **placement done** (*Fixed in `3d6d830`*) — `C145` 5.85 → **2.60 mm**, `C146` 5.85 → **1.75 mm**; XTAL_P 9.1 → 5.46 mm, XTAL_N 14.0 → 4.95 mm, B.Cu only, 0 vias, 4 GND vias ringing the pair. Items 2–4 of the recommendation (keep the ABS07, first-article check, FW fallback) still stand |

### 🔩 Mechanical: hole density and board thickness — assessed 2026-08-04

**The holes are not a fragility problem.** Every hole on the board, of every kind:

| | area | % of board |
|---|---|---|
| 615 vias (all 0.3 mm) | 43.5 mm² | 0.36 % |
| 54 plated through-holes | 70.5 mm² | 0.58 % |
| 9 non-plated (motor shaft/pegs, holder, USB) | 62.1 mm² | 0.51 % |
| **total** | **176 mm²** | **1.46 %** of 12 069 mm² |

Locally, inside the exposed pads where the new vias went, hole area is **U7 5.8 %,
U9 4.1 %, U2 3.0 %** of the pad — ordinary for a PowerPAD via array, and the pads are
small islands inside a continuous board. Stiffness is set by the laminate that is still
there; 1.5 % perforation, spread out, does not measurably change it. **No reason to remove
or thin the arrays.**

**What actually governs robustness here is the mounting, not the thickness.** The board is
110 × 110 mm with **four corner M3 holes only** (5,5 · 104.8,5.2 · 5,105 · 105.5,105.5), and
the 18650 holder sits at (51, 96) — **47 mm from the nearest mount**, in the middle of an
unsupported bottom edge. Inserting a cell into a sprung holder is the largest force this
board will ever see in normal use, and it lands exactly there.

Going 1.6 → 2.0 mm buys stiffness ∝ t³ = **1.95×**. A fifth mounting hole near (55, 100)
would have cut that span roughly in half (deflection ∝ span³, so ~8× on the mode that
matters) — **considered and declined 2026-08-04: the board keeps its four corner mounts.**
Thickness is therefore the only lever left on this axis.

**If you do go to 2.0 mm, three things need checking first:**

1. **Aspect ratio.** The smallest drill on the board is **0.2 mm** (the ESP32 module's
   12-hole thermal array). At 1.6 mm that is 8:1; at 2.0 mm it becomes **10:1**, which is at
   or past most fabs' standard limit and moves the order into a premium tier. The 0.3 mm
   vias are fine either way (5.3:1 → 6.7:1).
2. **The Juken motor's snap pegs.** X27 §3.2/§3.4 defer hole sizes and insertion force to
   application note **AN-JST-001, which is not in `datasheet/`** (finding #25). The *hole
   sizes* were validated against the vendor drawing on 2026-08-08 and are settled — but
   snap pegs are moulded for a specific board thickness, and the note is still the only
   source for **insertion force** and peg length. At 1.6 mm this is closed; **a thickness
   change re-opens it**, and it is the one change that could stop the motor seating at all.
3. **Through-hole connector retention** — J1's USB-C shell pegs in particular. (BT1 is
   surface-mount, so the holder itself is unaffected.)

Thermally, 2.0 mm makes the new vias ~25 % worse (longer barrel), which is second-order
next to going from zero vias to 49.

### ⚡ Power widths (#4) — partially done 2026-08-05 (`a20faef`)

A `POWER` net class (1.0 mm track, 0.8/0.4 via, board-default 0.1 mm clearance) now covers
`VBAT`, `PVDD`, `+5V`, `+12V`, `VBUS` and both amp output legs, so future routing defaults
to copper instead of 0.25 mm. Every existing segment on those nets was then widened to the
most its own neighbourhood allows, per-net targets from IPC-2221 (1 oz, external, ~20 °C):

| net | total copper R | narrowest | still ≤0.3 mm |
|---|---|---|---|
| `VBAT` | 408 → **268 mΩ** (−34 %) | 0.25 mm | **48 %** of its length |
| `PVDD` | 118 → **77 mΩ** (−35 %) | 0.25 mm | **53 %** |
| `+5V` | 513 → **322 mΩ** (−37 %) | 0.25 mm | 36 % |
| `+12V` | 187 → **111 mΩ** (−41 %) | 0.25 mm | — |
| `VBUS` | 107 → **83 mΩ** (−22 %) | 0.25 mm | — |
| amp leg A / leg B | 58 → **40** / 56 → **33 mΩ** | 0.25 mm | — |

Point-to-point, where the path is pure track: charger BAT → cell+ **126 → 70 mΩ**,
12 V → wake LEDs **136 → 69 mΩ**, PVDD ORing → amp **61 → 47 mΩ**, 5 V → motor VM
**172 → 146 mΩ**.

**Read the "narrowest" column, not the average.** A net's current capacity is set by its
tightest neck, and every one of these still contains 0.25 mm segments — ~1.2 A at +20 °C,
against VBAT's 4 A and PVDD's 3 A peaks. **So #4 is not closed by this.** The resistance win
is real, the ampacity limit is not yet lifted.

The remaining necks are not scattered — they are a few long runs on the **inner layers**:

| net | worst neck | where |
|---|---|---|
| `+5V` | **57.1 mm** at 0.25 mm | In2.Cu, (20.5, 44.7) → (77.6, 44.7) |
| `VBAT` | **21.5 mm** at 0.25 mm | In2.Cu, (51.0, 61.0) → (72.5, 61.0) |
| `VBAT` | 11.9 mm | B.Cu, (86.0, 39.3) → (86.0, 51.2) |
| `PVDD` | 11.9 mm | B.Cu, (106.7, 71.3) → (106.7, 83.2) |

They cannot widen in place because In2 is packed with the signal routing of **#18**. The fix
is the one #4 always pointed at: **move these trunks onto F.Cu**, which is still ~98 % empty
(118 mm of track on the whole layer). That is a re-route, not a width change, and it closes
#4 and a good part of #18 together.

#### Trunks moved to F.Cu — 2026-08-05

Four of them, each getting a via cluster at both ends (parallel vias sized to the net's
peak current — one 0.4 mm via is good for ~1.5 A) and a short stub of the original inner
copper kept under the cluster so every via is tied on both sides:

| net | run | was | now |
|---|---|---|---|
| `+5V` | 57.1 mm, (20.5, 44.7) → (77.6, 44.7) | In2.Cu 0.25 mm | **F.Cu 1.00 mm**, offset −0.40 |
| `+12V` | 42.8 mm, (30.1, 76.5) → (72.9, 76.5) | In2.Cu 0.60 mm | **F.Cu 1.00 mm** |
| `+5V` | 23.9 mm, (76.7, 9.2) → (76.7, 33.0) | In1.Cu 0.60 mm | **F.Cu 1.00 mm** |
| `VBAT` | 27.9 mm, (103.4, 61.0) → (103.4, 88.8) | In1.Cu 0.50 mm | **F.Cu 0.97 mm**, offset −0.25 |

| | before | after |
|---|---|---|
| power copper on In1/In2 | 299.6 mm | **160.9 mm** (−46 %) |
| `+5V` ≤0.3 mm / R | 95.1 mm / 322 mΩ | **39.8 mm / 235 mΩ** |
| `+12V` R | 111 mΩ | **99 mΩ** |
| `VBAT` R | 268 mΩ | **260 mΩ** |
| DRC | 0/0/0 | **0/0/0** |

**`VBAT`'s necks survive, and this is where #4 stops being automatable.** 99.7 mm of VBAT is
still ≤0.3 mm (≈57 mm on B.Cu, ≈43 mm on In2), and a sweep of every thin power segment on
the board shows **≤0.05 mm of widening headroom on each** — the widening pass already took
everything the current placement offers. Three specific dead ends:

- **`VBAT` (51.0, 61.0) → (72.5, 61.0), 21.5 mm at 0.25 mm — the worst one.** Its east end
  sits inside M1's pad field. There are ~190 free via slots within 3.6 mm of it, but not one
  has a clear In2 jumper back to the node, so neither moving the trunk nor running a *parallel*
  F.Cu trunk beside it can be tapped at that end.
- **`VBAT` (35.6, 59.0) → (48.9, 59.0)** was left alone deliberately. It is already 0.50 mm,
  and shortening it to a stub puts its end 0.096 mm from an M1-2i diagonal — the full-length
  track clears, the stub does not. Nothing to gain, a DRC error to lose.
- The diagonal past (72.5, 61.0) is boxed in by GND copper and takes only 0.31 mm on F.Cu, so
  extending the chain buys a neck rather than removing one.

Closing the rest means **moving other nets out of the way by hand in pcbnew** — a placement
and re-route job around M1, not something the width/via passes can reach.

### 🔎 Pre-fab audit — 2026-08-06

A DFM/latent-defect sweep over the routed board, looking specifically for things
`kicad-cli drc` structurally cannot report. **Baseline: DRC 0 errors / 0 warnings /
0 unconnected, ERC 0, `review_check.py` 11/12 with 0 regressions, zone fills verified
current (a headless refill changes 0 of 4).**

#### 26. `R1` pad 2 was hanging on 0.049 mm of copper — FIXED

The one real defect found. `Net-(U1-VDD)` reached `R1` pad 2 through a **0.049 mm-wide
strip**, well inside any fab's etch tolerance:

| | |
|---|---|
| cause | the #13 0603 → 1206 land swap. The track ended at x = 98.675, dead centre of the **old** 0603 pad. The 1206 pad's outer edge sits at x = 98.600, so the centreline missed it by 0.075 mm and only the track's 0.125 mm end cap still overlapped |
| why DRC was silent | connectivity is a boolean. Any overlap ≥ 1 nm reads as connected — 0 unconnected items, no warning |
| what it would have done | `R1` feeds the CH224K's shunt-regulated VDD. Open it and there is no PD negotiation → no 15 V → no charging and no 12 V rail. A board that looks fine and doesn't work |
| fix | one added B.Cu segment, (98.675, 35.092) → the pad centre (98.037, 35.092). It lies entirely inside the pad plus the existing end cap, so **no copper exists anywhere it did not already exist** — DRC-neutral by construction |

**A full sweep for the same class of defect found nothing else.** Every pad on the board
and every free track end was scored on how much copper actually bridges the joint:
**0 pads and 0 track ends now below 0.12 mm** (was 1 and 1, both `R1`). Worth keeping as a
standing check — footprint swaps are exactly what produces these.

#### `+3V3` widened — the rail the #4 pass never covered

`+3V3` was never in the `POWER` net class, so `a20faef` skipped it and **100 % of its
423 mm was still 0.25 mm**, making it the highest-resistance rail on the board. Same
method as `a20faef` (width-only, no endpoint moves, no re-routing), capped at 0.80 mm on
the outer layers and 0.60 mm on In1/In2:

| from `L3` (3V3 buck out) to | before | after |
|---|---|---|
| **`U8` ESP32 3V3 pin** | 258 mΩ | **169 mΩ** |
| `U9` DVDD / AVDD | 569 / 512 mΩ | **393 / 346 mΩ** |
| `U13` MCP23017 | 405 mΩ | **273 mΩ** |
| `U11` motor driver VCC | 388 mΩ | **270 mΩ** |
| `J7` sensor board | 413 mΩ | **285 mΩ** |
| net total copper R | 837 mΩ | **555 mΩ** (−34 %) |

97 of 164 segments widened. The two that mattered were the **42.9 mm In2 run at y = 9.83
feeding the MCU (0.25 → 0.60)** and the **22.8 mm In1 run at x = 102.62 (0.25 → 0.60)**.
Only the MCU number has real headroom consequences: at a 500 mA Wi-Fi TX peak the DC droop
at U8's pin goes **129 mV → 85 mV** against 300 mV of margin to the 3.0 V floor. Everything
else on the rail draws single-digit mA and was never in trouble — the win there is noise
coupling, not droop.

Widening an inner-layer trace does not meaningfully worsen #18: **a slot's return-path cost
is set by its length, not its width**, and these slots already existed at full length.

**Two segments' worth of the pass had to be given back, and the reason is worth recording:
`U9` carries a local clearance override of 0.2 mm** — twice the board rule, the same trap
the `f480ced` routing pass hit. The width solver modelled the board default and produced 3
clearance errors plus 1 starved thermal on `C161`. All 12 `+3V3` segments within 2.5 mm of
a `U9` pad (and 2 near `C161`'s GND pad) were reset to 0.25 mm; DRC back to 0/0/0.

#### The 2026-08-06 manual edits, assessed

| edit | verdict |
|---|---|
| `Y1` reference moved to (0.885, −1.9) | fine, cosmetic |
| `PVDD` (102.68, 83.12)→(102.68, 81.63) redrawn | identical geometry, new UUID. No-op |
| **`VBAT` stub out of `Q2` pad 3 re-drawn as a 5-segment chain at 0.30 mm** (was one 0.96 mm diagonal at 0.25 mm) | path kept, **width reverted to 0.25 mm** — and the combination beats both originals |

At 0.30 mm the chain bought **~0.1 mΩ** (0.96 mm going 0.25 → 0.30 on a net that totals
261 mΩ) and cost **0.014 mm of clearance to `Q2` pad 2**: 0.1169 mm before → 0.1029 mm after,
the second-tightest gap on the board. `Q2` pad 2 is `Net-(BT1-Pin_1)`, the **raw cell
terminal** — the one pair where a short bypasses the reverse-polarity FET and defeats
`CELL_TEST`, i.e. exactly where the safety section says to assume the worst.

**Reverted to 0.25 mm on 2026-08-06, keeping the re-routed path.** That was the right pairing:
the kink walks the trace away from pad 2, so at 0.25 mm it clears by **0.150 mm** — better
than the widened chain (0.1029) *and* better than the straight diagonal it replaced (0.1169).
The re-route was worth doing; only the width was not. Two 15.5 µm segments remain in the
chain — router artefacts, harmless, left alone rather than risk re-drawing the path.

| `Q2` pad 2 ↔ `VBAT` | clearance |
|---|---|
| original straight diagonal, 0.25 mm | 0.1169 mm |
| re-routed chain, 0.30 mm | 0.1029 mm |
| **re-routed chain, 0.25 mm (now)** | **0.1500 mm** |

#### Fab risk, measured

| | value | verdict |
|---|---|---|
| DRC / ERC / unconnected | 0 / 0 / 0 | clean |
| min track | 0.25 mm | standard |
| clearance rule | 0.10 mm | **at the floor of a standard 4-layer process** |
| pairs at exactly 0.100 mm | **16** | no margin at those 16 spots — **unchanged by this session's edits** |
| pairs below 0.127 mm (5 mil) | **347** | fine at JLC/PCBWay standard, not at a 5-mil-only shop |
| min annular ring | 0.15 mm (0.3/0.6), 0.20 on the 0.4/0.8 power vias | standard |
| smallest drill | **0.20 mm ×12** (ESP32 EP array) → **8:1** aspect at 1.6 mm | at the standard-tier limit; **10:1 if the board goes to 2.0 mm** |
| min hole-to-hole (edge) | 0.274 mm | above the 0.25 rule, tight |
| min copper to board edge | 1.175 mm | comfortable |
| zone fills | current | gerbers will match the editor |

**Nothing here blocks fabrication.** The residual risk is not electrical — it is the three
items that have never been checked against a vendor drawing (#25 Juken `AN-JST-001`, the
Keystone 1043 footprint's own "VERIFY vs the drawing before fab" note, and the GCT USB-C
land) plus the two MPNs that still need a DigiKey stock check. Those are paperwork, and they
are the ones that scrap a whole board rather than cost a bodge wire.

> ### ✅ All three closed 2026-08-08
>
> **Validated against the vendor drawings by the board owner:** the **Juken
> X40.879** land (peg holes and shaft hole), the **Keystone 1043** holder
> (pegs, index post, PC-pin span — the footprint's own `VERIFY` note has been
> replaced with the verification), and the **GCT USB4160** USB-C land.
>
> That clears the whole "scraps a board" class. What is left before ordering is
> the two **stock** checks below (`R1` `RC1206FR-071KL`, `C172` `EEH-ZA1E101P`),
> which gate *assembly*, not bare-board fab — and neither board has been through
> a fab's DFM yet, since no Gerbers have been exported from either project.

#### Checked and found clean (no action)

- **Bypass-cap locality, board-wide.** Every 2-pad cap on `<rail, GND>` measured to its
  intended pin. The outliers are exactly the ones §8 already lists (`C161`/`C163` at
  10.2/11.7 mm from U9's DVDD/AVDD, `C140`/`C141` at 8.7/8.8 mm from U8) — the #9 sweep
  surfaced nothing new. `C142`/`C143` at 4.7/4.8 mm from U8's 3V3 pin is mediocre for an RF
  module but boxed in by the antenna keep-out and the crystal cluster.
- **Copper stubs.** One 1.05 mm dangling fragment existed, on `Net-(U1-VDD)` — it was the
  #26 track, now landed. Every other free end terminates on a pad, via, track or pour.
- **Courtyards.** 0 footprints missing one (the DRC config ignores `missing_courtyard`, so
  this was worth confirming independently).
- **Board outline.** 8 Edge.Cuts shapes, closed, 110 × 110 mm with R6 corners.
- **Li-ion labelling.** `BT1`'s footprint already carries `Li-ion 18650 only 2.5-4.2V` on
  silk, plus `CELL+`/`CELL-`. The safety-labelling requirement is met.
- **Exposed-pad zone connection.** All four GND pours use thermal relief (0.4 mm spokes,
  0.3 mm gap), including under U7/U9/U2's EPs. **Considered switching the EPs to a solid
  connection and declined**: the 49 vias already bond them to three full planes, so the
  extra B.Cu coupling is second-order, while a solid pour connection makes a PowerPAD
  materially harder to hand-solder — and hand-assembly is a hard project constraint.
- **`U1` (CH224K) EP has 1 via and room for ~15 more. Declined for the same reason** — it
  dissipates well under 0.15 W, so the only effect of via-in-pad there would be solder
  wicking on a part that does not need the heat path.

#### Revision stamping — settled 2026-08-06

**There was never a "rev A".** That claim, repeated twice in this document, was wrong:
`gen/build.py` has set `rev="0.3"` all along and the schematic title block reads
`(rev "0.3")`, matching the `Tibo - Wooden Clock v0.3` on F.Silkscreen. The two statements
below and in the schematic-readiness section have been corrected.

The real gap was the other file: **`clock.kicad_pcb` had no `title_block` at all**, so
gerbers and the fab drawing carried no title, revision, company or date. Added, matching
the schematic:

```
(title "Wooden Clock - main board") (rev "0.3") (company "Tibo") (date "2026-08-06")
```

The PCB's date is deliberately 2026-08-06, not the schematic's 2026-08-04 — the board
changed today and the schematic did not.

#### `U9` mask/paste window (#23) — closed 2026-08-06

Cross-checked against TI's drawing (above) and **confirmed correct by the board owner**;
no change. For the record, if a stencil is ever ordered: 7 of the 33 thermal vias sit
inside the current 4.11 × 4.36 mm aperture and will wick. Shrinking it to TI's
3.04 × 3.74 mm is a footprint-only edit that touches no copper. Hand-soldering the EP it
makes no difference, which is the build method that matters here.

### ⏸ Deferred — accepted for now, revisit later

| # | Finding | Why deferred | Revisit when |
|---|---|---|---|
| ~~6~~ **re-opened for build #1** | Protector OC trip | ⚠ **2026-08-10: PCBWay will supply `-GB` — LCSC C160793 (-HB) is out of stock.** On this build the finding is held closed by firmware (R-AUDIO-1's `-GB` budget), not by the part. ✅ **Fixed 2026-08-08 by part change: `HY2111-GB` → `HY2111-HB`.** Same SOT-23-6, same pinout, same support network, every threshold identical except discharge-OC `V_DIP` (200 ±25 mV vs 150 ±25 mV) — worst-case trip **1.89 A → 2.65 A**, which clears both the battery case and the plugged sunrise-alarm case. Speaker stays 4 Ω. Firmware side is `FIRMWARE.md` **R-AUDIO-1** | Only if the audio ceiling or the LED budget is raised |
| 7 | Battery IR drop / `R18` caps the wall at ~1 A | Still deferred. The **cell supplies the balance even while plugged**, which is why #6 mattered — now covered by the -HB trip with ~15 % margin on the sunrise-alarm case | `R18` 0.1 → 0.05 Ω doubles the wall's share, but needs thermal vias under U2 (#5, blocked by #18) |
| 14 | Ungated always-on loads (~70 mA idle) | **Mostly wall-powered**, so backup runtime is good enough | If battery runtime becomes a goal — gate the EM14 (26 mA) and QRE1113 LED (14 mA); GPA4-6/GPB3 are free |
| 16 | `CELL_TEST` on battery cuts power | Self-recovering reset loop, **not damage**; firmware-enforced instead | If a hardware interlock against `PD_PG` is ever wanted |

### 🧾 Schematic readiness — checked 2026-08-04 (`35ca174`)

The schematic is **complete and validated for the PCB phase.** Evidence, not assertion:

| Check | Result |
|---|---|
| `kicad-cli sch erc --severity-all` | **0 violations** |
| Generator lint (dangling wires, wires through bodies) | **clean** |
| Components / nets | 183 / 149 |
| Parts missing a footprint | **0** |
| Parts missing MPN / Manufacturer / Package | **0** (`stamp_bom`: 183 matched, 0 unmatched) |
| Unintentional dangling (1-pin) nets | **0** — all 18 are explicit NC flags, each verified deliberate |
| `review_check.py` schematic items | **8/8 pass, 0 regressions** |
| Remaining open findings that touch the schematic | **none** — every one is PCB, BOM-sourcing, or explicitly deferred |

The 18 deliberate no-connects: J1 SBU1/2 (USB 2.0 subset) · J6 DET_A/B (no spare GPIO for
card-detect) · U1 CFG2/3 (correct for CH224K single-resistor mode, WCH fig. 6.1) · U10 STAT ·
U13 GPA4-7 spare + 2 NC pins · U16 VBUS (deliberate — the 15 V PD rail exceeds the pin's
5.25 V rating) · U3 NC · U8 IO35-37 (reserved by the octal PSRAM).

**Delta the PCB must absorb (what F8 will reconcile):**

| Kind | Items |
|---|---|
| New part | `D14` (BAT42W, SOD-123) — place near D13 |
| Footprint change | `R1` 0603 → **1206** |
| Value changes | `C104` 100nF→1µF · `R20` 100R→200R · `R111`/`R112` 100k→10k · `R114`/`R115` 200k→20k · `J7` label |
| Net changes | Q4 pads 2/3 · U9 pads 20/26 · C181/C182 far pads · new `ALS_INT` |

**Two things that are not blockers but are decisions you own:**

1. ~~**Revision letter.**~~ **Withdrawn 2026-08-06 — this finding was simply wrong.** The
   title block never read "rev A"; `gen/build.py` sets `rev="0.3"` and the schematic has
   carried `(rev "0.3")` throughout, consistent with the silkscreen. The genuine gap was that
   the *PCB* had no title block; that is now fixed. See the pre-fab audit above.
2. ~~**Two MPNs need a DigiKey stock check** before ordering~~ — **both closed 2026-08-10 by the
   PCBWay quote.** `R1` = `RC1206FR-071KL` (#13) quoted and sourceable at $0.441. `C172` was the
   one that bit: `EEH-ZA1E101P` (#19) **is not a real MPN** and PCBWay could not buy it —
   corrected to **`EEH-ZA1E101XP`**, in stock at both LCSC (C264047) and DigiKey (3088115).
   The written substitution rule in `parts_db.py` is what made the swap checkable in minutes.

### ✔ Accepted — no change planned

| # | Finding | Rationale |
|---|---|---|
| 22 | No UART console (IO43/44 consumed) | Known and accepted; USB-Serial-JTAG is the bring-up path, boot-log TX still probeable on IO43 |

### 🏭 Assembly quote — PCBWay `T-1N10W1120006A`, 1 unit (2026-08-10)

First real fab/assembly quote off this BOM (183 lines, $259.20 all-in: parts $146.83 + assembly
$29.00 + PCB $83.37). Six lines came back flagged. What they cost the design:

| item | ref | PCBWay said | answer |
|---|---|---|---|
| 14 | `C172` | "provide exact part number or URL" | **Our MPN was wrong.** `EEH-ZA1E101P` is not a real part — the ZA series uses the plain `-P` suffix only at other voltages/values (`EEH-ZA1H101P` = 50 V, `EEH-ZA1E560P` = 56 µF); the 25 V/100 µF part is **`EEH-ZA1E101XP`**. Corrected in `parts_db.py` + stamped into both files. LCSC **C264047** (4,252 stk) / DigiKey **3088115** (3,364 stk). Same land, 2 A @100 kHz, 30 mΩ — #19's rule is met 3× |
| 22 | `D40`/`D41` | "1 = 10 pack" | Adafruit **2758** confirmed (natural white ~4500 K). Asked them to **ship the 8 unused pixels** — 5 of them are the off-board status row on `J12`, which is not a BOM line |
| 23 | `F1` | `[DNP]` | **Accepted, and correct**: a 77 °C one-shot TCO cannot survive reflow. ⚠ **The safety chain is therefore incomplete as delivered** — see below |
| 36 | `L4` | price rising | accepted at actual price |
| 37 | `M1` | `[DNP]` | as instructed; holes must stay |
| 75 | `U3` | "we will supply `HY2111-GB`" | **-HB is out of stock at LCSC (C160793)**, and our own BOM note handed them C82747, which *is* the -GB. -HB requested if they can source it, **-GB accepted otherwise** — which un-does #6, see below |

**Two consequences that outlive the order:**

1. **`F1` ships unpopulated.** The board must not be run with a cell in the holder until the
   Cantherm `SDF-DF077S` is hand-soldered in (≥3 mm from the body, heatsink the lead). DigiKey
   1014754, $0.87, 6,508 in stock. Until then the cell's only thermal backstop is `RT1` + the
   LT3652's NTC qualification, which covers *charging* and not a cell that goes hot on its own.
2. **`U3` is a `-GB` again unless PCBWay finds -HB.** #6 was closed by that part change; on this
   build it is closed *by firmware instead*. Worst-case discharge-OC trip goes back to
   **1.89 A** (150 ±25 mV / 50–66 mΩ), and the plugged sunrise-alarm case draws **~2.3 A from
   the cell** — i.e. it trips. **`FIRMWARE.md` R-AUDIO-1 now carries a `-GB` budget** (keep peak
   cell current < ~1.8 A: full 8 W audio *or* the LED ramp, never both). **Check the marking on
   the assembled board** and relax the budget only if it reads -HB.

---

## #16 — how crucial is it, really?

**Not very, and firmware discipline is a legitimate fix here.** The failure mode is bounded:
asserting `CELL_TEST` on battery opens `Q2`, all rails drop, the MCP23017 loses power, its GPIOs
go hi-Z, `R26` pulls `Q8` off, `Q9` turns off and `Q2` conducts again — the board reboots. It is a
**self-recovering reset loop, not damage**, and plugging in ends it immediately. Nothing is
stressed beyond ratings at any point.

The residual risk is a firmware bug that re-asserts it every boot on battery, which would look
like a dead product until the user plugs in. That is cheap to prevent and now written down as
**R-BOARD-2** in `FIRMWARE.md` §6.5: gate every assertion on a fresh `PD_PG` read.

Worth knowing: the schematic comment in `b_charger.py` claiming *"on battery Q2's body diode keeps
the system alive but drops ~0.4 V"* is **wrong** — the body diode faces VBAT→cell+ and cannot
back-feed. That comment is what would mislead someone into thinking this is safe on battery.

## #24 — recommendation for the 32.768 kHz crystal

**Do the placement fix, keep the part, add a firmware check.** In priority order:

1. **Placement (during the routing pass, ~free).** ✅ **Done 2026-08-05 (`3d6d830`).** Move
   `C145`/`C146` to within ~2 mm of `Y1`'s pins — they are 5.9 mm away today. `Y1` itself is
   already fine (2.9 / 3.2 mm from the module's XTAL pins). Keep both nets on one layer, no vias,
   and ring the pair with GND stitching: these are MΩ-impedance nodes and the current layout gives
   them a large loop next to the switchers.

   | | before | after |
   |---|---|---|
   | `C145` pad 2 → `Y1` pin 2 | 5.85 mm | **2.60 mm** |
   | `C146` pad 2 → `Y1` pin 1 | 5.85 mm | **1.75 mm** |
   | XTAL_P copper | 9.1 mm | **5.46 mm** |
   | XTAL_N copper | 14.0 mm | **4.95 mm** |
   | layers / vias | B.Cu, 0 vias | **B.Cu, 0 vias** ✓ |
   | GND vias at the caps' return pads | 2.63 / 1.07 mm | **0.62 mm** (4 vias) |

   `C145` sits at (58.90, 18.50) and `C146` at (56.35, 22.11). **This was not two part moves but
   one delete-place-reroute job**: the XTAL traces were themselves counted as obstacles, yet they
   existed only to reach the caps' old position, so nothing could fit beside `Y1` until both nets
   were stripped first. `C145`'s best slot overlapped a hand-drawn 0.1 mm silk mark at
   (57.00, 16.50)→(58.50, 18.00), so `C145` was nudged clear rather than moving your mark; the
   reference fields of `Y1`/`C145`/`C146`/`R113` were repositioned into the space the caps vacated,
   having kept their old offsets.
2. **Keep the ABS07.** CL 12.5 pF with 18 pF loads gives CL_eff ≈ 12 pF — correct. Its 70 kΩ max
   ESR is *at* Espressif's ceiling, but that is a startup-margin question, not a correctness one,
   and the part is on Espressif's own kind of BOM. Don't respin it speculatively.
3. **Verify on the first article, not on paper.** Check that the RTC actually starts on the
   32 kHz crystal across the temperature range you care about. If it is marginal, the drop-in is
   any 3215 32.768 kHz part with lower ESR (≤ 50 kΩ) at the same 12.5 pF CL — same land, same caps.
4. **Firmware fallback.** Have `chrono` detect 32 kHz oscillator start failure and fall back to
   the internal RC, logging it. Consequence of the fallback is only more drift between SNTP syncs,
   which for this product is cosmetic.

Severity is genuinely low: worst case is a slightly worse holdover clock, not a broken board.

---

## Progress log

- **2026-08-03 — Phase 0** (branch `review-fixes`): destructive-run guard on `gen/pcb_build.py`;
  `gen/review_check.py` added as a standing regression check.
- **2026-08-03 — Phase 1** (`1f3736a`): #1, #2, #3 fixed in the schematic. ERC 0, netlist diff is
  exactly the 6 intended node moves.
- **2026-08-03 — Phase 1b** (`727dbfd`): #11, #12, #15, #17.
- **2026-08-03 — Phase 2 prep** (`1cf1629`): `C181`/`C182` swapped positions so each bootstrap cap
  sits on its own leg's column while keeping its **original U9 pin** — which the existing PCB
  copper already implements. Cuts the #2 PCB rework from two long crossing pin-side routes to two
  short straight far-pad runs.
- **2026-08-04** (`637be57`): #10 (silkscreen), #13 (1206), #20 (200R), #21 + #16 written into
  `FIRMWARE.md`. DRC 0 violations / 0 unconnected after the silkscreen addition.
- **2026-08-04** (`3cc15da`): `FIRMWARE.md` corrected from LIS3DH to BNO085 — new §6.5.1 (SHTP/SH-2
  driver model, board strapping, tap-only feature set), **R-BOARD-3** (no host reset line), plus the
  `SENSOR_INT`/`EXPANDER_INT` split from #11 and the standing-draw note in §7.4.
- **2026-08-04** (`4f00d68`): **PCB synced to the schematic** via the new
  `gen/sync_pcb.py`; zones refilled; DRC 0 errors / 12 unconnected. See the PCB sync
  table above.
- **2026-08-04** (`35ca174`): #19 PVDD bulk → hybrid polymer (BOM-only); #16 comment in
  `b_charger.py` corrected; drawing date bumped. **Schematic declared complete** — see the
  readiness table above.
- **2026-08-05** (`a0031c9`): **#4 / #18 — four power trunks moved from the inner planes to
  F.Cu.** Power copper on In1/In2 **299.6 → 160.9 mm**; `+5V` thin copper 95→40 mm and 322→235 mΩ;
  `+12V` 111→99 mΩ; `VBAT`'s In1 run re-placed 0.52 → 0.97 mm. DRC 0/0/0, `review_check.py`
  11/12 with 0 regressions. `VBAT`'s 0.25 mm necks remain and are **not** automatable — see the
  "Trunks moved to F.Cu" section for the three specific dead ends.
- **2026-08-05** (`f55b15f`): **#8 ground return.** 23 GND vias placed hard against every
  hot-loop return pad. The original §8 table measured only the forward legs and understated
  the problem: with a pour on all four layers the return is the plane image current, and
  every capacitor's return pad was 2.07–7.64 mm from the nearest via while the ICs were
  properly stitched. Now 0.62–2.20 mm. DRC 0/0/0. The placement half was attempted and
  **abandoned deliberately** — no candidate slot survives routing + DRC; §8 records the
  coordinates for doing it by hand. Same pass measured that **37 of 191 hot-loop segments
  run over a slot in their return plane**, including both switch nodes.
- **2026-08-05** (`8441d29`): **#9 done.** `C238` moved next to U15 — 50.50 → **1.75 mm** to
  the VCC pin, plus 2 GND vias at 0.62 mm. Surfaced the junction-vs-leaf rule above.
- **2026-08-05** (`3d6d830`): **#24 item 1 done.** Crystal cluster re-placed and re-routed —
  `C145` 5.85 → 2.60 mm, `C146` 5.85 → 1.75 mm, XTAL_P 9.1 → 5.46 mm, XTAL_N 14.0 → 4.95 mm,
  B.Cu only with 0 vias, 4 GND vias ringing the pair. Exposed two tooling bugs, both fixed:
  courtyard collision was tested on **bounding boxes**, and U8's courtyard is a T (module body +
  antenna keep-out) whose bbox spans x[19.95, 68.05] y[−13.79, 27.50] and swallows the whole
  crystal corner — every candidate near `Y1` was rejected until it used the real polygon; and a
  rejected candidate restored the footprint **origin** to the **pad's** coordinates, silently
  shifting both caps by the pad offset.
- **2026-08-06 — pre-fab audit.** **#26 found and fixed**: `R1` pad 2 was reachable only
  through a **0.049 mm** strip of copper — a latent open left by the #13 0603→1206 land swap,
  invisible to DRC because connectivity is a boolean. Sweeping the same class board-wide found
  **nothing else**: 0 pads and 0 free track ends below 0.12 mm of bridging copper. Also in
  this pass: **`+3V3` widened in place** (837 → 555 mΩ; ESP32 feed 258 → **169 mΩ**, 500 mA TX
  droop 129 → 85 mV) — it had never been in the `POWER` class so `a20faef` skipped it, leaving
  100 % of 423 mm at 0.25 mm. **#23 cross-checked against TI's DAP0032C drawing and confirmed
  by the board owner.** **#4 and #5 re-verified and both are genuinely exhausted** — every thin
  power segment has ≤0.10 mm of headroom (the three worst have 0.00), and U2's EP has exactly
  0 free via slots once paste apertures are excluded from the obstacle set. The PCB's missing
  `title_block` was added at `rev "0.3"`, and the "title block reads rev A" finding was
  **withdrawn as incorrect**. DRC 0/0/0, ERC 0, `review_check.py` 11/12 with 0 regressions.
- **2026-08-06** — the `+3V3` widening pass tripped **`U9`'s local 0.2 mm clearance override**
  (twice the board rule) for the second time in this project's history; `f480ced` hit it too.
  Any geometry solver run against this board must read the footprint-local override, not the
  netclass. 12 `+3V3` segments near `U9`/`C161` were reset to 0.25 mm to clear it.
- **2026-08-06** — the owner's `Q2` stub re-route was kept and only its **width** reverted
  0.30 → 0.25 mm. Worth recording as a general result: **the kinked path at 0.25 mm clears
  `Q2` pad 2 by 0.150 mm, beating both the 0.30 mm chain (0.1029) and the original straight
  diagonal it replaced (0.1169)** — the re-route was the good half of that edit.
- **2026-08-06 — tooling note.** `pcb_io.save_board()` does **not** protect
  `clock.kicad_prl`: pcbnew's settings manager rewrites it when the interpreter exits, i.e.
  *after* `save_board` has restored it. Any script run against the board silently resets the
  editor's active layer and visible-layer mask. Harmless (it is pure UI state, no design data)
  but it shows up as a spurious dirty file in `git status` and has to be put back by hand.
- **2026-08-10 — first assembly quote (PCBWay `T-1N10W1120006A`).** Six flagged lines, answered
  in the "Assembly quote" table above. Two real outcomes: **`C172`'s MPN was a typo**
  (`EEH-ZA1E101P` → **`EEH-ZA1E101XP`**, corrected in `parts_db.py` and stamped into
  `.kicad_sch`/`.kicad_pcb`; nothing else in the BOM changed), and **`U3` reverts to `HY2111-GB`**
  because -HB is out of stock — #6 is now held by firmware on this build, not by the part.
  `F1` (77 °C TCO) and `M1` (movement) ship unpopulated, both deliberately.
- Three bugs found while building the migration, worth remembering: (a) via clusters were
  committed before the *other* end was known to be placeable, leaving 5 orphan vias; (b) an inner
  stub widened past its original width shorted `M1-2i`, so a stub must keep the original width —
  it is then safe by construction, being a sub-segment of copper that already passes DRC;
  (c) `Board.via_clash()` has no spatial pre-filter, so a ring search over it runs for minutes
  until `Board.local()` is passed in.
- Fixing #1 surfaced a latent generator bug: `sch2.py::_xf()` mirrored **before** rotating while
  KiCad mirrors **after**, silently swapping the two mirror axes at rot 90/270. Harmless until now
  (BT1 was the only mirrored part, at rot 0); fixed, verified not to move any other net.

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

> ### ✅ Fixed 2026-08-08 — `HY2111-GB` → `HY2111-HB`
>
> The third option was taken, and the 8 Ω driver was **rejected**: at 4 Ω the plugged
> ceiling is `L5`/`L6` saturation, not the rail, so 8 Ω would give the *same* ~8 W
> plugged and cost 3 dB on battery for nothing. Datasheet p.190–201 confirms the two
> suffixes differ only in `V_DIP`; worst case (175 mV / 66 mΩ) the trip moves
> **1.89 A → 2.65 A**.
>
> | case | from the cell | vs 2.65 A |
> |---|---|---|
> | plugged, alarm only | ~1.6 A | comfortable |
> | plugged, sunrise alarm (12 W) | ~2.3 A | ~15 % margin |
> | battery, alarm | ~1.9 A peak | comfortable |
>
> The 15 % on the sunrise-alarm row is tolerance stack, not headroom, so the firmware
> must not ramp LEDs and audio to peak simultaneously — written up as **R-AUDIO-1** in
> `FIRMWARE.md` §6.2, together with the "a trip looks like a spontaneous reboot"
> logging requirement. Schematic, PCB and BOM updated (`sync_pcb.py` carried the value
> across); ERC 0, DRC 0, `review_check.py` 11/12 unchanged.

> ### ⚠ 2026-08-10 — build #1 gets a `-GB` anyway
>
> PCBWay's BOM quote (`T-1N10W1120006A`) came back "the part we will supply is
> `HY2111-GB`, please confirm". **`-HB` is real and stocked nowhere useful right now**:
> LCSC lists it as **C160793** but out of stock, and our own BOM note pointed them at
> **C82747**, which is the `-GB`. -HB was requested if they can source it; **-GB was
> accepted** so the build is not held up for a $0.60 part.
>
> **So the numbers above revert for this board** — worst case 175 mV over 66 mΩ becomes
> **125 mV over 66 mΩ = 1.89 A**, and the plugged sunrise-alarm row (~2.3 A from the cell)
> **trips**. The fix is the first of the two options from the original finding: cap the
> firmware budget. `FIRMWARE.md` **R-AUDIO-1** now carries both budgets — **< ~1.8 A peak
> cell current on a `-GB`** (8 W audio *or* the sunrise ramp, never both at peak) and the
> looser -HB one. **Read the SOT-23-6 marking when the board arrives** and pick the budget
> from what is actually on it; do not assume.

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

### Ground return — done 2026-08-05 (`f55b15f`)

The table above measures the *forward* legs, and that framing understated the
problem. GND is a filled pour on **all four layers**, so each loop's return is the
plane image current directly under the forward trace — but only once the return pad
has actually reached the plane. The ICs were stitched; **not one capacitor was**:

| return pad | nearest GND via, before |
|---|---|
| `D11` anode (LT3652 catch diode) | **7.64 mm** |
| `C131` (TPS55340 Cout) | 6.98 mm |
| `C129` (TPS55340 Cin bulk) | 6.42 mm |
| `C130` (TPS55340 Cout) | 5.49 mm |
| `C127` (TPS55340 Cin 10 µF) | 4.52 mm |
| `C132` / `C100` / `C102` / `C128` / `C172` | 2.07 – 3.79 mm |
| *(for contrast)* `U2` EP · `U7` EP · `U9` EP | 0.39 · 0.75 · 0.82 mm |

Until that via exists the return current runs sideways along the B.Cu pour to find
one, and **that detour, not the forward trace, sets the loop area**. 23 vias added
hard against each return pad → every one now reaches the planes at **0.62–2.20 mm**.
Holes 698 → 721 (+3.3 %, still far inside the density assessed above). DRC 0/0/0.

### Placement half — NOT done, needs pcbnew by hand

Every candidate slot was searched on a 0.25 mm grid × 4 rotations, filtered for
courtyard clearance, clear copper under the pads, and B.Cu routability, then gated on
real DRC. **None survived**, so nothing was moved:

| part | now | best slot found | why it failed |
|---|---|---|---|
| `C132`/`C131`/`C130` → D20 K | 12.1 / 14.4 / 17.2 mm | **3.83 mm** at (91.11, 66.51) | routable, but real DRC finds a 0.17 mm hole clearance and a dangling track — 4 candidates, all fail |
| `C128` → U7 VIN | 10.0 mm | 3.25 mm at (78.40, 60.94) | 120 slots free, **none B.Cu-routable**: `Net-(D20-A)` (the switch node) sits between the slot and VIN |
| `C127` → U7 VIN | 7.4 mm | 3.50 mm at (78.15, 60.69) | same — boxed in by the switch node |
| `C100` → U2 VIN | 16.2 mm | 8.19 mm at (107.18, 45.72) | 120 slots free, none B.Cu-routable |
| `C102` → U2 VIN | 9.0 mm | 6.00 mm at (102.93, 43.47) | 99 slots free, none B.Cu-routable |
| `D11` → U2 SW pin | 7.9 mm | 7.15 mm | 0.75 mm gain — not worth the re-route |

The slots are real and the distances are worth having; what the automated pass cannot
do is *push existing copper aside*, which is exactly what pcbnew's interactive router
does. **Use the coordinates above as placement targets.**

### Return planes are slotted under the hot loops

A second, separate finding from the same pass — **37 of 191 hot-loop segments (19 %)
run over a break in their adjacent return plane** (B.Cu ↔ In2, F.Cu ↔ In1):

| hot net | inner nets slotting its return plane |
|---|---|
| `VBAT` | 10 nets — `+5V`, `I2C_SDA`, `Net-(BT1-Pin_1)`, `Net-(D40-DOUT)`, … |
| `+12V` | 5 — `I2C_SDA`, `Net-(U8-IO10_BCLK)`, `Net-(U8-IO12_DOUT)`, … |
| **`Net-(D20-A)`** (boost SW node) | 3 — `I2C_SDA`, `Net-(U8-IO10_BCLK)`, `Net-(U8-IO12_DOUT)` |
| `Net-(U9-SPK_OUTB+)` | 3 — `PVDD`, both speaker legs |
| `Net-(D11-K)` (buck SW node) | 1 — `Net-(U8-IO11_LRCLK)` |

The switch nodes are the ones that matter: a slot there both enlarges the loop and
puts the I²S/I²C lines directly under the highest-dV/dt copper on the board. This is
**#18's inner-layer signal routing seen from the EMI side** — same fix, same job.

---

# 🟡 Worth a pass

**14. Ungated always-on loads.** EM14 encoder **26 mA max** on `+5V`, QRE1113 homing
LED **14 mA** on `+3V3` (R98 = 150 R straight from the rail), plus ~5–7 mA of SK6812
idle — none switchable. ≈ 70 mA from the cell at idle → ~34 h on 2400 mAh usable.
Spare expander pins (GPA4-6, GPB3) exist to gate both through a small FET.

**16. `CELL_TEST` on battery = hard power cut.** Q2's body diode is oriented
VBAT → cell+, so it cannot back-feed. Asserting CELL_TEST unplugged kills the rails,
which drops the MCP23017, which releases CELL_TEST → Q2 back on → boot loop. The
comment in `gen/b_charger.py` ("on battery Q2's body diode keeps the system alive but
drops ~0.4 V") is **wrong**. Consider gating CELL_TEST with `PD_PG` in hardware.

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

**22. No UART console.** IO43/IO44 are consumed by MCLK and the expander INT; the
PROG header J2 is 3V3/GND/EN/IO0 only. All bring-up depends on USB-Serial-JTAG.
(Boot-log TX is still probeable on IO43.)

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
  (0x20 / 0x6C / **0x77** / 0x29 / 0x4A). *(BME688 corrected from 0x76 on
  2026-08-07 — `R10` straps `SDO` high, so the part answers at 0x77;
  `kicad-sensor/REVIEW.md` #19. No collision either way.)*
- **BOM sanity** — BAT46W-E3-08 really is SOD-123; XGL5050 on the XAL5050 land
  (already verified in `parts_db.py`); TCO rated 250 V/15 A; all four J1 VBUS pads and
  all GND pads netted; cap voltage ratings correct (25 V on the 12 V rail, 50 V on the
  audio LC, 16 V on the 5 V/3.3 V rails).
