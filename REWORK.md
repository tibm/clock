# Rework — main board rev0.3, build #1

**Two reworks. Both required. They are independent — order does not matter.**

| # | What | Why | Difficulty |
|---|---|---|---|
| **R1** | Replace **`U4`** (SO-8 dual FET) — `AOSD32334C` → **`AO4838`** | The board **cannot charge at all**. Also trips the protector on cell insertion. | Medium — hot air, SO-8 |
| **R2** | **`U9` pin 1 (`AVDD`)** → `PVDD` — one trace cut + one wire | The amp **makes no sound**. Its analog supply is 1.2 V under minimum. | Medium — one scalpel cut next to 0.65 mm pitch |

Everything else on the v0.4 list is either a respin-only change or deliberately
not worth reworking. See [Not doing](#not-doing-and-why) at the end.

Full engineering rationale: [`kicad/REVIEW.md`](kicad/REVIEW.md) **V9** and **V13**.
This file is only how to do it.

---

## ⚠ Before you start

1. **TAKE THE CELL OUT.** Both reworks involve heat and sharp tools on a board with an
   18650 in a holder. Remove it and put it somewhere else on the bench.
2. **Unplug USB.**
3. **Open `kicad/clock.kicad_pcb` in pcbnew** and turn on **View → Flip Board View**.
   Almost every part on this board is on **B.Cu** (the back), so the flipped view is
   what matches the side you are looking at. All coordinates below are the ones
   pcbnew shows in its cursor readout.
4. Work with magnification. Both jobs are on parts with 0.65–1.27 mm pitch.

### Kit

| Item | Notes |
|---|---|
| Fine-tip soldering iron | 2 mm chisel or smaller |
| Hot-air station | **R1 only** — for lifting the SO-8 |
| Scalpel / #11 craft blade | **R2 only** — fresh blade, a dull one skates |
| **30 AWG Kynar wire-wrap wire** | **R2** — see [wire](#which-wire) below |
| Flux (no-clean gel or pen) | Both. Use plenty |
| Solder wick + fine solder | 0.5 mm or finer |
| Tweezers | Fine, non-magnetic |
| Multimeter with sharp probes | For the checks — **not optional** |
| Magnifier / microscope | ≥10× |
| IPA + brush | Clean flux off before powering up |
| Kapton tape | To mask neighbours during hot air |

### Parts

| Ref | Fit this | Order | Note |
|---|---|---|---|
| `U4` | **AO4838** (30 V dual N-ch, SO-8) | [DigiKey 3152401](https://www.digikey.com/en/products/detail/alpha-omega-semiconductor-inc/AO4838/3152401), ~$1.15 | Ordered 2026-09-13 — check it arrived |

---

# R1 · Replace `U4` — `AOSD32334C` → `AO4838`

### Where

| | |
|---|---|
| **Ref** | `U4` |
| **Location** | **(15.4, 80.6)** on **B.Cu** |
| **Package** | SOIC-8, 3.9 × 4.9 mm, 1.27 mm pitch |
| **Landmark** | Battery-protection corner. Nearest neighbours: `R21` 5.5 mm, `C126` 7.7 mm, `U3` (SOT-23-6) 9.0 mm |

### Why it has to change

The `HY2111` protector measures both charge and discharge current as the voltage drop
across this FET pair. The fitted `AOSD32334C` is **52–66 mΩ**, which is too high in
*both* directions:

- **Charging:** 1 A × 52 mΩ = 52 mV against a −60 mV worst-case threshold → **8 mV of
  margin**. Measured on build #1 2026-09-13: a 3.4 V cell took **zero** charge.
- **Insertion:** the 5 V boost's 3.7 A startup surge × 52 mΩ = 244 mV against a 150 mV
  trip → trips every time.

The **AO4838** pair is **26–33 mΩ** — charging lands at 33 mV, 1.8× under the
threshold.

### Pinout — verified identical, no rotation change

Both parts, from their own datasheets (`datasheet/protection_mosfet_ao4838.pdf`,
`datasheet/protection_mosfet_aosd32334c.pdf`):

```
        AOSD32334C  (old)          AO4838  (new)
         S2  1    8  D2             S2  1    8  D2
         G2  2    7  D2             G2  2    7  D2
         S1  3    6  D1             S1  3    6  D1
         G1  4    5  D1             G1  4    5  D1
```

**Identical.** Same package, same pin assignment, same orientation. Pin 1 goes where
pin 1 was. All four drain pins are tied together on this board, so D1/D2 labelling
does not matter.

### Steps

1. **Photograph `U4` first**, including which way the pin-1 dot points. This is your
   reference if anything goes wrong.
2. Mask the neighbours with Kapton. Nothing is very close — `R21` is the nearest at
   5.5 mm and `U3` is 9.0 mm away — so a small nozzle and a steady hand is enough.
3. Flux the old part's leads generously.
4. Hot air, ~300 °C, small nozzle, moving in circles over the part only. Lift it with
   tweezers when the solder goes wet — **do not pry it**.
5. Wick the pads clean, then flux again. Aim for flat, bright pads with no bumps.
6. Place the AO4838 with **pin 1 in the same corner** as the photo. Tack one corner
   pin, check alignment under magnification, then solder the rest.
7. Clean with IPA and inspect every joint.

### Check before powering up

| Check | Expect |
|---|---|
| Pins 1–2–3–4 to each other | No shorts |
| Pins 5–6–7–8 to each other | **Continuous** — all four are the tied drain node |
| Pin 1 to pin 8 | Not a dead short |
| Visual | All 8 leads wetted, nothing bridged |

### Then

Put the cell in, plug in the 15 V brick, and watch the charger:

```
sensor vbat stream 1 600
```

`chrg=1` and the millivolts climbing = **charging works for the first time**.

> ⚠ **`U3` marking — do this while you have the magnifier out.** Read the marking on
> `U3` (SOT-23-6 at **(6.5, 81.5)**) and write it in the board log. The BOM says
> `HY2111-**HB**`; build #1 was assembled with a **`-GB`** because the -HB was out of
> stock. Which one is fitted decides the current budget in `FIRMWARE.md` R-AUDIO-1.
> **No rework either way** — with the AO4838 fitted a `-GB` is livable (see
> [Not doing](#not-doing-and-why)).

---

# R2 · `U9` pin 1 (`AVDD`) → `PVDD`

### Where

| | |
|---|---|
| **Ref** | `U9` — TAS5760M amp |
| **Location** | **(96.5, 78.6)** on **B.Cu**, HTSSOP-32, 0.65 mm pitch |
| **Pin 1 pad** | **(92.850, 83.432)** |
| **Cut the trace at** | **≈ (92.4, 83.43)** |
| **Wire to** | **`C170` pad 1** at **(103.50, 83.934)** |

### Why

`AVDD` (pin 1) is a **4.5–26.4 V** pin — the same range as `PVDD`. Only `DVDD` is the
3.3 V one. This board wires `AVDD` to **+3V3**, 1.2 V under its minimum, so the amp's
analog section never comes up: I²C answers perfectly, every register reads back
correctly, all three I²S clocks are correct on a scope — and reg 0x08 sits at `CLKE`
with no audio. Datasheet §6.3 and Figure 64.

### The good news

Verified against `clock.kicad_pcb`: **all four copper zones on this board are GND**, so
there is no +3V3 pour. Pin 1's *only* connection to +3V3 is **one 0.25 mm trace, 0.85 mm
long**:

```
   (92.850, 83.432)                    (92.000, 83.432)
    U9 pad 1  ●━━━━━━━━━━━━━━━━━━━━━━━━━━●  junction ──┬── to the 0.8 mm +3V3 trunk
              ↑        0.85 mm            └── to a via at (91.688, 83.101)
         CUT HERE ≈ (92.4, 83.43)
```

**So you cut a trace — you do NOT lift a pin.** The pad stays attached to the pin and
you solder the wire to the pad. Nothing gets lifted next to `SFT_CLIP` on 0.65 mm pitch.

### Step 1 — cut the trace

The trace leaves pin 1 heading **away from the package** and ends 0.85 mm later at a
T-junction (one branch widens to 0.8 mm, the other goes to a via). It is the only trace
touching pin 1.

- Score it **across**, in the middle of its 0.85 mm run, at ≈ (92.4, 83.43).
- Several light passes, not one deep one. Then scrape a small **gap** — don't just
  score it, remove 0.2–0.3 mm of copper so it cannot re-bridge.
- ⚠ **The nearest other copper is pin 2's `GVDD_REG` trace, 0.65 mm away** (at
  y = 82.782). That is the one thing to keep the blade off.

**Verify the cut before going further:**

| Check | Expect |
|---|---|
| `U9` pin 1 ↔ +3V3 (e.g. `C162` pad 1, or `U9` pin 10) | **Open** |
| `U9` pin 1 ↔ GND | **Open** ← catches a whisker into the GND pour |
| `U9` pin 1 ↔ `U9` pin 2 | **Open** ← catches blade damage |
| +3V3 ↔ GND | Normal, not shorted |

### Step 2 — the wire

Solder from **`U9` pin 1 / its pad** to **`C170` pad 1**.

#### Tie-point options — all on B.Cu, all the same PVDD net

| Option | Pad | Position | Distance | Verdict |
|---|---|---|---|---|
| **A ⭐** | **`C170` pad 1** | (103.50, 83.934) | **10.7 mm** | **Recommended.** Closest cap, 0.9 × 0.9 mm pad, near-straight run, and the 0.1 µF sits right at the tie point |
| B | `C171` pad 1 | (106.00, 83.934) | 13.2 mm | Fine. Same size pad, 2.5 mm further |
| C | `C172` pad 1 | (104.70, 64.014) | 22.8 mm | **Biggest, easiest pad** (6.3 mm electrolytic) — pick this if the 0603s look intimidating. Longest wire |
| D | `U9` pin 28 | (100.15, 80.832) | 7.8 mm | ❌ **Don't.** Shortest, but it is another 0.4 mm fine-pitch pin — doubles the risk for nothing |

> ### ⛔ Use pad **1**, never pad 2
> **`C170` pad 2, `C171` pad 2 and `C172` pad 2 are all GND.** Wiring `AVDD` there
> shorts it to ground. On `C170`/`C171` the GND pad is the one at **y = 85.484**; on
> `C172` it is the one at **x = 99.30**. **Ring it out before you solder** — the pad you
> are about to use must read continuous with `U9` pin 21 or 28, and open to GND.

#### Which wire

**30 AWG Kynar wire-wrap wire.** Insulated, ~0.25 mm conductor, ~0.5 mm outside
diameter, strips and tins easily. This is the standard bodge wire and it is the right
choice here.

- **Current is not the constraint.** `AVDD` is analog bias — a few mA. 30 AWG is good
  for hundreds of mA; it is chosen for *handling*, not ampacity.
- **Don't go thicker.** Anything stiffer puts mechanical stress on a 0.4 mm-wide pad and
  will eventually tear it off. 28 AWG is the most I would use.
- **Alternative:** 0.2–0.3 mm enamelled copper (magnet) wire, if that is what you have —
  thinner and neater, but you must burn or scrape the enamel off both ends and it is
  bare if the enamel is nicked.
- **Insulated is mandatory**, whichever you pick — see the routing note.

#### Routing

⚠ **A straight line from pin 1 to `C170` passes directly over `U9` pin 32**
(`GVDD_REG`, at (100.15, 83.432) — the same y). With insulated wire that is fine.
Do not let it sit under tension on the pin, and **never** run bare wire on that path.

Two ways to dress it:

- **Over the package** — run it across the plastic body of `U9` and tack it down with a
  dot of Kapton. Simplest.
- **Around the bottom edge** — from pad 1, head away from the package to about y ≈ 86,
  across, then up to `C170`. Avoids the pins entirely. ⚠ Keep clear of **`L5` (93.3,
  87.4)** and **`L6` (98.8, 88.0)** — those are the speaker output inductors, and the
  amp's output traces are the noisiest copper on the board.

Keep the wire short, flat, and strain-relieved with a tack of Kapton mid-run.

### Check after soldering

| Check | Expect |
|---|---|
| `U9` pin 1 ↔ `U9` pin 28 (PVDD) | **≈ 0 Ω** |
| `U9` pin 1 ↔ GND | **Open** |
| `U9` pin 1 ↔ +3V3 | **Open** |
| `U9` pin 1 ↔ `U9` pin 2 | **Open** |
| PVDD ↔ GND | Not shorted |

### Then

Power up, and with the speaker on `J3`:

```
audio tone 1000
audio status
```

**Reg 0x08 should read `0x00`.** If the `CLKE` line is gone, you should be hearing
1 kHz at 10 % volume.

### If it goes wrong

**The cut is reversible.** A wire from pin 1's pad back to the via at **(91.688,
83.101)** puts `AVDD` back on +3V3, exactly as built.

> ⚠ After this rework `AVDD` follows `PVDD` — **including up to 12 V** when the boost is
> enabled. That is intended and is what the datasheet does (26.4 V recommended max,
> 30 V absolute).

---

## After both reworks

### Firmware

Nothing to change to *test* either fix — the current build already reports everything
you need (`audio status`, `sensor vbat stream`).

One thing becomes **available** once `U4` is swapped: the audio volume ceiling. It is
pinned at **25 %** (`hal::audio::kMaxVolPct`) because with the old FET a full-scale tone
sat on the protector's trip. With the AO4838 the trip moves to 3.8–6.6 A and that no
longer binds. **Don't raise it in the same session as the rework** — confirm audio works
at 10 % first, then raise it deliberately and watch the cell current.

### What each rework unblocks

| Rework | Unblocks |
|---|---|
| **R1** `U4` | Charging (all of it) · `sensor vbat stream` showing a real charge curve · milestone 1's last bench item · the 25 % audio ceiling |
| **R2** `U9` pin 1 | **All of audio** — milestone 6. The firmware is written and already proven correct on the bench |

---

## Not doing, and why

| Item | Status |
|---|---|
| **`U3` → `HY2111-HB`** (v0.4 V10) | **Skip.** The `-HB` is out of stock and single-sourced, and with the AO4838 fitted a `-GB` is livable: worst case it trips on *cell insertion only*, which costs a one-time bridge (`FIRMWARE.md` §12.0.13), not damage. Charging is fixed by `U4` alone. **Do read the marking** and log it |
| **`R99` 10k → 22k** (v0.4 V2) | **Wait.** Improves the homing signal, but the decision needs a re-measurement with the real printed hand tabs on — they reflect far better than the bare surfaces the original numbers came from. `NEXT_STEPS.md` F2.4 |
| **`U5` `EN` supervisor** (v0.4 V8) | **Not worth reworking.** Needs a part that is not chosen yet and a net that does not exist. Workaround: keep a charged cell in the holder |
| **`USB_D±` on `J2`** (v0.4 V6) | **Not a rework.** The temporary form is an inline USB-C pass-through *cable* between brick and `J1` — must pass `CC1`/`CC2`, so a PD sniffer, not a fan-out breakout. `FIRMWARE.md` §12.0.11 |
| Everything else (V1, V3, V4, V5, V7, V11, V12) | Respin-only — footprints, connectors, net names, BOM sourcing |

---

## Quick reference — every coordinate on one card

```
R1   U4     (15.4,  80.6)   B.Cu  SOIC-8      AOSD32334C -> AO4838, same orientation
     U3     ( 6.5,  81.5)   B.Cu  SOT-23-6    read the marking, -GB or -HB, log it

R2   U9     (96.5,  78.6)   B.Cu  HTSSOP-32
     pad 1  (92.850, 83.432)                  AVDD
     CUT    (92.4,   83.43)                   0.25 mm trace, 0.85 mm long, B.Cu
     via    (91.688, 83.101)                  <- solder here to UNDO the cut

     PVDD tie points (pad 1 on every cap - pad 2 is GND):
       C170 pad 1   (103.50, 83.934)   10.7 mm   <- recommended
       C171 pad 1   (106.00, 83.934)   13.2 mm
       C172 pad 1   (104.70, 64.014)   22.8 mm   <- biggest pad
       U9 pin 28    (100.15, 80.832)    7.8 mm   <- fine pitch, avoid

     Keep clear:  U9 pin 2 GVDD_REG (92.85, 82.782)  - 0.65 mm from the cut
                  U9 pin 32 GVDD_REG (100.15, 83.432) - the wire crosses over it
                  L5 (93.3, 87.4) / L6 (98.8, 88.0)  - speaker output inductors
```
