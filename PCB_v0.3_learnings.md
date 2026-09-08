# PCB bring-up

Bench notes taken while bringing up board #1. **Now tracked, with analysis and measured
numbers, in [`kicad/REVIEW.md`](kicad/REVIEW.md) § "v0.4 — carried out of rev0.3 bring-up"
(V1-V5)** — this file stays as the raw record of what was noticed at the bench.

Thinks noted that would need a change for a next PCB version:

1. `M1` lower hold needs to be moved. It's not exactly where it should be. (Needed manual rework). The top 2 holes, plus the center hole is perfect. The only change needed is: Move the bottom hole only further down by 2mm.
2. Flashing is painful! We should add another IO to `J2` allowing to provide 5V for flashing.
3. Board-2-Board connection (sensor) has the cable flipped. We should modify that. 

---

*Folded into `kicad/REVIEW.md` on 2026-09-08 as V1 (M1 peg), V3 (J2 +5 V) and V4 (harness
reversal), alongside two found by the firmware bring-up: V2 (`R99` 10k → 22k, doubles the
homing signal) and V5 (gate the QRE1113 LED — power, and ambient rejection).*
