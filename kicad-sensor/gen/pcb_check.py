"""Independent re-check of sensor.kicad_pcb.  Run after pcb_build.py:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_check.py

Deliberately shares NOTHING with the builder's in-memory model: it loads the
saved board, re-exports the netlist from the schematic, and re-derives the
board outline from Edge.Cuts, then re-tests every promise the board makes --
part clearance, edge clearance, which side each sensor is on, pad->net
assignment, mounting holes, the gas sensor's thermal island, zone stitching,
the optical window, the board's own identity, and the BOM the assembly house
will be handed (stamp_bom.py's fields and DNP flags, checked against the
schematic, not against parts_db).  A pass here means the file on disk is
right, not just the script that wrote it.

**Re-baselined 2026-08-07 for v0.2** (../REVIEW.md #6/#9/#15).  It used to
encode `pcb_build.py`'s *generated* 2-layer placement; the board has been
4-layer, hand-placed and hand-routed since 2026-08-02, so four checks were
failing against a board that is fine.  What changed, and why:

  * **4 GND pours, 4 layers** -- was "2 GND pours" (F/B only).
  * **Part clearance is now judged on copper, not courtyards.**  A 0603's
    KiCad courtyard is its pad box + 0.28 mm, so two 0603s 0.65 mm apart in
    copper report a 0.09 mm "courtyard gap".  Nothing is fabricated from a
    courtyard: the number that decides bridging, tombstoning and rework room
    is pad-to-pad copper, and MIN_COPPER checks it.  The courtyard floor is
    still reported, with the pairs that sit under it listed by name in
    COURTYARD_TIGHT -- each one measured and accepted, not waived in bulk.
  * **Mounting holes are checked at the hole wall, not the washer.**
    `MountingHole_2.2mm_M2`'s courtyard is a Ø4.95 disc modelling a screw
    head + washer, and on a 30 x 16 mm board with the holes in opposite
    corners it necessarily overhangs the rounded corner.  The hole wall is
    1.90 mm from the edge and both holes are NPTH with nothing inside their
    keepout, so what is actually checked now is the drill, the wall clearance
    and the emptiness -- and the accepted washer overhang is printed.
  * Three checks were ADDED for things that did bite during the v0.2 pass:
    every zone island is stitched (a re-route left one orphaned), no F.Silk
    inside the optical window, and board identity (PCB title block == the
    schematic's == the F.Silk revision legend).
"""
import itertools
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile

import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
SENSOR_DIR = os.path.dirname(HERE)
PCB = os.path.join(SENSOR_DIR, "sensor.kicad_pcb")
SCH = os.path.join(SENSOR_DIR, "sensor.kicad_sch")
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
sys.path.insert(0, os.path.abspath(os.path.join(SENSOR_DIR, "..", "kicad", "gen")))
from sexp import Node, parse  # noqa: E402

MIN_GAP = 0.22          # requirement 1, courtyard to courtyard
MIN_COPPER = 0.30       # pad copper to pad copper -- the number that decides
                        # bridging and rework room (DRC's own rule is 0.22)
PAD_EDGE = 0.5          # copper to board edge
CRTYD_EDGE = 0.35       # courtyard to board edge
HOLE_WALL_EDGE = 1.0    # NPTH wall to board edge (they are 1.90 mm)
LAYERS = 4              # F / In1 / In2 / B, 1.6 mm
GND_POURS = {"GND_F", "GND_In1", "GND_In2", "GND_B"}
FRONT_PARTS = {"U2", "U3"}
BACK_PARTS = {"J1"}
BOM_FIELDS = ("MPN", "Manufacturer", "Package", "Description")

# Enclosure apertures on User.Drawings, as (x, y, r).  Nothing on the silk
# layer of the sensing face may sit inside the optical one: white silk is a
# diffuse reflector and U3 looks straight through this window.
WINDOW = (11.5, 3.6, 2.0)

# Courtyard pairs that sit under MIN_GAP and are accepted, each with the real
# copper gap that makes it safe.  A pair only belongs here once it has been
# measured -- the check re-measures the copper and fails if any of them has
# moved closer than MIN_COPPER.
COURTYARD_TIGHT = {
    frozenset(("C1", "J1")): "1.24 mm of copper; C1 clears J1's pin 1 by more "
                             "than an iron tip needs, and J1 is hand-soldered",
    frozenset(("H1", "TP2")): "M2 washer keepout vs a test pad: 1.90 mm of "
                              "copper, and no fastener sits on a bare pad",
    frozenset(("R9", "C8")): "0.60 mm of copper between two 0603s",
    frozenset(("C9", "TP1")): "0.85 mm of copper",
    frozenset(("R7", "C6")): "0.85 mm of copper",
    frozenset(("R4", "R3")): "0.65 mm of copper; both are the address-strap "
                             "pair, only one of which is ever fitted",
}

M = pcbnew.ToMM
fails = []
notes = []


def check(ok, msg):
    (notes if ok else fails).append(msg)
    print(f"  [{'OK ' if ok else 'FAIL'}] {msg}")


# ---------------------------------------------------------------- geometry
def bbox(fp):
    xs0, ys0, xs1, ys1 = [], [], [], []
    for p in fp.Pads():
        b = p.GetBoundingBox()
        xs0.append(b.GetLeft()); xs1.append(b.GetRight())
        ys0.append(b.GetTop()); ys1.append(b.GetBottom())
    for d in fp.GraphicalItems():
        if d.GetLayer() in (pcbnew.F_CrtYd, pcbnew.B_CrtYd):
            b = d.GetBoundingBox()
            xs0.append(b.GetLeft()); xs1.append(b.GetRight())
            ys0.append(b.GetTop()); ys1.append(b.GetBottom())
    return (M(min(xs0)), M(min(ys0)), M(max(xs1)), M(max(ys1)))


def gap(a, b):
    dx = max(a[0], b[0]) - min(a[2], b[2])
    dy = max(a[1], b[1]) - min(a[3], b[3])
    if dx < 0 and dy < 0:
        return max(dx, dy)
    if dx < 0:
        return dy
    if dy < 0:
        return dx
    return math.hypot(dx, dy)


def edge_distance(board, x, y):
    """Shortest distance from (x, y) to the real Edge.Cuts boundary.

    Not the bounding box, and not `hypot(hole, arc_centre) - arc_r` either:
    a rounded corner curves AWAY from a part sitting inboard of it, so that
    shortcut under-reports (it is what made the old check flag H1/H2 on a
    board where the M2 washer keepout is in fact 0.5 mm clear).  Segments are
    exact; arcs are sampled."""
    best = 1e9
    for d in board.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if d.GetShape() == pcbnew.SHAPE_T_ARC:
            c, r = d.GetCenter(), M(d.GetRadius())
            cx, cy = M(c.x), M(c.y)
            a0 = math.atan2(M(d.GetStart().y) - cy, M(d.GetStart().x) - cx)
            a1 = math.atan2(M(d.GetEnd().y) - cy, M(d.GetEnd().x) - cx)
            if a1 < a0:
                a1 += 2 * math.pi
            for i in range(65):
                a = a0 + (a1 - a0) * i / 64
                best = min(best, math.hypot(cx + r * math.cos(a) - x,
                                            cy + r * math.sin(a) - y))
        else:
            x1, y1 = M(d.GetStart().x), M(d.GetStart().y)
            x2, y2 = M(d.GetEnd().x), M(d.GetEnd().y)
            dx, dy = x2 - x1, y2 - y1
            t = 0.0 if dx == dy == 0 else max(0.0, min(
                1.0, ((x - x1) * dx + (y - y1) * dy) / (dx * dx + dy * dy)))
            best = min(best, math.hypot(x1 + t * dx - x, y1 + t * dy - y))
    return best


def in_circle(item, cx, cy, r):
    """Does any corner of `item`'s bounding box fall inside the circle?"""
    b = item.GetBoundingBox()
    return any(math.hypot(x - cx, y - cy) < r
               for x in (M(b.GetLeft()), M(b.GetRight()))
               for y in (M(b.GetTop()), M(b.GetBottom())))


def pad_box(p):
    b = p.GetBoundingBox()
    return (M(b.GetLeft()), M(b.GetTop()), M(b.GetRight()), M(b.GetBottom()))


def copper_gap(f1, f2):
    """Closest approach between any pad of f1 and any pad of f2, in copper.
    This -- not the courtyard -- is what decides solder bridges, tombstoning
    and whether a rework iron can get in."""
    return min((gap(pad_box(p), pad_box(q))
                for p in f1.Pads() for q in f2.Pads()), default=1e9)


def is_thru(fp):
    return any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH)
               for p in fp.Pads())


def sides(fp):
    return ("F", "B") if is_thru(fp) else \
        ("F",) if fp.GetLayerName() == "F.Cu" else ("B",)


def outline_from_file(board):
    """Board edge re-derived from the saved Edge.Cuts geometry: extents plus
    the corner arc centres/radius, so the containment test is the real
    rounded rectangle."""
    xs, ys, arcs = [], [], []
    for d in board.GetDrawings():
        if d.GetLayer() != pcbnew.Edge_Cuts:
            continue
        if d.GetShape() == pcbnew.SHAPE_T_ARC:
            c = d.GetCenter()
            arcs.append((M(c.x), M(c.y), M(d.GetRadius())))
            for p in (d.GetStart(), d.GetEnd()):
                xs.append(M(p.x)); ys.append(M(p.y))
        elif d.GetShape() == pcbnew.SHAPE_T_SEGMENT:
            for p in (d.GetStart(), d.GetEnd()):
                xs.append(M(p.x)); ys.append(M(p.y))
        else:
            fails.append(f"unexpected Edge.Cuts shape {d.GetShape()}")
    return min(xs), min(ys), max(xs), max(ys), arcs


def outside(rect, ol, margin):
    x0, y0, x1, y1 = ol[:4]
    out = max(0.0, (x0 + margin) - rect[0], (y0 + margin) - rect[1],
              rect[2] - (x1 - margin), rect[3] - (y1 - margin))
    for cx, cy, r in ol[4]:
        px = rect[0] if cx < (x0 + x1) / 2 else rect[2]
        py = rect[1] if cy < (y0 + y1) / 2 else rect[3]
        if ((px < cx) if cx < (x0 + x1) / 2 else (px > cx)) and \
           ((py < cy) if cy < (y0 + y1) / 2 else (py > cy)):
            out = max(out, math.hypot(px - cx, py - cy) - (r - margin))
    return out


# ---------------------------------------------------------------- netlist
def components(root):
    """{ref: (value, {field: text}, {flag})} straight from the schematic --
    `flag` is KiCad's own `dnp` / `exclude_from_bom` / `exclude_from_pos_files`
    marker, which is what a BOM exporter acts on."""
    out = {}
    for c in root.find("components").findall("comp"):
        ref = str(c.find("ref")[1])
        fields = {}
        for f in (c.find("fields") or Node()).findall("field"):
            name = str(f.find("name")[1])
            fields[name] = str(f[2]) if len(f) > 2 else ""
        flags = {str(p.find("name")[1]) for p in c.findall("property")
                 if p.find("value") is None}
        out[ref] = (str(c.find("value")[1]), fields, flags)
    return out


def netlist():
    """Fresh export into a temp dir -- never reuse the builder's copy, and
    leave no artefact behind.  -> ({(ref, pin): net}, {ref: component})"""
    tmp = tempfile.mkdtemp()
    try:
        out = os.path.join(tmp, "check.net")
        subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format",
                        "kicadsexpr", "-o", out, SCH], check=True,
                       capture_output=True)
        root = parse(open(out, encoding="utf-8").read())
    finally:
        shutil.rmtree(tmp, ignore_errors=True)
    comps = components(root)
    mapping = {}
    for net in root.find("nets").findall("net"):
        name = None
        for c in net[1:]:
            if getattr(c, "tag", None) == "name":
                name = str(c[1])
        for node in net.findall("node"):
            ref = pin = None
            for c in node[1:]:
                if getattr(c, "tag", None) == "ref":
                    ref = str(c[1])
                elif getattr(c, "tag", None) == "pin":
                    pin = str(c[1])
            if ref and pin:
                mapping[(ref, pin)] = name
    return mapping, comps


def main():
    board = pcbnew.LoadBoard(PCB)
    fps = {f.GetReference(): f for f in board.GetFootprints()}
    boxes = {r: bbox(f) for r, f in fps.items()}
    ol = outline_from_file(board)
    w, h = ol[2] - ol[0], ol[3] - ol[1]

    print(f"\n=== sensor.kicad_pcb: {len(fps)} footprints, "
          f"{w:.1f} x {h:.1f} mm, {board.GetCopperLayerCount()} layers, "
          f"{M(board.GetDesignSettings().GetBoardThickness()):.2f} mm ===")

    print("\n--- requirements ---")
    check(board.GetCopperLayerCount() == LAYERS,
          f"{LAYERS} copper layers ({board.GetCopperLayerCount()})")
    for ref in FRONT_PARTS:
        check(fps[ref].GetLayerName() == "F.Cu",
              f"{ref} on the FRONT ({fps[ref].GetLayerName()})")
    for ref in BACK_PARTS:
        check(fps[ref].GetLayerName() == "B.Cu",
              f"{ref} on the BACK ({fps[ref].GetLayerName()})")
    holes = {r: f for r, f in fps.items()
             if all(p.GetAttribute() == pcbnew.PAD_ATTRIB_NPTH for p in f.Pads())
             and f.Pads()}
    check(len(holes) == 2, f"2 mounting holes ({sorted(holes)})")
    for r, f in holes.items():
        d = M(list(f.Pads())[0].GetDrillSizeX())
        check(abs(d - 2.2) < 0.01, f"{r} drill {d:.2f} mm (M2)")

    print(f"\n--- part clearance (copper floor {MIN_COPPER:.2f} mm, "
          f"courtyard floor {MIN_GAP:.2f} mm) ---")
    pairs, tight = [], []
    for (r1, f1), (r2, f2) in itertools.combinations(fps.items(), 2):
        if not set(sides(f1)) & set(sides(f2)):
            continue
        g, c = gap(boxes[r1], boxes[r2]), copper_gap(f1, f2)
        pairs.append((g, c, r1, r2))
        if g < MIN_GAP - 1e-6:        # 0.220 mm lands on 0.21999... in float
            tight.append((g, c, r1, r2))
    pairs.sort()
    tight.sort()

    # Copper is the hard rule, on every pair -- tight courtyard or not.
    thin = [(round(c, 3), r1, r2) for _, c, r1, r2 in pairs if c < MIN_COPPER]
    check(not thin, f"every pad-to-pad copper gap >= {MIN_COPPER} mm "
                    f"(tightest {min(c for _, c, _, _ in pairs):.3f} mm)"
                    f"{'' if not thin else ' -- ' + str(thin)}")

    # A courtyard under the floor is allowed only for a pair on the list.
    unlisted = [(round(g, 3), r1, r2) for g, _, r1, r2 in tight
                if frozenset((r1, r2)) not in COURTYARD_TIGHT]
    check(not unlisted,
          f"all {len(tight)} sub-{MIN_GAP} mm courtyard pairs are accepted in "
          f"COURTYARD_TIGHT{'' if not unlisted else ' -- NEW: ' + str(unlisted)}")
    for g, c, r1, r2 in tight:
        why = COURTYARD_TIGHT.get(frozenset((r1, r2)), "NOT ON THE LIST")
        print(f"     accepted {r1:4s}<->{r2:4s} courtyard {g:.3f}, "
              f"copper {c:.2f} mm — {why}")
    rest = [(g, r1, r2) for g, _, r1, r2 in pairs
            if frozenset((r1, r2)) not in COURTYARD_TIGHT][:5]
    print("     tightest courtyards otherwise: " +
          ", ".join(f"{a}/{b} {g:.2f}" for g, a, b in rest))

    print("\n--- board edge ---")
    # Mounting holes are judged at the hole wall: their footprint courtyard is
    # a Ø4.95 screw-head/washer disc, which on a 30 x 16 mm board with the
    # holes in opposite corners necessarily overhangs the corner radius.
    bad = [(r, round(outside(boxes[r], ol, CRTYD_EDGE), 3))
           for r in fps if r not in holes]
    bad = [(r, o) for r, o in bad if o > 0.0005]
    check(not bad, f"every part courtyard >= {CRTYD_EDGE} mm inside the "
                   f"outline{'' if not bad else ' -- ' + str(bad)}")
    wall, washer = [], []
    for r, f in sorted(holes.items()):
        p = list(f.Pads())[0]
        c = (M(p.GetPosition().x), M(p.GetPosition().y))
        d = edge_distance(board, *c)              # true distance, arcs and all
        wall.append((r, round(d - M(p.GetDrillSizeX()) / 2, 2)))
        washer.append((r, round(d - (boxes[r][2] - boxes[r][0]) / 2, 2)))
    check(all(v >= HOLE_WALL_EDGE for _, v in wall),
          f"both NPTH walls >= {HOLE_WALL_EDGE} mm from the outline {wall}")
    check(all(v >= 0 for _, v in washer),
          f"the Ø{boxes['H1'][2] - boxes['H1'][0]:.2f} mm screw-head/washer "
          f"keepout stays on the board {washer}")
    inside_keepout = [r for r in fps if r not in holes
                      and any(gap(boxes[r], boxes[h]) < 0 for h in holes)]
    check(not inside_keepout, f"no part inside either fastener keepout"
                              f"{'' if not inside_keepout else ' -- ' + str(inside_keepout)}")
    padbad = []
    for r, f in fps.items():
        for p in f.Pads():
            b = p.GetBoundingBox()
            o = outside((M(b.GetLeft()), M(b.GetTop()),
                         M(b.GetRight()), M(b.GetBottom())), ol, PAD_EDGE)
            if o > 0.0005:
                padbad.append(f"{r}.{p.GetNumber()} {o:.2f}")
    check(not padbad, f"every pad >= {PAD_EDGE} mm inside the outline"
                      f"{'' if not padbad else ' -- ' + str(padbad)}")

    print("\n--- pad -> net (against a fresh netlist export) ---")
    nl, comps = netlist()
    wrong, missing = [], []
    for (ref, pin), want in nl.items():
        fp = fps.get(ref)
        if fp is None:
            missing.append(f"{ref} not on the board")
            continue
        pads = [p for p in fp.Pads() if p.GetNumber() == pin]
        if not pads:
            missing.append(f"{ref}.{pin} has no pad")
            continue
        for p in pads:
            got = p.GetNetname()
            if got != want:
                wrong.append(f"{ref}.{pin}: board '{got}' != schematic '{want}'")
    check(not missing, f"every schematic pin has a pad ({len(nl)} pins)"
                       f"{'' if not missing else ' -- ' + str(missing)}")
    check(not wrong, f"every pad carries its schematic net"
                     f"{'' if not wrong else ' -- ' + str(wrong[:5])}")
    orphan = [f"{r}.{p.GetNumber()}" for r, f in fps.items() for p in f.Pads()
              if p.GetNetname() == "" and p.GetAttribute() != pcbnew.PAD_ATTRIB_NPTH]
    check(not orphan, f"no netless copper pad{'' if not orphan else ' -- ' + str(orphan)}")

    print("\n--- gas sensor thermal island ---")
    ka = [z for z in board.Zones() if z.GetIsRuleArea()]
    check(len(ka) == 1, f"one copper-pour keepout ({len(ka)})")
    if ka:
        b = ka[0].GetBoundingBox()
        kr = (M(b.GetLeft()), M(b.GetTop()), M(b.GetRight()), M(b.GetBottom()))
        u2 = boxes["U2"]
        inside = (kr[0] <= u2[0] and kr[1] <= u2[1]
                  and kr[2] >= u2[2] and kr[3] >= u2[3])
        check(inside, f"it encloses U2 (keepout {kr}, U2 {u2})")
        behind = [r for r, f in fps.items()
                  if r != "U2" and "B" in sides(f) and gap(boxes[r], u2) < 0]
        check(not behind, f"nothing on the back behind U2 {behind if behind else ''}")
    zones = [z for z in board.Zones() if not z.GetIsRuleArea()]
    names = {z.GetZoneName() for z in zones}
    check(names == GND_POURS and all(z.GetNetname() == "GND" for z in zones),
          f"{len(zones)} GND pours, one per layer ({sorted(names)})")

    # Every filled island must own a pad or a via of its net.  A hand re-route
    # can seal one off without DRC noticing until the next fill (it happened
    # on 2026-08-07: a 0.3 mm2 pocket beside C4 swallowed a GND stub).
    print("\n--- zone stitching ---")
    orphans, islands = [], 0
    for z in zones:
        for lay in z.GetLayerSet().Seq():
            poly = z.GetFilledPolysList(lay)
            anchors = [(M(p.GetPosition().x), M(p.GetPosition().y))
                       for f in fps.values() for p in f.Pads()
                       if p.GetNetname() == z.GetNetname() and p.IsOnLayer(lay)]
            anchors += [(M(t.GetPosition().x), M(t.GetPosition().y))
                        for t in board.GetTracks()
                        if t.GetClass() == "PCB_VIA"
                        and t.GetNetname() == z.GetNetname()]
            for i in range(poly.OutlineCount()):
                islands += 1
                o = poly.Outline(i)
                if not any(o.PointInside(pcbnew.VECTOR2I(pcbnew.FromMM(x),
                                                         pcbnew.FromMM(y)))
                           for x, y in anchors):
                    b = o.BBox()
                    orphans.append(f"{z.GetZoneName()}/{board.GetLayerName(lay)}"
                                   f" island at ({M(b.GetLeft()):.1f},"
                                   f"{M(b.GetTop()):.1f})")
    check(not orphans, f"all {islands} filled islands hold a pad or via of "
                       f"their net{'' if not orphans else ' -- ' + str(orphans)}")

    print("\n--- optical window ---")
    wx, wy, wr = WINDOW
    inside_win = []
    for d in board.GetDrawings():
        if d.GetLayer() == pcbnew.F_SilkS and in_circle(d, wx, wy, wr):
            inside_win.append(repr(d.GetText()) if d.GetClass() == "PCB_TEXT"
                              else d.GetClass())
    for r, f in fps.items():
        for g in list(f.GraphicalItems()) + [f.Reference(), f.Value()]:
            if g.GetLayer() != pcbnew.F_SilkS:
                continue
            if hasattr(g, "IsVisible") and not g.IsVisible():
                continue              # hidden fields print nothing
            if in_circle(g, wx, wy, wr):
                inside_win.append(f"{r} {g.GetClass()}")
    check(not inside_win,
          f"no F.Silk inside the Ø{wr * 2:.1f} mm window over U3"
          f"{'' if not inside_win else ' -- ' + str(inside_win)}")
    on_front = [f"{r}.{p.GetNumber()}" for r, f in fps.items() if r != "U3"
                for p in f.Pads() if p.IsOnLayer(pcbnew.F_Cu)
                and math.hypot(M(p.GetPosition().x) - wx,
                               M(p.GetPosition().y) - wy) < wr]
    check(not on_front, f"nothing but U3 under the window"
                        f"{'' if not on_front else ' -- ' + str(on_front)}")

    print("\n--- board identity ---")
    tb = board.GetTitleBlock()
    sch_tb = dict(re.findall(r'\((title|date|rev) "([^"]*)"\)',
                             open(SCH, encoding="utf-8").read()[:2000]))
    silk = [d.GetText() for d in board.GetDrawings()
            if d.GetClass() == "PCB_TEXT" and d.GetLayer() == pcbnew.F_SilkS
            and d.GetText().startswith("SENSE ")]
    check(bool(tb.GetTitle() and tb.GetRevision() and tb.GetDate()),
          f"the PCB has a title block ({tb.GetTitle()!r}, rev "
          f"{tb.GetRevision()!r}, {tb.GetDate()!r})")
    check(tb.GetRevision() == sch_tb.get("rev")
          and tb.GetDate() == sch_tb.get("date")
          and tb.GetTitle() == sch_tb.get("title"),
          f"it matches the schematic's ({sch_tb.get('title')!r}, rev "
          f"{sch_tb.get('rev')!r}, {sch_tb.get('date')!r})")
    check(len(silk) == 1 and silk[0] == f"SENSE {tb.GetRevision()}",
          f"the F.Silk revision legend agrees ({silk})")
    invisible = sorted(r for r, f in fps.items()
                       if not f.Reference().IsVisible())
    check(invisible == ["H1", "H2"],
          f"every part's reference is on the silk except the mounting holes "
          f"({invisible})")

    print("\n--- BOM (fields + flags, board vs schematic) ---")
    on_bom = {r: f for r, f in fps.items()
              if not f.GetAttributes() & pcbnew.FP_EXCLUDE_FROM_BOM}
    blank = [f"{r}.{n}" for r, f in sorted(on_bom.items()) for n in BOM_FIELDS
             if not f.GetFieldsShownText().get(n, "").strip()]
    check(not blank, f"all {len(on_bom)} BOM parts carry {'/'.join(BOM_FIELDS)}"
                     f"{'' if not blank else ' -- ' + str(blank[:8])}")
    drift = []
    for r, f in sorted(on_bom.items()):
        if r not in comps:                      # mounting holes: board-only
            continue
        value, fields, _ = comps[r]
        got = f.GetFieldsShownText()
        if got.get("Value", "") != value:
            drift.append(f"{r}.Value {got.get('Value')!r} != {value!r}")
        for n in BOM_FIELDS:
            if got.get(n, "") != fields.get(n, ""):
                drift.append(f"{r}.{n} {got.get(n)!r} != {fields.get(n)!r}")
    check(not drift, f"board fields match the schematic's"
                     f"{'' if not drift else ' -- ' + str(drift[:5])}")

    sch_dnp = {r for r, c in comps.items() if "dnp" in c[2]}
    pcb_dnp = {r for r, f in fps.items() if f.IsDNP()}
    said = {r for r, c in comps.items() if "DNP" in c[0].upper()}
    check(sch_dnp == pcb_dnp, f"same do-not-populate set in both files "
                              f"(sch {sorted(sch_dnp)}, pcb {sorted(pcb_dnp)})")
    check(said == sch_dnp, f"every '(DNP)' value string is a real DNP flag "
                           f"(said {sorted(said)}, flagged {sorted(sch_dnp)})")
    sch_off = {r for r, c in comps.items() if "exclude_from_bom" in c[2]}
    pcb_off = {r for r in comps
               if r in fps and fps[r].GetAttributes() & pcbnew.FP_EXCLUDE_FROM_BOM}
    check(sch_off == pcb_off, f"same off-BOM set in both files "
                              f"(sch {sorted(sch_off)}, pcb {sorted(pcb_off)})")

    print(f"\n=== {len(fails)} FAILURES, {len(notes)} checks passed ===")
    for f in fails:
        print("  FAIL:", f)
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
