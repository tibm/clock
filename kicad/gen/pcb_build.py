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
# Only 4 parts get a genuine hand-placed anchor -- everything else (all
# other ICs/connectors/discretes/passives) is grid-packed into per-block
# fill zones below, sized from real measured footprint area and positioned
# to clear 3 hard exclusions: the ESP32-S3 antenna keepout (built into the
# stock footprint), the motor's centre courtyard (~21mm radius), and the
# battery holder footprint. (First attempt hand-placed ~54 anchors from a
# paper floor plan; a real overlap/keepout checker found 1000+ collisions,
# because eyeballed mm-level spacing without a live renderer is unreliable
# at this component count. Reducing to 4 true anchors -- the ones with an
# actual mechanical/RF constraint -- and grid-packing the rest against
# verified-safe rectangles is what actually converges.)
ANCHORS = {
    # ---- FRONT: dial side ----
    "U14": (55.0, 23.0, 0, "F"),          # homing sensor, 12 o'clock, 32mm from centre
    "D40": (18.6, 76.0, 0, "F"),          # status row (bottom arc, r=42mm)
    "D41": (34.0, 91.4, 0, "F"),
    "D42": (55.0, 97.0, 0, "F"),
    "D43": (76.0, 91.4, 0, "F"),
    "D44": (91.4, 76.0, 0, "F"),
    "D45": (13.0, 55.0, 0, "F"),          # dial-wash, 9 o'clock
    "D46": (97.0, 55.0, 0, "F"),          # dial-wash, 3 o'clock
    # per-pixel decoupling: FRONT, offset next to its LED (the back, at the
    # same XY, is the battery holder under the bottom arc -- see PCB_NOTES.md)
    "C230": (23.6, 76.0, 0, "F"),
    "C231": (39.0, 91.4, 0, "F"),
    "C232": (60.0, 97.0, 0, "F"),
    "C233": (81.0, 91.4, 0, "F"),
    "C234": (96.4, 76.0, 0, "F"),
    "C235": (13.0, 60.0, 0, "F"),
    "C236": (97.0, 60.0, 0, "F"),

    # ---- BACK: the 4 parts with a real mechanical/RF constraint ----
    "U8": (27.0, 15.0, 0, "B"),            # ESP32-S3-WROOM-1: antenna -> top edge
    "Y1": (42.0, 15.0, 90, "B"),           # crystal: must sit close to U8
    "J6": (100.0, 25.0, 90, "B"),          # microSD: needs a board-edge slot
    "J3": (104.0, 66.0, 0, "B"),           # speaker: clear of D44/D46 (front LEDs)
    "M1": (55.0, 61.0, 0, "B"),            # motor: corrected to true centre at runtime
}

# Hard exclusions every fill-zone rect below is verified clear of:
#   EXCL_U8      -- ESP32 module body + its built-in antenna keepout zone
#   EXCL_MOTOR   -- motor courtyard, as a centre circle
#   EXCL_BATTERY -- battery holder footprint
EXCL_U8 = (2.0, 0.0, 52.0, 26.0)
EXCL_MOTOR_R = 21.0  # circle at (CX, CY)
EXCL_BATTERY = (5.0, 84.0, 85.0, 108.0)

# Fill zones for every other part (all other ICs/connectors/discretes plus
# every R/C/RT), grouped by the same functional block the schematic itself
# uses (b_*.py generators). Rectangles chosen + verified (see
# verify_zones() below) to clear the exclusions above and each other.
FILL_ZONES = {
    "MCU": {
        "rect": (2.0, 28.0, 40.0, 36.0),
        "refs": ["C140", "C141", "C142", "C143", "C144", "C145", "C146"],
    },
    "MCU2": {  # small verified-safe pocket between the MCU and RAILS_SD zones
        "rect": (40.0, 26.0, 53.0, 33.0),
        "refs": ["R50", "R51"],
    },
    "POWER": {  # POWERIN + CHARGER + PROTECTOR + wake-LED driver + NeoPixel buffer
        "rect": (2.0, 38.0, 32.0, 82.0),
        "refs": (
            ["J1", "U1", "C1", "R1", "R2", "R3", "R4", "D1"]
            + ["U2", "U3", "U4", "F1", "L1", "Q1", "Q2", "Q3", "D10", "D11", "D12", "RT1"]
            + [f"C{n}" for n in (100, 101, 102, 104, 105, 106, 107, 108, 109, 110)]
            + [f"R{n}" for n in range(10, 24)]
            + ["J9", "Q6", "Q7", "U15", "C237", "C238", "R52",
               "R104", "R105", "R106", "R107", "R110"]
        ),
    },
    "RAILS_SD": {
        # top-right; J6 (microSD anchor) occupies x91.7-108.3, y17.7-32.3,
        # so full width is available above it (y<17) and J2/SW1/SW2 move to
        # the small reclaimed corner beside it (RAILS_SD_CORNER).
        "rect": (53.0, 2.0, 92.0, 17.0),
        "refs": (
            ["U5", "U6", "U7", "L2", "L3", "L4", "D20"]
            + [f"C{n}" for n in range(120, 136)] + [f"R{n}" for n in range(40, 49)]
            + ["C222", "C223", "R70", "R71", "R72"]
        ),
    },
    "RAILS_SD_CORNER": {  # reclaimed sliver above J6, right of RAILS_SD
        "rect": (92.0, 2.0, 108.0, 17.0),
        "refs": ["J2", "SW1", "SW2"],
    },
    "IO_SENSOR": {
        "rect": (53.0, 20.0, 90.0, 34.0),  # X capped below 90 -- J6 (microSD anchor) sits at x91.7-108.3
        "refs": (
            ["U13", "J7", "J10", "J11", "C239", "C240", "C241", "R62"]
            + [f"R{n}" for n in range(90, 100)] + [f"R{n}" for n in range(111, 116)]
            + ["R97", "R98", "R99"]  # QRE1113GR bias, behind U14 up front
        ),
    },
    "MOTORDRV": {
        "rect": (78.0, 35.0, 108.0, 58.0),
        "refs": ["U11", "U12", "C200", "C201", "C202", "C210", "C211", "C212"],
    },
    "AUDIO": {
        "rect": (78.0, 60.0, 108.0, 82.0),
        "refs": (
            ["U9", "U10", "D30", "L5", "L6", "Q4"]  # J3 hand-anchored below --
            # grid-fill placed it right under D44 (front LED at 91.4,76): DRC
            # flagged a real short risk between J3's through-hole pin and
            # D44's GND pad, both landing near the same XY on F.Cu.
            + [f"C{n}" for n in (160, 161, 162, 164, 165, 166, 167, 170, 171, 172,
                                  180, 181, 182, 183, 184, 185, 186)]
            + ["R60", "R61"]
        ),
    },
}

BT1_POS = (45.0, 96.0)  # battery holder, bottom arm, horizontal


def verify_zones():
    """Sanity-check every fill rect against the 3 hard exclusions and
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
        if circle_rect_overlap(CX, CY, EXCL_MOTOR_R, r):
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


def grid_fill(board, rect, refs, parts):
    """Row-pack refs (small R/C/RT) into rect on the BACK, using each
    footprint's real bounding box so mixed 0603/0805/1210/electrolytic
    sizes pack correctly. Returns list of placed FOOTPRINTs."""
    x0, y0, x1, y1 = rect
    gap = 0.6
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
        w = w0 + gap
        h = h0 + gap
        if cx + w > x1:
            cx = x0 + gap
            cy += row_h + gap
            row_h = 0.0
        # fp is currently anchored at (0,0); its pads bbox starts at (bx0,by0)
        # in that same frame, so moving the anchor to (cx-bx0, cy-by0) puts
        # the bbox's top-left corner exactly at (cx,cy).
        target_x = cx - bx0
        target_y = cy - by0
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
    (8.0, 14.0),    # top-left: nudged off the ESP32 antenna keepout (x3-51,y0-8.25)
    (102.0, 8.0),   # top-right: clear of RAILS_SD_CORNER
    (55.0, 80.0),   # bottom-centre: clear strip between the motor and BT1
    (102.0, 102.0),  # bottom-right: clear of AUDIO zone and BT1
]


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
        placed = grid_fill(board, spec["rect"], spec["refs"], parts)
        for fp in placed:
            all_footprints[fp.GetReference()] = fp
        # undo the blanket flip inside grid_fill for FRONT-only zone? none here;
        # all fill zones are BACK, matches grid_fill's hard-coded flip.

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
