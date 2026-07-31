"""Generates sensor.kicad_pcb from sensor.kicad_sch with KiCad's own pcbnew
bindings (run under KiCad's bundled interpreter, NOT system python3):

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_build.py

Same contract as the main board (../../kicad/gen/pcb_build.py, see
../../kicad/PCB_NOTES.md): the schematic is the single source of truth for
parts + nets, every part has an explicit hand-chosen position derived from
its connectivity, and the build refuses to run on a placement/schematic
mismatch.  Placement + outline + zones + net assignment + silkscreen; no
traces (routing is left to a human, as on the main board).

The geometry helpers and the B-side transform are IMPORTED from the main
board's generator rather than copied, so the empirically-verified flip
convention lives in exactly one place; only `resolve_fp` is monkeypatched
(this project's custom footprints live in ../sensor.pretty, nickname
`sensor`).  Transform recap, verified by verify_pins() on every build:

    side B, rot   0 -> pad(lx,ly) at world (ax-lx, ay+ly)   [X mirrored]
    side B, rot 180 ->                     (ax+lx, ay-ly)   [Y mirrored]
    side B, rot  90 ->                     (ax-ly, ay+lx)
    side B, rot 270 ->                     (ax+ly, ay-lx)
    side F, rot   0 ->                     (ax+lx, ay+ly)
    side F, rot 180 ->                     (ax-lx, ay-ly)

Two of those are worth spelling out because they cost a build iteration each:
rot 180 on the back mirrors Y, *not* X (so an IC's local top row comes out
BELOW the part), and the back-side rot 90 mapping is (ax-ly, ay+lx) -- the
main board's docstring says (ax-ly, ay-lx), which is a stale note: it only
ever places rot 90 on the FRONT (U14), so nothing there exercises it.

Requirements this layout is built to (2026-07-30):
  1. every courtyard pair >= 0.22 mm apart (MIN_GAP) -- and the copper
     clearance rule is set to the same 0.22 mm, which every footprint here
     can hold (tightest in-footprint pad gap = 0.25 mm, U1/U3);
  2. two mounting holes;
  3. light sensor U3 + environment sensor U2 on the FRONT;
  4. J1 on the BACK;
  5. sensor cross-interference minimised (see the block comments below);
  6. small board -- 30 x 16 mm.
"""
import importlib.util
import json
import os
import shutil
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
SENSOR_DIR = os.path.dirname(HERE)
MAIN_GEN = os.path.abspath(os.path.join(SENSOR_DIR, "..", "kicad", "gen"))
sys.path.insert(0, MAIN_GEN)

import pcbnew  # noqa: E402

SCH = os.path.join(SENSOR_DIR, "sensor.kicad_sch")
PCB_OUT = os.path.join(SENSOR_DIR, "sensor.kicad_pcb")
SENSOR_PRETTY = os.path.join(SENSOR_DIR, "sensor.pretty")
STOCK_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

MM = pcbnew.FromMM


def _main_pcb_build():
    """Load ../../kicad/gen/pcb_build.py by path -- same basename as this
    file, so a plain `import pcb_build` would find this one instead (the
    same trap gen/build.py documents for the schematic side)."""
    spec = importlib.util.spec_from_file_location(
        "clock_main_pcb_build", os.path.join(MAIN_GEN, "pcb_build.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


MAIN = _main_pcb_build()


def resolve_fp(field):
    nick, name = field.split(":", 1)
    if nick == "sensor":
        return SENSOR_PRETTY, name
    return os.path.join(STOCK_FP, f"{nick}.pretty"), name


MAIN.resolve_fp = resolve_fp          # add_footprint() reads this global
add_footprint = MAIN.add_footprint    # incl. the Flip(LEFT_RIGHT) convention
fp_bbox_mm = MAIN.fp_bbox_mm          # pads u courtyard, no silk/text
rect_gap = MAIN.rect_gap              # separation, negative = overlap
add_zone = MAIN.add_zone
load_schematic_parts = MAIN.load_schematic_parts   # reads MAIN.SCH -> repointed
load_netlist = MAIN.load_netlist
MAIN.SCH = SCH

# ==========================================================================
# Board
# ==========================================================================
BOARD_W, BOARD_H = 30.0, 16.0     # 4.8 cm^2 -- see PCB_NOTES.md for the sizing
CORNER_R = 1.0
EDGE_M = 0.35                     # courtyard must stay this far inside the edge
PAD_EDGE_M = 0.5                  # copper-to-edge (matches the DRC constraint)
MIN_GAP = 0.22                    # hard floor between courtyards (requirement 1)
TIGHT_GAP = 0.30                  # below this it is reported as "tight"
LAYERS = 2
THICKNESS = 1.6                   # stiff enough that U1 reads gravity, not flex


PROJECT = os.path.join(SENSOR_DIR, "sensor.kicad_pro")


def read_project():
    if not os.path.exists(PROJECT):
        return None
    with open(PROJECT) as f:
        return json.load(f)


def project_template():
    """The .kicad_pro the schematic side would write from scratch --
    obtained by pointing project2 at a scratch directory, since it refuses
    to overwrite a project that already exists."""
    import project2
    tmp = tempfile.mkdtemp()
    keep = project2.OUT_DIR
    try:
        project2.OUT_DIR = tmp
        with open(project2.write_project("sensor")) as f:
            return json.load(f)
    finally:
        project2.OUT_DIR = keep
        shutil.rmtree(tmp, ignore_errors=True)


def restore_project(before):
    """board.Save() rewrites the .kicad_pro from pcbnew's own model, which
    drops every key only eeschema knows about (bom presets, ngspice, erc
    severities, ...).  Put those back -- from the file as it was before this
    build, and from the schematic generator's own template for anything an
    earlier build already lost -- while keeping everything pcbnew wrote:
    that is where the design rules set above now live."""
    after = read_project()
    if after is None:
        return 0

    def merge(src, dst):
        n = 0
        for k, v in src.items():
            if k not in dst:
                dst[k] = v
                n += 1
            elif isinstance(v, dict) and isinstance(dst[k], dict):
                n += merge(v, dst[k])
        return n

    restored = merge(before or {}, after) + merge(project_template(), after)
    if restored:
        with open(PROJECT, "w") as f:
            json.dump(after, f, indent=2)
    return restored


def export_netlist():
    out = os.path.join(HERE, "_netlist.net")
    subprocess.run(
        [KICAD_CLI, "sch", "export", "netlist", "--format", "kicadsexpr",
         "-o", out, SCH], check=True, capture_output=True)
    return out


# ==========================================================================
# PLACEMENT -- ref: (x, y, rot, side).  World mm, FRONT view, +y down.
#
# FRONT = the sensing face (looks out of the enclosure through a vent + a
# window).  It carries ONLY the two sensors that have to see the outside
# world and their own local passives -- everything else is on the BACK, which
# faces into the cube and carries the harness connector.
#
# Interference budget (requirement 5):
#   * U2 (gas/RH) sits in the bottom-left corner, 9.1 mm from U1 and 16 mm
#     from J1: away from the only self-heating part on the board (U1 ~35 mW)
#     and away from the PVC harness, whose plasticiser outgassing is a real
#     VOC-baseline offender.  A copper-pour keepout (U2_THERMAL_KEEPOUT)
#     breaks the ground plane around it so board heat cannot conduct in, and
#     nothing sits on the back behind it, so the enclosure vent can be a
#     plain through-hole.
#   * U3 (ALS) sits on the top edge with >=0.6 mm of clear courtyard all
#     round and only 0.5 mm-tall neighbours, so a window above it is not
#     shadowed; it is 10.3 mm from U2 so the vent and the window stay
#     separate apertures.
#   * U1 (IMU) sits on the centre line between the two mounting holes -- the
#     stiffest spot on the board, so the gravity vector reads the body and
#     not board flex -- with its crystal 2 mm from XIN/XOUT and its supply
#     decoupling on the opposite side from the digital pins.
# ==========================================================================

PLACEMENT = {}


def P(ref, x, y, rot=0, side="B"):
    if ref in PLACEMENT:
        raise SystemExit(f"duplicate placement for {ref}")
    PLACEMENT[ref] = (x, y, rot, side)


# ---- FRONT: environment sensor, bottom-left = the vent corner --------------
# rot 0 puts U2's I2C pins (3/4) and SDO (5) on the side facing the rest of
# the board and its two supply pins (6/8) on the bottom edge, next to C10/C11.
P("U2", 4.3, 11.0, 0, "F")      # BME688 -- vent over this part
P("C11", 3.0, 14.0, 180, "F")   # 100n at VDD  (pin 8), pad 1 faces the pin
P("C10", 6.4, 14.0, 0, "F")     # 100n at VDDIO (pin 6)
P("R10", 9.0, 11.4, 180, "F")   # SDO -> VDDIO = 0x77 (fitted)
P("R11", 10.2, 13.6, 0, "F")    # SDO -> GND   = 0x76 (DNP)

# ---- FRONT: ambient-light sensor, top edge = the window --------------------
# Kept clear of J1's through-hole pads (which poke through to this side) and
# of U2's vent, so the enclosure gets two small separate apertures.
P("U3", 11.5, 3.6, 0, "F")      # TSL2591 -- clear window over this part
P("C12", 15.0, 3.6, 0, "F")     # 1uF low-ESR at VDD (pin 5) -- 2.1 mm, the
                                # datasheet's one hard placement rule
P("R12", 8.0, 3.6, 0, "F")      # INT (pin 2) open-drain pull-up

# ---- BACK: host connector, right end --------------------------------------
# rot 0 lands pin 1 (GND) at the outboard end and pin 6 (ALS_INT) inboard, so
# the signal pins face the parts they feed and the rails land next to C1/C2.
P("J1", 27.0, 6.0, 0, "B")
P("C1", 26.0, 2.6, 180, "B")    # 10u  rail-entry bulk, pad 1 over J1.2
P("C2", 22.4, 2.6, 180, "B")    # 100n rail-entry HF

# ---- BACK: IMU, centre, between the two screws ----------------------------
# rot 180 on the back mirrors Y (not X): U1's local right column (SCL/SA0/
# CSN/ENV_SDA) faces world RIGHT toward J1, its supply column faces LEFT, the
# local bottom row (CAP/NRST/H_INTN) comes out ABOVE the part and the local
# top row (VDDIO/XIN32/XOUT32/SDA) BELOW it.  Hence: decoupling left, crystal
# below, reset/CAP above, host straps right.
P("U1", 12.5, 7.0, 180, "B")

# left column: supply decoupling + the BOOTN strap, pad 1 facing U1
P("R7", 7.7, 5.3, 180, "B")     # BOOTN pull-up  (pad 2 -> pin 4, 2.2 mm)
P("C3", 7.7, 7.1, 0, "B")       # 100n at VDD    (pin 3, 1.7 mm)
P("C4", 7.7, 8.9, 0, "B")       # 100n at VDDIO  (pin 28, 2.3 mm)

# below U1: the 32.768 kHz loop -- the one net on this board that wants short
# traces (high-impedance, 12.5 pF CL), so it gets the prime real estate.
P("Y1", 11.5, 10.7, 180, "B")   # 2.4 mm to XIN32/XOUT32, no crossover
P("C7", 7.7, 10.7, 0, "B")      # 22p on XIN32  (pad 1 faces Y1.2)
P("C8", 15.4, 10.7, 180, "B")   # 22p on XOUT32 (pad 1 faces Y1.1)

# above U1: CAP reservoir, the 10k/100n power-on reset and the two DFU
# test pads (short 1.0 mm pads on the same net as the parts beside them)
P("TP2", 6.85, 3.0, 0, "B")     # BOOTN test pad (ground it, pulse NRST -> DFU)
P("TP1", 9.15, 3.0, 0, "B")     # NRST test pad
P("C6", 11.2, 3.0, 270, "B")    # 100n on CAP (pin 9), pad 1 down, 1.8 mm
P("C9", 13.825, 3.0, 180, "B")  # NRST cap  \ pad 1 faces the NRST pin
P("R6", 19.0, 3.0, 180, "B")    # NRST 10k  / pull-up, same net

# the 2.1 mm channel between U1 and J1: the two host straps that most want to
# be at their pins (vertical 0603, pad 2 down toward the part)
P("R8", 16.45, 3.0, 90, "B")    # ENV_SCL pull-up (pin 15, 2.3 mm)
P("R5", 16.45, 6.5, 90, "B")    # H_CSN pull-up   (pin 18, 1.6 mm)

# bottom rows: the rail bulk, static straps and bus pull-ups.  These are DC
# bias resistors on strap pins and bus pull-ups -- 6-7 mm of trace is
# electrically irrelevant, so they take the leftover space.
P("C5", 7.7, 12.9, 0, "B")      # 1u local bulk (this board hangs off 200 mm
                                # of harness; the CEVA reference omits it)
P("R3", 13.5, 12.9, 0, "B")     # SA0 -> GND = 0x4A (fitted)
P("R1", 17.0, 12.9, 0, "B")     # SDA pull-up
P("R2", 20.4, 12.9, 0, "B")     # SCL pull-up
P("R4", 13.5, 14.7, 0, "B")     # SA0 -> VDDIO = 0x4B (DNP)
P("R9", 16.9, 14.7, 0, "B")     # ENV_SDA pull-up (unused bus, but the SH-2
                                # firmware probes it at every reset)

# ==========================================================================
# Mounting holes -- 2x M2, NON-plated (requirement 2)
#
# NPTH, not plated+grounded like the main board's: this board's ground
# arrives on a single harness wire, so bolting its plane to the same
# aluminium the main board is bolted to would close a chassis ground loop
# around that wire.  Isolated holes + nylon/brass M2 also keep steel out of
# the magnetometer's field.  Placed at opposite corners for the longest
# possible span (25.3 mm) with U1 near the middle of it.
# ==========================================================================
MOUNTING_HOLES = [("H1", 3.1, 3.1), ("H2", 26.5, 12.6)]
MOUNTING_FP = "MountingHole:MountingHole_2.2mm_M2"

# Copper-pour keepout that thermally decouples the gas sensor from the rest
# of the board (Bosch BME680/688 handbook: reduce the copper around it).  The
# five signals still reach it on tracks; only the pour is excluded.
U2_THERMAL_KEEPOUT = (1.6, 8.2, 7.6, 13.4)

# Enclosure apertures, drawn on User.Drawings for the mechanical design:
# (x, y, r, label)
APERTURES = [(4.3, 11.0, 2.6, "VENT"), (11.5, 3.6, 2.0, "ALS")]

# Critical-pin world positions -- asserts the F/B x rot transform every build
VERIFY_PINS = [
    ("U1", "3", 10.188, 7.25),     # B/rot180 mirrors Y: local left col -> left
    ("U1", "20", 14.75, 8.562),    #   local top row (SDA) -> BELOW the part
    ("U1", "14", 14.25, 5.438),    #   local bottom row (H_INTN) -> ABOVE it
    ("U1", "26", 11.75, 8.562),    #   XOUT32, right of XIN32 -> no crossover
    ("J1", "1", 27.0, 6.0),        # B/rot0: pin 1 at the anchor
    ("J1", "6", 19.5, 6.0),        # B/rot0 mirrors X: pin 6 lands to the left
    ("Y1", "1", 12.75, 10.7),      # B/rot180
    ("C6", "1", 11.2, 3.775),      # B/rot270: pad 1 points down at U1
    ("R5", "2", 16.45, 7.325),     # B/rot90: pad 2 points down at U1
    ("U2", "3", 4.7, 9.812),       # F/rot0: SDI on the top edge
    ("U3", "5", 12.137, 3.6),      # F/rot0: VDD on the right column
    ("C12", "1", 14.225, 3.6),     # F/rot0: pad 1 faces U3
    ("R11", "1", 9.375, 13.6),     # F/rot0
    ("C11", "1", 3.775, 14.0),     # F/rot180 mirrors both axes
]


# ==========================================================================
# Board construction
# ==========================================================================


def set_board_outline(board):
    """Rounded rectangle, 4 segments + 4 true arcs (same idiom/sweep
    direction as the main board's outline)."""
    r, W, H = CORNER_R, BOARD_W, BOARD_H
    segs = [((W - r, 0), (r, 0)), ((W, H - r), (W, r)),
            ((r, H), (W - r, H)), ((0, r), (0, H - r))]
    for (x1, y1), (x2, y2) in segs:
        s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
        s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
        s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
        s.SetLayer(pcbnew.Edge_Cuts)
        s.SetWidth(MM(0.1))
        board.Add(s)
    arcs = [((W - r, r), (W - r, 0)), ((W - r, H - r), (W, H - r)),
            ((r, H - r), (r, H)), ((r, r), (0, r))]
    for (cx, cy), (sx, sy) in arcs:
        a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
        a.SetCenter(pcbnew.VECTOR2I(MM(cx), MM(cy)))
        a.SetStart(pcbnew.VECTOR2I(MM(sx), MM(sy)))
        a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90.0, pcbnew.DEGREES_T), False)
        a.SetLayer(pcbnew.Edge_Cuts)
        a.SetWidth(MM(0.1))
        board.Add(a)


def outline_violation(rect, margin):
    """How far `rect` pokes outside the rounded outline shrunk by `margin`
    (0.0 = fully inside).  Straight edges + the four corner arcs."""
    x0, y0, x1, y1 = rect
    r, W, H, m = CORNER_R, BOARD_W, BOARD_H, margin
    out = max(0.0, m - x0, m - y0, x1 - (W - m), y1 - (H - m))
    for cx, cy in ((r, r), (W - r, r), (r, H - r), (W - r, H - r)):
        # the rect corner that is diagonally outward from this arc centre
        px = x0 if cx < W / 2 else x1
        py = y0 if cy < H / 2 else y1
        if (px < cx if cx < W / 2 else px > cx) and \
           (py < cy if cy < H / 2 else py > cy):
            d = ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5
            out = max(out, d - (r - m))
    return out


def add_mounting_holes(board):
    holes = {}
    for ref, x, y in MOUNTING_HOLES:
        lib, name = resolve_fp(MOUNTING_FP)
        fp = pcbnew.FootprintLoad(lib, name)
        if fp is None:
            raise RuntimeError(f"missing footprint {MOUNTING_FP}")
        fp.SetReference(ref)
        fp.SetFPID(pcbnew.LIB_ID(*MOUNTING_FP.split(":", 1)))
        fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        board.Add(fp)
        holes[ref] = fp
    return holes


def add_keepout(board, rect, name):
    """Rule area that excludes the copper pours (tracks/vias still allowed:
    the five BME688 signals have to get in)."""
    x0, y0, x1, y1 = rect
    z = pcbnew.ZONE(board)
    z.SetIsRuleArea(True)
    z.SetZoneName(name)
    ls = pcbnew.LSET()
    ls.addLayer(pcbnew.F_Cu)
    ls.addLayer(pcbnew.B_Cu)
    z.SetLayerSet(ls)
    for setter in ("SetDoNotAllowZoneFills", "SetDoNotAllowCopperPour"):
        if hasattr(z, setter):
            getattr(z, setter)(True)
            break
    else:
        raise RuntimeError("no copper-pour keepout setter in this pcbnew")
    for setter, val in (("SetDoNotAllowTracks", False),
                        ("SetDoNotAllowVias", False),
                        ("SetDoNotAllowPads", False),
                        ("SetDoNotAllowFootprints", False)):
        if hasattr(z, setter):
            getattr(z, setter)(val)
    o = z.Outline()
    o.NewOutline()
    for x, y in ((x0, y0), (x1, y0), (x1, y1), (x0, y1)):
        o.Append(MM(x), MM(y))
    board.Add(z)
    return z


def add_doc_circle(board, x, y, r, layer=None):
    c = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_CIRCLE)
    c.SetCenter(pcbnew.VECTOR2I(MM(x), MM(y)))
    c.SetStart(pcbnew.VECTOR2I(MM(x), MM(y)))
    c.SetEnd(pcbnew.VECTOR2I(MM(x + r), MM(y)))
    c.SetLayer(pcbnew.Dwgs_User if layer is None else layer)
    c.SetWidth(MM(0.1))
    board.Add(c)


def add_doc_text(board, text, x, y, size=0.8, layer=None, mirror=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetLayer(pcbnew.Dwgs_User if layer is None else layer)
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(size * 0.15))
    if mirror:
        t.SetMirrored(True)
    board.Add(t)
    return t


# ==========================================================================
# QA
# ==========================================================================


def is_thru(fp):
    """PTH *and* NPTH occupy both sides (the main board's helper only had to
    know about PTH; the M2 holes here are NPTH)."""
    return any(p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH,
                                    pcbnew.PAD_ATTRIB_NPTH)
               for p in fp.Pads())


def side_of(fp):
    return "F" if fp.GetLayerName() == "F.Cu" else "B"


def sides_of(ref, fp):
    return ("F", "B") if is_thru(fp) else (side_of(fp),)


def qa_courtyards(fps, boxes):
    errors, tight, allpairs = [], [], []
    refs = sorted(fps)
    for i in range(len(refs)):
        for j in range(i + 1, len(refs)):
            r1, r2 = refs[i], refs[j]
            if not (set(sides_of(r1, fps[r1])) & set(sides_of(r2, fps[r2]))):
                continue
            g = rect_gap(boxes[r1], boxes[r2])
            allpairs.append((g, r1, r2))
            if g < MIN_GAP:
                errors.append((g, r1, r2))
            elif g < TIGHT_GAP:
                tight.append((g, r1, r2))
    allpairs.sort()
    return errors, tight, allpairs


def qa_edges(fps, boxes):
    errors = []
    for ref, fp in fps.items():
        out = outline_violation(boxes[ref], EDGE_M)
        if out > 0.0005:
            errors.append(f"{ref} courtyard pokes {out:.2f} mm past the "
                          f"{EDGE_M} mm edge margin")
        for p in fp.Pads():
            b = p.GetBoundingBox()
            pr = (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                  pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))
            out = outline_violation(pr, PAD_EDGE_M)
            if out > 0.0005:
                errors.append(f"{ref}.{p.GetNumber()} copper is {out:.2f} mm "
                              f"inside the {PAD_EDGE_M} mm edge clearance")
    return errors


def qa_requirements(fps, boxes):
    """The user-facing constraints, re-checked on the built board."""
    out = []
    for ref, want in (("U2", "F"), ("U3", "F"), ("J1", "B")):
        got = side_of(fps[ref])
        out.append((f"{ref} on {'FRONT' if want == 'F' else 'BACK'}",
                    got == want, f"{ref} is on {got}"))
    n = len([r for r in fps if r.startswith("H")])
    out.append(("2 mounting holes", n == 2, f"{n} placed"))
    # cross-side: keep the back clear behind the gas sensor so the vent can
    # be a plain hole and no part heats it through the board
    behind = [r for r, f in fps.items()
              if side_of(f) == "B" and r != "U2"
              and rect_gap(boxes[r], boxes["U2"]) < 0.0]
    out.append(("back side clear behind U2", not behind, str(behind)))
    return out


def part_distance(boxes, a, b):
    return rect_gap(boxes[a], boxes[b])


def qa_sensing(fps, boxes, net_map):
    """Reports the numbers requirement 5 is judged on."""
    lines = []
    def centres(a, b):
        pa, pb = fps[a].GetPosition(), fps[b].GetPosition()
        return ((pcbnew.ToMM(pa.x) - pcbnew.ToMM(pb.x)) ** 2
                + (pcbnew.ToMM(pa.y) - pcbnew.ToMM(pb.y)) ** 2) ** 0.5
    for a, b, why in (("U2", "U1", "IMU = the only self-heating part"),
                      ("U2", "J1", "PVC harness outgassing"),
                      ("U2", "U3", "keeps vent and window separate")):
        lines.append(f"{a} <-> {b}: {centres(a, b):5.1f} mm centre to centre, "
                     f"{part_distance(boxes, a, b):.1f} mm courtyard gap "
                     f"({why})")
    # optical clear radius around U3: nearest same-side neighbour
    near = sorted((rect_gap(boxes["U3"], boxes[r]), r) for r, f in fps.items()
                  if r != "U3" and "F" in sides_of(r, f))
    lines.append(f"U3 clear courtyard: {near[0][0]:.2f} mm (nearest: "
                 f"{', '.join(f'{r} {g:.2f}' for g, r in near[:3])})")
    # crystal loop
    pads = {}
    for ref, fp in fps.items():
        for p in fp.Pads():
            pads[(ref, p.GetNumber())] = (pcbnew.ToMM(p.GetPosition().x),
                                          pcbnew.ToMM(p.GetPosition().y))
    def d(a, b):
        (x1, y1), (x2, y2) = pads[a], pads[b]
        return ((x1 - x2) ** 2 + (y1 - y2) ** 2) ** 0.5
    lines.append(f"32k crystal: Y1.1-U1.26 {d(('Y1', '1'), ('U1', '26')):.1f} mm, "
                 f"Y1.2-U1.27 {d(('Y1', '2'), ('U1', '27')):.1f} mm")
    # mounting stiffness: U1 should sit between the two screws
    (hx1, hy1), (hx2, hy2) = [(x, y) for _, x, y in MOUNTING_HOLES]
    ux, uy = pads[("U1", "3")]
    span = ((hx1 - hx2) ** 2 + (hy1 - hy2) ** 2) ** 0.5
    # distance from U1 to the line through the two holes
    t = abs((hx2 - hx1) * (hy1 - uy) - (hx1 - ux) * (hy2 - hy1)) / span
    lines.append(f"mounting span {span:.1f} mm; U1 sits {t:.1f} mm off the "
                 f"screw-to-screw line")
    return lines


RAIL_NETS = {"GND", "+3V3"}


def qa_locality(fps, net_map, limit=6.0):
    """Every 2-pad satellite must sit at the thing it serves."""
    pad_pos = {}
    for ref, fp in fps.items():
        for p in fp.Pads():
            pad_pos[(ref, p.GetNumber())] = (pcbnew.ToMM(p.GetPosition().x),
                                             pcbnew.ToMM(p.GetPosition().y))
    by_net = {}
    for (ref, pin), net in net_map.items():
        if (ref, pin) in pad_pos:
            by_net.setdefault(net, []).append((ref, pin))
    report = []
    for ref, fp in fps.items():
        if ref[0] not in "RCY":
            continue
        worst, worst_net = 0.0, ""
        for p in fp.Pads():
            net = net_map.get((ref, p.GetNumber()))
            if not net or net in RAIL_NETS or net.startswith("unconnected"):
                continue
            others = [pad_pos[o] for o in by_net.get(net, []) if o[0] != ref]
            if not others:
                continue
            x, y = pad_pos[(ref, p.GetNumber())]
            dmin = min(((x - ox) ** 2 + (y - oy) ** 2) ** 0.5 for ox, oy in others)
            if dmin > worst:
                worst, worst_net = dmin, net
        if worst > limit:
            report.append((worst, ref, worst_net))
    report.sort(reverse=True)
    return report


def qa_ratsnest(fps, net_map):
    pad_pos = {}
    for ref, fp in fps.items():
        for p in fp.Pads():
            pad_pos[(ref, p.GetNumber())] = (pcbnew.ToMM(p.GetPosition().x),
                                             pcbnew.ToMM(p.GetPosition().y))
    by_net = {}
    for key, net in net_map.items():
        if net.startswith("unconnected") or key not in pad_pos:
            continue
        by_net.setdefault(net, []).append(pad_pos[key])
    total = 0.0
    for pts in by_net.values():
        if len(pts) < 2:
            continue
        used, rest = [pts[0]], pts[1:]
        while rest:
            best, bi = None, None
            for i, q in enumerate(rest):
                dd = min(((q[0] - u[0]) ** 2 + (q[1] - u[1]) ** 2) ** 0.5
                         for u in used)
                if best is None or dd < best:
                    best, bi = dd, i
            total += best
            used.append(rest.pop(bi))
    return total


# ==========================================================================
# Silkscreen
# ==========================================================================


def silk_obstacles(fps_all):
    """Two classes: HARD = pads (silk over a mask opening is a fab defect,
    never allowed), SOFT = part outlines and other labels (ugly, tolerated
    as a last resort)."""
    hard = {"F": [], "B": []}
    soft = {"F": [], "B": []}
    for ref, fp in fps_all.items():
        for p in fp.Pads():
            b = p.GetBoundingBox()
            box = (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                   pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))
            if p.GetAttribute() in (pcbnew.PAD_ATTRIB_PTH, pcbnew.PAD_ATTRIB_NPTH):
                hard["F"].append(box)
                hard["B"].append(box)
            else:
                hard["F" if p.IsOnLayer(pcbnew.F_Cu) else "B"].append(box)
        for d in fp.GraphicalItems():
            lay = d.GetLayer()
            if lay in (pcbnew.F_SilkS, pcbnew.B_SilkS):
                b = d.GetBoundingBox()
                soft["F" if lay == pcbnew.F_SilkS else "B"].append(
                    (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                     pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom())))
    return hard, soft


SILK_HIDDEN = []


def settle_text(t, hard, bodies, labels, side, cands, strict=False,
                exclude=None):
    """Place a text item at the first candidate whose REAL rendered box
    (KiCad font metrics, incl. mirroring) is clear.  Pass 1 avoids pads,
    outlines and other labels; pass 2 gives up on the soft obstacles but
    still keeps 0.15 mm off every pad.  Returns None if neither works --
    the caller then hides that label rather than printing silk onto a
    solder pad (the reference stays on the F.Fab/B.Fab assembly layer)."""
    boxes = []
    for cx, cy in cands:
        t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
        b = t.GetBoundingBox()
        boxes.append(((cx, cy), (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                                 pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))))
    bods = [b for b in bodies[side] if b != exclude]
    # Order of surrender: first keep clear of everything; then allow crossing
    # a part OUTLINE but never another label (two labels on top of each other
    # are unreadable, a label over a silk outline is merely untidy); then,
    # for reference designators only, pads-only.
    passes = [(hard[side] + bods + labels[side], 0.15),
              (hard[side] + labels[side], 0.15)]
    for obstacles, cl in passes:
        for (cx, cy), box in boxes:
            if outline_violation(box, 0.1) > 0:
                continue
            if all(rect_gap(box, ob) >= cl for ob in obstacles):
                t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
                return box
    if strict:
        return None
    # last resort (reference designators only): pads and the board edge are
    # still absolute, but among those spots take the LEAST crowded one rather
    # than the nearest, so the leftovers spread instead of stacking up
    rest = [(cx, cy, box) for (cx, cy), box in boxes
            if outline_violation(box, 0.1) <= 0
            and all(rect_gap(box, ob) >= 0.15 for ob in hard[side])]
    if not rest:
        return None
    cx, cy, box = max(rest, key=lambda e: min(
        [rect_gap(e[2], ob) for ob in bods + labels[side]] or [0.0]))
    t.SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    return box


def ref_candidates(fp, ref):
    x0, y0, x1, y1 = fp_bbox_mm(fp)
    cx0, cy0 = (x0 + x1) / 2, (y0 + y1) / 2
    w = 0.68 * len(ref) + 0.4
    cands = []
    for pad in (0.62, 1.35, 2.1, 2.85):
        cands += [(cx0, y0 - pad), (cx0, y1 + pad),
                  (x0 - w / 2 - pad, cy0), (x1 + w / 2 + pad, cy0)]
        for dx in (-1.0, 1.0, -2.0, 2.0, -3.0, 3.0, -4.0, 4.0):
            cands += [(cx0 + dx, y0 - pad), (cx0 + dx, y1 + pad)]
        for dy in (-0.8, 0.8, -1.6, 1.6, -2.4, 2.4):
            cands += [(x0 - w / 2 - pad, cy0 + dy), (x1 + w / 2 + pad, cy0 + dy)]
    # nearest first, so a label never drifts across a neighbour when a closer
    # spot is free -- "R12" printed next to U3 reads as U3's designator
    cands.sort(key=lambda c: (c[0] - cx0) ** 2 + (c[1] - cy0) ** 2)
    return cands


def place_ref_labels(fps_all, hard, bodies, labels):
    """Greedy, most-constrained-first: a part boxed in on every side gets to
    pick before its roomier neighbours do."""
    todo = []
    for ref, fp in fps_all.items():
        t = fp.Reference()
        fp.Value().SetVisible(False)      # values would double the silk load
        if ref.startswith("H"):
            t.SetVisible(False)
            continue
        t.SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
        t.SetTextThickness(MM(0.12))
        todo.append(ref)

    # a part's own courtyard must not push its own label away
    own = {r: fp_bbox_mm(fps_all[r]) for r in todo}

    def freedom(ref):
        fp = fps_all[ref]
        side = side_of(fp)
        obs = (hard[side] + [b for b in bodies[side] if b != own[ref]]
               + labels[side])
        n = 0
        for cx, cy in ref_candidates(fp, ref):
            fp.Reference().SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy)))
            b = fp.Reference().GetBoundingBox()
            box = (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                   pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))
            if outline_violation(box, 0.1) > 0:
                continue
            if all(rect_gap(box, ob) >= 0.15 for ob in obs):
                n += 1
        return n

    for ref in sorted(todo, key=freedom):
        fp = fps_all[ref]
        side = side_of(fp)
        box = settle_text(fp.Reference(), hard, bodies, labels, side,
                          ref_candidates(fp, ref), exclude=own[ref])
        if box is None:
            # keep silk off every solder pad: hide it (the .Fab layer copy
            # still names the part for assembly) and park it back on the
            # part, so un-hiding it in the GUI puts it somewhere sane
            t = fp.Reference()
            t.SetPosition(fp.GetPosition())
            t.SetVisible(False)
            SILK_HIDDEN.append(ref)
        else:
            labels[side].append(box)


# B.SilkS is mirrored so it reads correctly when you look at the back.
BACK_LABELS = [("TO J7", 21.2, 10.3)]
FRONT_LABELS = [("VENT", 4.3, 7.4), ("ALS", 11.5, 6.6),
                ("SENSOR v0.19", 22.6, 2.2)]


def main():
    net_map = load_netlist(export_netlist())
    parts = load_schematic_parts()

    missing = sorted(set(parts) - set(PLACEMENT))
    extra = sorted(set(PLACEMENT) - set(parts))
    if missing or extra:
        raise SystemExit(f"placement/schematic mismatch: missing={missing} "
                         f"extra={extra}")

    board = pcbnew.CreateEmptyBoard()
    ds = board.GetDesignSettings()
    ds.SetCopperLayerCount(LAYERS)
    ds.SetBoardThickness(MM(THICKNESS))
    nc = ds.m_NetSettings.GetDefaultNetclass()
    nc.SetClearance(MM(MIN_GAP))       # 0.22 mm -- requirement 1, and every
    nc.SetTrackWidth(MM(0.25))         # footprint here holds it (min in-part
    nc.SetViaDiameter(MM(0.6))         # pad gap = 0.25 mm on U1/U3)
    nc.SetViaDrill(MM(0.3))
    ds.m_MinThroughDrill = MM(0.3)
    ds.m_TrackMinWidth = MM(0.2)
    ds.m_CopperEdgeClearance = MM(PAD_EDGE_M)

    fps = {}
    for ref, (x, y, rot, side) in PLACEMENT.items():
        fps[ref] = add_footprint(board, ref, parts[ref]["footprint"], x, y,
                                 rot, side, parts[ref]["value"])

    # ---- nets ----
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

    unmatched = []
    for (ref, pin), netname in net_map.items():
        fp = fps.get(ref)
        if fp is None:
            continue
        hits = 0
        for pad in fp.Pads():
            if pad.GetNumber() == pin:
                pad.SetNet(get_net(netname))
                hits += 1
        if not hits:
            unmatched.append(f"{ref}.{pin}")
    gnd = get_net("GND")

    # ---- transform verification ----
    fails = []
    for ref, pin, ex, ey in VERIFY_PINS:
        pad = fps[ref].FindPadByNumber(pin)
        px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
        if abs(px - ex) > 0.02 or abs(py - ey) > 0.02:
            fails.append(f"{ref}.{pin}: expected ({ex},{ey}) got ({px:.3f},{py:.3f})")
    if fails:
        print("PIN TRANSFORM CHECK FAILED:")
        for f in fails:
            print("  ", f)
        raise SystemExit(1)

    set_board_outline(board)
    holes = add_mounting_holes(board)
    allfp = dict(fps)
    allfp.update(holes)

    # ---- ground pours + the gas sensor's thermal island ----
    m = 0.5
    r = CORNER_R
    rect = [(m + r * 0.3, m), (BOARD_W - m - r * 0.3, m),
            (BOARD_W - m, m + r * 0.3), (BOARD_W - m, BOARD_H - m - r * 0.3),
            (BOARD_W - m - r * 0.3, BOARD_H - m), (m + r * 0.3, BOARD_H - m),
            (m, BOARD_H - m - r * 0.3), (m, m + r * 0.3)]
    for layer, name in ((pcbnew.F_Cu, "GND_F"), (pcbnew.B_Cu, "GND_B")):
        z = add_zone(board, layer, gnd, rect, name)
        # SOLID pad connection, not the thermal relief the main board uses:
        # this board is reflowed by the assembler, never hand-soldered, so
        # relief spokes buy nothing and cost ground impedance -- and several
        # GND pads sit in channels the pour can only reach from one side,
        # which a 2-spoke thermal rule would flag (and starve) forever.
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        board.Add(z)
    add_keepout(board, U2_THERMAL_KEEPOUT, "U2_thermal_island")

    # ---- documentation layer: the two enclosure apertures ----
    for x, y, rr, label in APERTURES:
        add_doc_circle(board, x, y, rr)
        add_doc_text(board, label, x, y - rr - 0.7, 0.8)
    add_doc_text(board, f"{BOARD_W:.0f} x {BOARD_H:.0f} mm, 2 layer, "
                        f"{THICKNESS} mm, M2 NPTH x2", 1.0, -1.2, 0.9)

    # ---- silkscreen ----
    # Board-level markings are placed FIRST -- they say what the board is and
    # which way round the harness goes, so they outrank a reference
    # designator (which has 100+ candidate spots and a .Fab-layer copy).
    hard, bodies = silk_obstacles(allfp)
    labels = {"F": [], "B": []}
    for ref, fp in allfp.items():
        b = fp_bbox_mm(fp)
        for sd in sides_of(ref, fp):
            bodies[sd].append(b)        # keep labels off part BODIES too
    for texts, layer, sd, mirror in ((FRONT_LABELS, pcbnew.F_SilkS, "F", False),
                                     (BACK_LABELS, pcbnew.B_SilkS, "B", True)):
        for text, x, y in texts:
            t = add_doc_text(board, text, x, y, 0.9, layer, mirror=mirror)
            cands = [(x + dx, y + dy)
                     for dy in (0, -0.8, 0.8, -1.6, 1.6, -2.4, 2.4, -3.2, 3.2)
                     for dx in (0, -1.0, 1.0, -2.0, 2.0, -3.0, 3.0)]
            box = settle_text(t, hard, bodies, labels, sd, cands, strict=True)
            if box is None:
                board.Remove(t)
                SILK_HIDDEN.append(text)
            else:
                labels[sd].append(box)
    place_ref_labels(allfp, hard, bodies, labels)

    pro_before = read_project()
    board.Save(PCB_OUT)          # unfilled first: if the filler dies (it does
                                 # on the main board) a valid file is on disk
    # Fill the pours in a CHILD process: the filler segfaults on a board this
    # process built in memory, but not on one loaded from disk (pcb_fill.py).
    fill = subprocess.run([sys.executable, os.path.join(HERE, "pcb_fill.py")],
                          capture_output=True, text=True)
    filled = fill.returncode == 0
    restored = restore_project(pro_before)   # the child's save rewrites it too

    # ---- QA report ----
    boxes = {r_: fp_bbox_mm(f) for r_, f in allfp.items()}
    print(f"saved {PCB_OUT}")
    print(f"  {len(fps)} parts + {len(holes)} mounting holes on "
          f"{BOARD_W} x {BOARD_H} mm ({BOARD_W * BOARD_H / 100:.1f} cm2), "
          f"{LAYERS} layers")
    print(f"  net assignment: {len(unmatched)} pad lookups missed "
          f"{unmatched if unmatched else ''}")
    print(f"  sensor.kicad_pro: design rules written by pcbnew, "
          f"{restored} eeschema-only keys restored")
    print("  GND pours: " + (fill.stdout.strip().replace("\n", " |")
                             if filled else
                             "NOT filled (pcb_fill.py failed) -- "
                             "use Edit > Fill All Zones"))
    front = sorted(r_ for r_, f in fps.items() if side_of(f) == "F")
    print(f"  FRONT ({len(front)}): {' '.join(front)}")

    print("\n--- requirements ---")
    ok = True
    for name, passed, detail in qa_requirements(allfp, boxes):
        print(f"  [{'OK ' if passed else 'FAIL'}] {name}: {detail}")
        ok = ok and passed

    errors, tight, allpairs = qa_courtyards(allfp, boxes)
    print(f"\n--- courtyard clearance (floor {MIN_GAP} mm): "
          f"{len(errors)} ERRORS, {len(tight)} tight ---")
    for g, r1, r2 in errors:
        print(f"  ERR   {r1:4s} <-> {r2:4s} {g:+.3f} mm")
    for g, r1, r2 in tight:
        print(f"  tight {r1:4s} <-> {r2:4s} {g:+.3f} mm")
    print("  10 closest pairs: " + ", ".join(f"{r1}/{r2} {g:.2f}"
                                             for g, r1, r2 in allpairs[:10]))
    eerr = qa_edges(allfp, boxes)
    print(f"--- board edge: {len(eerr)} violations ---")
    for e in eerr:
        print("  ERR", e)

    print("\n--- sensing / interference ---")
    for line in qa_sensing(allfp, boxes, net_map):
        print("  " + line)

    loc = qa_locality(fps, net_map)
    print(f"\n--- locality (satellites >6 mm from their signal net): {len(loc)} ---")
    for d_, ref, net in loc:
        print(f"  {ref:4s} {d_:5.1f} mm from {net}")
    print(f"--- est. total ratsnest (MST): {qa_ratsnest(fps, net_map):.0f} mm ---")
    print(f"--- silk: {len(SILK_HIDDEN)} labels hidden for want of a "
          f"pad-free spot (they stay on the .Fab assembly layer)"
          f"{': ' + ' '.join(SILK_HIDDEN) if SILK_HIDDEN else ''} ---")
    if errors or eerr or unmatched or not ok:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
