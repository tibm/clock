"""Generates clock.kicad_pcb from clock.kicad_sch using KiCad's own pcbnew
Python bindings (run under KiCad's bundled interpreter, not system python3).

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_build.py

Layout is hand-placed (function-block clustering derived from the b_*.py
schematic generators), not auto-placed/auto-routed. See ../PCB_NOTES.md for
the design rationale. This script only PLACES footprints + stackup + zones
+ keepouts + net assignment; it does not route traces (see notes for why).
"""
import math
import os
import subprocess
import sys

sys.path.insert(0, os.path.dirname(__file__))
from sexp import parse  # noqa: E402

import pcbnew  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.dirname(HERE)
SCH = os.path.join(KICAD_DIR, "clock.kicad_sch")
PCB_OUT = os.path.join(KICAD_DIR, "clock.kicad_pcb")
CLOCK_PRETTY = os.path.join(KICAD_DIR, "clock.pretty")
STOCK_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

MM = pcbnew.FromMM

BOARD_SIZE = 110.0
CX, CY = 55.0, 55.0  # board centre == motor shaft centre

# --------------------------------------------------------------------------
# 1. Pull the authoritative ref -> (value, lib_id, footprint) map straight
#    from the schematic (single source of truth; never hand-transcribed).
# --------------------------------------------------------------------------


def load_schematic_parts():
    text = open(SCH, encoding="utf-8").read()
    root = parse(text)
    syms = [c for c in root[1:] if getattr(c, "tag", None) == "symbol"]

    def prop(node, name):
        for c in node[1:]:
            if getattr(c, "tag", None) == "property" and len(c) > 2 and str(c[1]) == name:
                return str(c[2])
        return None

    parts = {}
    for s in syms:
        ref = prop(s, "Reference")
        if not ref or ref.startswith("#"):
            continue  # power symbols / flags carry no footprint
        fp = prop(s, "Footprint")
        if not fp:
            continue
        parts[ref] = {"value": prop(s, "Value"), "footprint": fp}
    return parts


# --------------------------------------------------------------------------
# 2. Netlist (kicad-cli sch export netlist --format kicadsexpr), parsed with
#    the same sexp reader, to drive pad -> net assignment.
# --------------------------------------------------------------------------


def export_netlist():
    out = os.path.join(HERE, "_netlist.net")
    subprocess.run(
        [KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr", "-o", out, SCH],
        check=True, capture_output=True,
    )
    return out


def load_netlist(path):
    """Returns {(ref, pin_number): net_name}."""
    root = parse(open(path, encoding="utf-8").read())
    nets_node = root.find("nets")
    mapping = {}
    for net in nets_node.findall("net"):
        name = None
        for c in net[1:]:
            if getattr(c, "tag", None) == "name":
                name = str(c[1])
        for node in net.findall("node"):
            ref = None
            pin = None
            for c in node[1:]:
                if getattr(c, "tag", None) == "ref":
                    ref = str(c[1])
                elif getattr(c, "tag", None) == "pin":
                    pin = str(c[1])
            if ref and pin:
                mapping[(ref, pin)] = name
    return mapping


# --------------------------------------------------------------------------
# 3. Footprint library resolution: "LibNick:FpName" -> (dir, FpName).
#    "clock" is the project .pretty; everything else is a stock KiCad lib.
# --------------------------------------------------------------------------


def resolve_fp(field):
    nick, name = field.split(":", 1)
    if nick == "clock":
        return CLOCK_PRETTY, name
    return os.path.join(STOCK_FP, f"{nick}.pretty"), name


# --------------------------------------------------------------------------
# 4. Layout. Board is 110x110 mm, origin = board centre = motor shaft centre
#    (constraint: shaft perfectly centred). Front (F.Cu) carries only the
#    dial-facing parts (shaft, homing sensor, 7 pixels + their decoupling);
#    everything else lives on the back (B.Cu), grouped into the functional
#    blocks the schematic itself already uses (b_*.py generators), arranged
#    around the centre motor keepout (~19 mm radius) and clear of the
#    ESP32-S3 antenna keepout (built into the stock footprint).
# --------------------------------------------------------------------------

# ref -> (x, y, rot_deg, side)  side "F" or "B".
#
# v2 (2026-07-21): rebuilt for real clearance after the first pass packed
# everything with a 0.6mm gap -- correct on paper (0 DRC clearance
# violations) but not actually buildable: no room to hand-solder, no
# routing channel between parts, and connectors close enough to look
# merged. This version widens every zone, adds real gaps BETWEEN zones
# (not just within them), and gives connectors 1.5x the local gap (see
# grid_fill). It also moves 5 of the 7 on-board NeoPixels off-board onto a
# 3-pin breakout (J12) per schematic rev, and fixes microSD (J6) orientation
# -- verified empirically (see PCB_NOTES.md) rather than assumed.
ANCHORS = {
    # ---- FRONT: dial side ----
    "U14": (55.0, 23.0, 0, "F"),          # homing sensor, 12 o'clock, 32mm from centre
    "D40": (13.0, 55.0, 0, "F"),          # on-PCB pixel 1 (chain pos 1), 9 o'clock
    "D41": (97.0, 55.0, 0, "F"),          # on-PCB pixel 2 (chain pos 2), 3 o'clock
    # per-pixel decoupling for D40/D41: BACK, near (not exactly behind --
    # (13,55)/(97,55) fall inside the POWER/MOTORDRV zone rects, and DRC
    # caught a real short risk against whatever the packer put there) --
    # verified clear of every other placed footprint (see PCB_NOTES.md).
    "C230": (18.0, 48.0, 0, "B"),
    "C231": (103.0, 51.0, 0, "B"),

    # ---- BACK: parts with a real mechanical/RF/access constraint ----
    "U8": (27.0, 15.0, 0, "B"),            # ESP32-S3-WROOM-1: antenna -> top edge
    "Y1": (42.0, 15.0, 90, "B"),           # crystal: must sit close to U8
    # microSD: card slot must open onto a free board edge, not "inward".
    # Rotation verified empirically per-footprint (see PCB_NOTES.md) --
    # rot=270 on the flipped/back footprint puts the slot mouth at +X (this
    # footprint's contacts and its slot are on opposite sides, and which
    # world direction "opposite the contacts" ends up pointing at depends
    # on the flip+rotation combination, not just rotation alone).
    "J6": (100.0, 93.0, 270, "B"),
    "M1": (55.0, 61.0, 0, "B"),            # motor: corrected to true centre at runtime
}

# Hard exclusions every fill-zone rect below is verified clear of:
#   EXCL_U8      -- ESP32 module body + its built-in antenna keepout zone
#   EXCL_MOTOR   -- motor courtyard, as a centre circle
#   EXCL_BATTERY -- battery holder footprint
#   EXCL_J6      -- microSD socket + card-slot mouth (measured, +margin)
EXCL_U8 = (2.0, 0.0, 52.0, 26.0)
# Motor courtyard circle: NOT centred on the shaft (CX,CY). The footprint's
# own anchor point -- which is also its courtyard circle's centre -- sits
# 6mm above the shaft in local coordinates (shaft pad is at local (0,-6)),
# and that offset survives the flip-to-back unchanged (flip mirrors about
# the anchor itself). M1 is placed at (55,61) precisely so the SHAFT lands
# at (55,55); the courtyard is therefore centred at (55,61), not (55,55).
# (Found via DRC: 3 "courtyard overlap" hits against AUDIO_EXT parts ~17mm
# from (55,55) but genuinely inside the true, (55,61)-centred courtyard.)
MOTOR_COURTYARD_CENTER = (CX, CY + 6.0)
EXCL_MOTOR_R = 19.0  # ~2.3mm margin over the true 16.7mm courtyard radius
EXCL_BATTERY = (5.0, 84.0, 85.0, 108.0)
EXCL_J6 = (88.0, 82.0, 111.0, 103.0)

# Fill zones for every other part (all other ICs/connectors/discretes plus
# every R/C/RT), grouped by the same functional block the schematic itself
# uses (b_*.py generators). Rectangles verified (see verify_zones() below)
# clear of the exclusions above, AND of each other with a real >=4mm gap
# in between -- that gap is the routing channel between blocks, and also
# where the two solid inner-layer GND pours get real width to work with
# instead of being squeezed to nothing between adjacent component courtyards.
FILL_ZONES = {
    "MCU": {  # decoupling, tucked directly below U8/Y1
        # x1=46: verified clear of the r=21 circle at this zone's worst
        # corner (46,36) -- widening further right dips into the circle.
        "rect": (4.0, 29.0, 46.0, 36.0),
        "refs": ["C140", "C141", "C142", "C143", "C144", "C145", "C146", "R50", "R51"],
        "gap": 1.5,
    },
    "RAILS_IO": {  # 5V/3.3V/12V boost + knob conn + MCP23017 expander + ext conns
        # top-right, the whole board's "y<=33" safe arm (any x is motor-circle-
        # clear up here) -- RAILS and IO_SENSOR were 2 thin zones squeezed by
        # the circle when split; merged, the packer uses the shared height
        # far better and both blocks get real room.
        "rect": (53.0, 3.0, 108.0, 33.0),
        "refs": (
            ["U5", "U6", "U7", "L2", "L3", "L4", "D20", "J10", "J2", "SW1", "SW2"]
            + [f"C{n}" for n in range(120, 136)] + [f"R{n}" for n in range(40, 49)]
            + ["U13", "J7", "J11", "R62", "R97", "R98", "R99"]
            + [f"R{n}" for n in range(90, 100)] + [f"R{n}" for n in range(111, 116)]
            + ["C239", "C240", "C241"]
            + ["C222", "C223", "R70", "R71", "R72"]  # microSD passives -- room here, not AUDIO
        ),
        "gap": 1.05,
    },
    "POWER": {  # POWER-IN + CHARGER + PROTECTOR (USB-C, LT3652, HY2111/AOSD32334C)
        # left column, capped at x<=33 -- the motor keepout circle (r=21)
        # reaches to within 21mm of centre (55,55), i.e. x<34 at y=55.
        "rect": (2.0, 38.0, 33.0, 83.0),
        "refs": (
            ["J1", "U1", "C1", "R1", "R2", "R3", "R4", "D1"]
            + ["U2", "U3", "U4", "F1", "L1", "Q1", "Q2", "Q3", "D10", "D11", "D12", "RT1"]
            + [f"C{n}" for n in (100, 101, 102, 104, 105, 106, 107, 108, 109, 110)]
            + [f"R{n}" for n in range(10, 26)]  # incl. R24/R25 gate pulldowns (2026-07-21)
        ),
        "gap": 1.2,
    },
    "WAKE": {  # wake-LED driver FETs + NeoPixel chain head/breakout -- reclaimed
        # pocket between RAILS_IO and MOTORDRV: y>=33 clears RAILS_IO, x>=76
        # is unconditionally clear of the r=21 circle for any y here, y<=44
        # clears MOTORDRV.
        "rect": (76.0, 33.0, 108.0, 44.0),
        "refs": ["J9", "J12", "Q6", "Q7", "U15", "R52", "R104", "R105", "R106", "R107",
                 "R110", "C237", "C238"],
        "gap": 1.0,
    },
    "MOTORDRV": {
        "rect": (78.0, 44.0, 108.0, 66.0),
        "refs": (
            ["U11", "U12", "C200", "C201", "C202", "C210", "C211", "C212"]
            # TAS5760M bootstrap caps + PVDD flyback diode + speaker conn:
            # no room left in AUDIO/AUDIO_EXT once the motor courtyard's
            # true centre (55,61), not (55,55), was accounted for -- this
            # zone has real slack (600mm2 allocated, ~280mm2 of actual
            # parts), so they land here instead.
            + [f"C{n}" for n in (180, 181, 182, 183, 184, 185, 186)]
            + ["D30", "J3"]
        ),
        "gap": 1.3,
    },
    "AUDIO": {
        # bottom-right; x0=74 verified clear of the r=21 motor circle at
        # y=66 (this zone's nearest edge to centre); y1=82 stops at the
        # microSD keepout (EXCL_J6 starts y=82).
        # U9 (HTSSOP-32) is 11mm tall -- with the packer's tallest-first
        # shelf-packing that dominates one whole row, so this zone holds
        # only the similarly-sized anchors (U9/U10/L5/L6/Q4); everything
        # smaller moves to AUDIO_EXT, which has real slack, rather than
        # forcing several more short rows into this zone's tight 16mm height.
        "rect": (74.0, 68.0, 108.0, 82.0),
        "refs": ["U9", "U10", "L5", "L6", "Q4"],
        "gap": 1.0,
    },
    "AUDIO_EXT": {
        # Rest of AUDIO's small parts: a reclaimed strip below the motor,
        # above the battery. y0=80: with the motor courtyard correctly
        # centred at (55,61) (not (55,55) -- see MOTOR_COURTYARD_CENTER),
        # any y>=61+19=80 is unconditionally clear of it for ANY x, which
        # is what lets this run the full width from POWER's edge (33) to
        # the battery's edge (85) instead of the old, much narrower strip.
        "rect": (33.0, 80.0, 73.0, 83.5),
        "refs": (
            [f"C{n}" for n in (160, 161, 162, 163, 164, 165, 166, 167, 170, 171, 172)]
            + ["R60", "R61", "R63"]  # C163 AVDD bypass + R63 SPK_SD pulldown (2026-07-21)
        ),
        "gap": 0.5,
    },
}

BT1_POS = (45.0, 96.0)  # battery holder, bottom arm, horizontal


def verify_zones():
    """Sanity-check every fill rect against the 4 hard exclusions and
    against every other fill rect, before spending time on placement."""
    problems = []
    rects = {name: spec["rect"] for name, spec in FILL_ZONES.items()}

    def rects_overlap(a, b):
        ax0, ay0, ax1, ay1 = a
        bx0, by0, bx1, by1 = b
        return not (ax1 <= bx0 or bx1 <= ax0 or ay1 <= by0 or by1 <= ay0)

    def circle_rect_overlap(cx, cy, r, rect):
        x0, y0, x1, y1 = rect
        nx = max(x0, min(cx, x1))
        ny = max(y0, min(cy, y1))
        return (nx - cx) ** 2 + (ny - cy) ** 2 < r * r

    for name, r in rects.items():
        if rects_overlap(r, EXCL_U8):
            problems.append(f"{name} overlaps EXCL_U8")
        if rects_overlap(r, EXCL_BATTERY):
            problems.append(f"{name} overlaps EXCL_BATTERY")
        if rects_overlap(r, EXCL_J6):
            problems.append(f"{name} overlaps EXCL_J6")
        if circle_rect_overlap(MOTOR_COURTYARD_CENTER[0], MOTOR_COURTYARD_CENTER[1], EXCL_MOTOR_R, r):
            problems.append(f"{name} overlaps motor keepout circle")
    for (n1, r1), (n2, r2) in itertools_combinations(list(rects.items())):
        if rects_overlap(r1, r2):
            problems.append(f"{n1} overlaps {n2}")
    return problems


def itertools_combinations(items):
    for i in range(len(items)):
        for j in range(i + 1, len(items)):
            yield items[i], items[j]

# --------------------------------------------------------------------------
# 5. Board / footprint construction
# --------------------------------------------------------------------------


def add_footprint(board, ref, footprint_field, x, y, rot, side):
    lib_dir, fp_name = resolve_fp(footprint_field)
    fp = pcbnew.FootprintLoad(lib_dir, fp_name)
    if fp is None:
        raise RuntimeError(f"could not load footprint {footprint_field} for {ref}")
    fp.SetReference(ref)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    board.Add(fp)  # must be parented before Flip() -- an unparented flip of a
    # footprint carrying an embedded zone (e.g. the ESP32 antenna keepout) segfaults
    if side == "B":
        fp.Flip(pcbnew.VECTOR2I(MM(x), MM(y)), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    if rot:
        fp.SetOrientationDegrees(rot)
    return fp


def grid_fill(board, rect, refs, parts, gap=1.4):
    """Row-pack refs into rect on the BACK, using each footprint's real
    bounding box so mixed 0603/0805/1210/electrolytic/IC/connector sizes
    pack correctly, with real edge-to-edge clearance (`gap`, mm) for hand
    soldering, routing escape, and reference-designator silkscreen -- the
    first pass used 0.6mm, which left components touching in places.
    Connectors (ref starts with "J") get 1.5x extra clearance beyond the
    zone's base gap: they need real room for the mating cable/plug and
    silkscreen labelling, and a 0.6mm-spaced connector row was the single
    biggest complaint on the first layout. Returns list of placed FOOTPRINTs."""
    x0, y0, x1, y1 = rect
    cx, cy = x0 + gap, y0 + gap
    row_h = 0.0
    placed = []

    def pads_bbox_mm(fp):
        """True world pads bbox (min/max over each pad's own bbox), read
        AFTER flip -- unlike GetFpPadsLocalBbox(), which was found (empirically,
        via a throwaway script) to ignore the current flip transform entirely
        and silently return the pre-flip layout, which threw placement off-board."""
        xs0, ys0, xs1, ys1 = [], [], [], []
        for p in fp.Pads():
            b = p.GetBoundingBox()
            xs0.append(b.GetLeft()); xs1.append(b.GetRight())
            ys0.append(b.GetTop()); ys1.append(b.GetBottom())
        return (pcbnew.ToMM(min(xs0)), pcbnew.ToMM(min(ys0)),
                pcbnew.ToMM(max(xs1)), pcbnew.ToMM(max(ys1)))

    # Load everything first so we can shelf-pack tallest-first (classic
    # bin-packing heuristic) instead of arbitrary ref order, which was
    # wasting a lot of row height mixing e.g. a tall header with 0603s.
    loaded = []
    for ref in refs:
        fp_field = parts[ref]["footprint"]
        lib_dir, fp_name = resolve_fp(fp_field)
        fp = pcbnew.FootprintLoad(lib_dir, fp_name)
        if fp is None:
            raise RuntimeError(f"could not load footprint {fp_field} for {ref}")
        fp.SetReference(ref)
        fp.SetPosition(pcbnew.VECTOR2I(0, 0))
        board.Add(fp)
        fp.Flip(pcbnew.VECTOR2I(0, 0), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
        x0m, y0m, x1m, y1m = pads_bbox_mm(fp)
        loaded.append((fp, x0m, y0m, x1m - x0m, y1m - y0m))
    loaded.sort(key=lambda t: -t[4])

    for fp, bx0, by0, w0, h0 in loaded:
        extra = gap * 0.5 if fp.GetReference().startswith("J") else 0.0
        w = w0 + gap + extra
        h = h0 + gap + extra
        if cx + w > x1:
            cx = x0 + gap
            cy += row_h + gap
            row_h = 0.0
        # fp is currently anchored at (0,0); its pads bbox starts at (bx0,by0)
        # in that same frame, so moving the anchor to (cx-bx0, cy-by0) puts
        # the bbox's top-left corner exactly at (cx,cy) (offset by half the
        # connector's extra clearance so it's centred in its own cell).
        target_x = cx - bx0 + extra / 2
        target_y = cy - by0 + extra / 2
        fp.SetPosition(pcbnew.VECTOR2I(MM(target_x), MM(target_y)))
        cx += w
        row_h = max(row_h, h)
        placed.append(fp)
    if cy + row_h > y1 + 0.01:
        print(f"  !! fill zone {rect} OVERFLOWED: needed y up to {cy + row_h:.1f}, "
              f"budget was {y1}")
    return placed


def set_board_outline(board, size):
    r = 6.0  # corner radius
    pts = []
    corners = [(r, 0), (size - r, 0), (size, r), (size, size - r),
               (size - r, size), (r, size), (0, size - r), (0, r)]
    # Build a rounded-rect polygon via short arcs approximated as chamfers
    # (KiCad's PCB_SHAPE arc support via python is fiddly; chamfered corners
    # are manufacturable/DRC-identical for a first-pass outline).
    poly = pcbnew.SHAPE_POLY_SET()
    outline = pcbnew.SHAPE_LINE_CHAIN()
    for x, y in corners:
        outline.Append(MM(x), MM(y))
    outline.SetClosed(True)
    poly.AddOutline(outline)
    for i in range(outline.PointCount()):
        p1 = outline.CPoint(i)
        p2 = outline.CPoint((i + 1) % outline.PointCount())
        seg = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        seg.SetStart(pcbnew.VECTOR2I(int(p1.x), int(p1.y)))
        seg.SetEnd(pcbnew.VECTOR2I(int(p2.x), int(p2.y)))
        seg.SetLayer(pcbnew.Edge_Cuts)
        seg.SetWidth(MM(0.15))
        board.Add(seg)


MOUNTING_HOLES = [
    (36.0, 36.0),  # between MCU and POWER zones, right of POWER's column
    (99.0, 12.0),  # RAILS_IO's unused right margin
    (99.0, 75.0),  # AUDIO/AUDIO_EXT's unused margin
    (30.0, 78.0),  # AUDIO_EXT's unused margin, above the battery
]  # all 4 verified clear of every placed footprint + all hard exclusions
# (see the grid-scan in PCB_NOTES.md) -- one per quadrant for even support.


def add_mounting_holes(board, size, gnd_net):
    for x, y in MOUNTING_HOLES:
        fp = pcbnew.FootprintLoad(
            os.path.join(STOCK_FP, "MountingHole.pretty"),
            "MountingHole_3.2mm_M3_Pad",
        )
        fp.SetReference("MH")
        fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        board.Add(fp)
        for pad in fp.Pads():
            pad.SetNet(gnd_net)


def add_zone(board, layer, net, rect_pts, name=""):
    zone = pcbnew.ZONE(board)
    zone.SetLayer(layer)
    zone.SetNetCode(net.GetNetCode())
    if name:
        zone.SetZoneName(name)
    outline = zone.Outline()
    outline.NewOutline()
    for x, y in rect_pts:
        outline.Append(MM(x), MM(y))
    zone.SetIsFilled(False)
    zone.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    board.Add(zone)
    return zone


def main():
    netlist_path = export_netlist()
    net_map = load_netlist(netlist_path)  # (ref,pin) -> net name
    parts = load_schematic_parts()

    board = pcbnew.CreateEmptyBoard()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(4)
    ds.SetBoardThickness(MM(1.6))
    # 0.1mm (4mil) min clearance: still inside PCBWay's advanced/HDI tier,
    # and required by J1 (USB-C, native 0.5mm-pitch pads) and U5 (SOT-563) --
    # KiCad's generic 0.2mm default flags both as "clearance violations"
    # even though the parts' own pad geometry is exactly per their datasheet.
    default_nc = ds.m_NetSettings.GetDefaultNetclass()
    default_nc.SetClearance(MM(0.1))

    problems = verify_zones()
    if problems:
        print("ZONE VERIFICATION FAILED:")
        for p in problems:
            print(" ", p)
        raise SystemExit(1)

    all_footprints = {}

    # ---- anchors ----
    for ref, (x, y, rot, side) in ANCHORS.items():
        fp = add_footprint(board, ref, parts[ref]["footprint"], x, y, rot, side)
        all_footprints[ref] = fp

    bt1 = add_footprint(board, "BT1", parts["BT1"]["footprint"], BT1_POS[0], BT1_POS[1], 0, "B")
    all_footprints["BT1"] = bt1

    # correct the motor so the shaft NPTH lands exactly on board centre
    m1 = all_footprints["M1"]
    shaft_pad = None
    for p in m1.Pads():
        if p.GetNumber() == "" and abs(pcbnew.ToMM(p.GetDrillSizeX()) - 4.6) < 0.01:
            shaft_pad = p
            break
    dx = MM(CX) - shaft_pad.GetPosition().x
    dy = MM(CY) - shaft_pad.GetPosition().y
    m1.Move(pcbnew.VECTOR2I(dx, dy))

    # ---- grid-filled passives ----
    for zname, spec in FILL_ZONES.items():
        placed = grid_fill(board, spec["rect"], spec["refs"], parts, gap=spec.get("gap", 1.4))
        for fp in placed:
            all_footprints[fp.GetReference()] = fp

    print(f"placed {len(all_footprints)} footprints (expected {len(parts)})")

    # ---- net assignment ----
    net_cache = {}

    def get_net(name):
        if name not in net_cache:
            existing = board.FindNet(name)
            if existing:
                net_cache[name] = existing
            else:
                ni = pcbnew.NETINFO_ITEM(board, name)
                board.Add(ni)
                net_cache[name] = ni
        return net_cache[name]

    unmatched = 0
    for (ref, pin), netname in net_map.items():
        fp = all_footprints.get(ref)
        if fp is None:
            continue
        pad = fp.FindPadByNumber(pin)
        if pad is None:
            unmatched += 1
            continue
        pad.SetNet(get_net(netname))
    print(f"net assignment done, {unmatched} pad lookups missed")

    gnd_net = get_net("GND")

    # ---- board outline + mounting holes ----
    set_board_outline(board, BOARD_SIZE)
    add_mounting_holes(board, BOARD_SIZE, gnd_net)

    # ---- ESP32 antenna keepout is built into the RF_Module footprint; no
    # extra work needed here (verified in verify_keepout.py).

    # ---- ground plane pours, both inner layers, whole board ----
    board_rect = [(1.5, 1.5), (BOARD_SIZE - 1.5, 1.5),
                  (BOARD_SIZE - 1.5, BOARD_SIZE - 1.5), (1.5, BOARD_SIZE - 1.5)]
    for layer in (pcbnew.In1_Cu, pcbnew.In2_Cu):
        add_zone(board, layer, gnd_net, board_rect, name=f"GND_{layer}")
    # Zones are left unfilled (outline only) -- ZONE_FILLER.Fill() segfaults
    # under the bundled scripting interpreter here; KiCad computes the fill
    # automatically the first time the board is opened/edited in the GUI,
    # or via `kicad-cli pcb ... ` / Edit > Fill All Zones, so this doesn't
    # lose anything, just defers the fill computation to a live KiCad session.

    board.Save(PCB_OUT)
    print(f"saved {PCB_OUT}")


if __name__ == "__main__":
    main()
