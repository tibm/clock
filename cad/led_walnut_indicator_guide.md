# Walnut LED Indicator Manufacturing Blueprint

Step-by-step process for casting 6 diffused LED indicators into walnut hardwood.
Shopping list is **Home Depot in-store**; alternatives listed for anything that may not be on the shelf.

**Stock assumption: 6 mm walnut.** See §"Working at 6 mm" before drilling — the thin stock
changes the drill order and gives you a build choice.

---

## 🛒 Shopping List (Home Depot)

| # | Item | Home Depot pick | Aisle | Alternatives |
|---|---|---|---|---|
| 1 | Clear casting epoxy | [TotalBoat Table Top, 1 qt kit](https://www.homedepot.com/p/TOTALBOAT-Table-Top-1-qt-Kit-Clear-High-gloss-Epoxy-Interior-Exterior-Resin-For-Bar-Counter-and-Table-Top-Application-409334/332248285) — 1:1, ~20 min working time, cures hard and sands like acrylic | Paint / epoxy | [SuperClear Table Top](https://www.homedepot.com/p/SuperClear-2-Gal-Table-Top-Epoxy-Resin-and-Activator-141014/321860696) or [WiseBond Deep Pour](https://www.homedepot.com/p/WiseBond-1-5-gal-Clear-Deep-Pour-2-in-Thick-Single-Pour-2-1-Ratio-River-Table-Epoxy-Kit-DW025109/318986737). **Cheap route:** a J-B Weld ClearWeld / Loctite clear 2-part syringe (~$8) — you only need ~0.3 mL total. Trade-off below. |
| 2 | Grain sealer | [Zinsser SealCoat](https://www.homedepot.com/p/Zinsser-1-qt-SealCoat-Universal-Sanding-Sealer-Case-of-6-854/205140204) (dewaxed shellac), 1 qt can | Wood finishes | **Thin CA glue** (Loctite/Gorilla super glue, adhesives aisle) wicked into the bore — arguably better, hardens the end grain too. Or seal with a first wash of *un-tinted* epoxy (§Step 2). |
| 3 | White diffusion pigment | Not reliably stocked — check the epoxy endcap for a white pigment/dry-color pack | Paint / epoxy | **A drop of white latex from a paint sample pot** (~$6, paint desk), or a pinch of **cornstarch / baking soda** from the pantry (free, works fine). |
| 4 | Mold-face tape | **Tuck tape / red sheathing tape** — flat, rigid, epoxy peels off it cleanly | Building materials | Clear packing tape (stretches slightly → a marginally domed face). |
| 5 | Drill bits | [Milescraft 7-pc metric brad point set](https://www.homedepot.com/p/Milescraft-Metric-Brad-Point-Bit-Set-7-Piece-2318/206520373) — has both the 5 mm and the 10 mm | Power tool accessories | A 10 mm **Forstner** for the counterbore if you'd rather not pre-drill (flatter pocket floor). |
| 6 | Sandpaper | 150 / 220 / 400 grit sheets + a **flat rubber sanding block** | Paint | — |

**Do NOT buy a water-repellent deck sealer** (Olympic Waterguard and similar). They are wax/silicone
penetrating repellents that never fully harden — epoxy fisheyes and delaminates on them. Dewaxed
shellac or CA only.

### Epoxy trade-off (item 1)
* **Table-top epoxy** — thin, self-levelling, long open time so bubbles rise out, cures glass-hard,
  sands and polishes cleanly. Being thin, it *will* find any gap in your tape. Best result.
* **5-minute syringe epoxy** — slightly amber, traps bubbles you can't outwait, and sands gummy.
  Acceptable for 6 dots you're going to frost anyway; not if you want optical clarity.

---

## 📏 Working at 6 mm

6 mm is thin, but it's workable — and for an RGBW emitter it's arguably the *right* thickness.
Pick a route before you drill:

### Option A — counterbored, 2 mm plug (bright, fragile)
10 mm relief pocket from the back, 4.0 mm deep, leaving a **2.0 mm** front web. LED nests in the
pocket right behind the plug.
* ✅ Brightest, least epoxy, fastest cure.
* ⚠️ A 2 mm web over a 10 mm pocket is a drum skin. Don't clamp it hard, don't lean on it while
  sanding, and don't run an orbital sander over it.
* ⚠️ **RGBW warning:** with only 2 mm of diffuser you will see the individual red/green/blue/white
  dies as separate coloured spots, especially at low brightness. Mitigate by leaving a **2–3 mm air
  gap** between the emitter and the back of the plug instead of pressing it flush — the gap does more
  for colour mixing than pigment does.

### Option B — straight 6 mm plug, no counterbore (dimmer, robust) ← recommended at 6 mm
Drill 5 mm straight through, fill the full 6 mm, mount the LED behind the board.
* ✅ Board keeps its full strength; nothing to blow through.
* ✅ The 6 mm plug acts as a light pipe — much better colour mixing, no die separation.
* ✅ One drilling operation, no depth stop, no relief pocket.
* ⚠️ ~30–40 % dimmer, and a deeper pour so watch for bubbles. Use less pigment than you think.

> **Note on the emitters:** the clock's status LEDs are SK6812 RGBW **5050 SMD** parts (flat 5×5 mm
> packages), not 5 mm through-hole LEDs. A 5050 has no dome and butts flat against the back of the
> plug — which is why the air gap in Option A matters. Also note a 5 mm THT LED's flange is ~5.4–5.9 mm
> and will *not* push into a 5 mm hole; it has to sit in the pocket (Option A) or behind the board (Option B).

---

## 🛠️ Step-by-Step

### Step 1: Drill
1. Mark the 6 centres. Test the whole sequence on an offcut of the same 6 mm stock first.
2. Clamp a **sacrificial backer board** tight behind the walnut — non-negotiable on thin stock or the
   back face blows out.
3. Drill the **5 mm hole through, from the front face**, with the brad-point bit. ~1500–2000 rpm,
   steady feed. Drilling front-to-back means the crisp visible rim is *cut*, not torn.
4. **Option A only:** flip the board and counterbore 10 mm from the back. The brad point self-centres
   in the 5 mm hole you just made — that's why you drill the small hole first; a 10 mm brad point spur
   driven into solid 6 mm stock will punch straight out the front face. Depth stop at **4.0 mm**.
   Verify on the offcut.

### Step 2: Seal the grain (crucial)
Walnut is open-pored. Unsealed, liquid epoxy wicks sideways into the grain and leaves a permanent
dark "wet" halo around every indicator.
1. Blow out all dust.
2. Wick **dewaxed shellac** or **thin CA** into the bore walls until they're visibly wet. A pipe
   cleaner or a toothpick works better than a spray for a 5 mm hole.
3. Let it dry fully (shellac: ~15 min; CA: a few min, or seconds with accelerator).
4. *Zero-purchase alternative:* pour un-tinted epoxy in, let it soak in and gel (~1 hr), then pour the
   tinted batch on top. The first pass becomes the sealer.

### Step 3: Tape the face
1. Burnish **Tuck tape** hard across the front face over all 6 holes — press the adhesive down around
   every hole rim with a fingernail or a plastic scraper.
2. This tape is the mould wall and it forms the finished front face of every indicator. Any gap here
   leaks thin epoxy under the tape and stains the face.

### Step 4: Mix and tint
1. Mix the epoxy strictly by its ratio — off-ratio epoxy stays tacky forever. You need ~0.3 mL total,
   but most kits won't mix accurately below ~10 mL, so mix 10 mL and waste the rest.
2. Dip a toothpick tip in white pigment (or a pinch of cornstarch) and stir it in **incrementally**.
3. Stop while it's still milky and semi-translucent — cloudy, not opaque. Over-pigmenting kills far
   more light than people expect. Test on the offcut with an actual LED behind it before committing.

### Step 5: Pour and cure
1. Board taped-face-down, flat on the bench.
2. Syringe the tinted epoxy into each hole **from the back**. Overfill slightly — epoxy shrinks a
   little as it cures, and the excess sits on the back where nobody sees it.
3. Pop rising bubbles by **exhaling across the surface** (CO₂ collapses them) or a fast flick of a
   lighter flame. **Not a heat gun** — it will lift your tape and cause a leak.
4. Cure 24 h at room temperature. Don't move the board.

### Step 6: Finish the face
1. Peel the tape. The front face is already flat and glossy — the tape moulded it. There are **no
   raised bumps to sand flush** on this face; the overfill is all on the back.
2. If there's a slight lip or tape texture: **flat sanding block only**, 400 grit, light pressure.
   No orbital sander — cured epoxy is softer than walnut and an orbital will dish the plug concave.
3. Scuff each plug face with **220 grit** on purpose. The frosted finish it leaves measurably improves
   diffusion — this is a feature, not a defect.
4. Back side: knock the overfill down flush-ish with 150 grit so the LED can seat. Cosmetics don't
   matter here.

### Step 7: Install the LEDs
1. **Option A:** seat the emitter in the 10 mm pocket, held **2–3 mm off** the back of the plug (a
   punched foam or card spacer ring does this and doubles as a light dam between neighbouring dots).
2. **Option B:** mount the emitter flat against the back face, centred on the hole.
3. Secure with hot glue or small clips. Light-dam between adjacent indicators if they're close — walnut
   is dark but 2 mm of it is not opaque.

---

## Sources
- [TotalBoat Table Top epoxy, 1 qt — Home Depot](https://www.homedepot.com/p/TOTALBOAT-Table-Top-1-qt-Kit-Clear-High-gloss-Epoxy-Interior-Exterior-Resin-For-Bar-Counter-and-Table-Top-Application-409334/332248285)
- [Zinsser SealCoat dewaxed shellac — Home Depot](https://www.homedepot.com/p/Zinsser-1-qt-SealCoat-Universal-Sanding-Sealer-Case-of-6-854/205140204)
- [Milescraft metric brad point set (5 mm + 10 mm) — Home Depot](https://www.homedepot.com/p/Milescraft-Metric-Brad-Point-Bit-Set-7-Piece-2318/206520373)
- [Home Depot 2-part epoxy resin category](https://www.homedepot.com/b/Paint-Paint-Supplies-Epoxy-Epoxy-Resin/2-Part/N-5yc1vZ2fkpf2zZ1z1cs9t)
- [Home Depot forstner bits](https://www.homedepot.com/b/Tools-Power-Tool-Accessories-Drill-Bits-Forstner-Bits/N-5yc1vZc90r)
