"""Independent re-check of sensor.kicad_pcb.  Run after pcb_build.py:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_check.py

Deliberately shares NOTHING with the builder's in-memory model: it loads the
saved board, re-exports the netlist from the schematic, and re-derives the
board outline from Edge.Cuts, then re-tests every promise the build makes --
part clearance, edge clearance, which side each sensor is on, pad->net
assignment, mounting holes, the gas sensor's thermal island, and the BOM the
assembly house will be handed (stamp_bom.py's fields and DNP flags, checked
against the schematic, not against parts_db).  A pass here means the file on
disk is right, not just the script that wrote it.
"""
import itertools
import math
import os
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

MIN_GAP = 0.22          # requirement 1
PAD_EDGE = 0.5          # copper to board edge
CRTYD_EDGE = 0.35       # courtyard to board edge
FRONT_PARTS = {"U2", "U3"}
BACK_PARTS = {"J1"}
BOM_FIELDS = ("MPN", "Manufacturer", "Package", "Description")

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

    print("\n--- part clearance (courtyard, floor {:.2f} mm) ---".format(MIN_GAP))
    worst = []
    for (r1, f1), (r2, f2) in itertools.combinations(fps.items(), 2):
        if not set(sides(f1)) & set(sides(f2)):
            continue
        g = gap(boxes[r1], boxes[r2])
        worst.append((g, r1, r2))
    worst.sort()
    check(worst[0][0] >= MIN_GAP,
          f"tightest pair {worst[0][1]}<->{worst[0][2]} = {worst[0][0]:.3f} mm")
    print("     next: " + ", ".join(f"{a}/{b} {g:.2f}" for g, a, b in worst[1:6]))

    print("\n--- board edge ---")
    bad = [(r, outside(boxes[r], ol, CRTYD_EDGE)) for r in fps]
    bad = [(r, o) for r, o in bad if o > 0.0005]
    check(not bad, f"every courtyard >= {CRTYD_EDGE} mm inside the outline"
                   f"{'' if not bad else ' -- ' + str(bad)}")
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
    check(len(zones) == 2 and all(z.GetNetname() == "GND" for z in zones),
          f"{len(zones)} GND pours ({[z.GetZoneName() for z in zones]})")

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
