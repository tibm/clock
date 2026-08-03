"""Generate the STEP 3D models KiCad doesn't ship for parts on these boards.

Three footprints on the two boards have no usable stock 3D model:

* **JST ZH vertical headers** (J7/J10/J11 main, J1 sensor).  KiCad 10 ships the
  ZH *footprints* but no ZH models at all -- Connector_JST.3dshapes has
  EH/GH/PH/SH/XH and stops there -- so the footprints point at a .step that
  isn't in the install and the parts rendered as bare pads.  Built from the JST
  drawing (`datasheet/connector_jst_zh.pdf` p.4, header/through-hole, top
  entry): A = (n-1)x1.50 pin span, B = A+3.00 wafer length, wafer 3.50 wide
  (-1.30..+2.20 about the pin row, locking side +2.20) x 4.50 high, pin O0.50
  with the 2.70 mm soldering length of the plain B*B-ZR.
* **Cantherm SDF thermal cut-off** (F1) -- custom footprint, no vendor model.
  Body 10.5 x O4.0 mm on a 20.32 mm pitch with AWG18 (O1.024) axial leads, per
  the footprint's own dimensions; it matters in 3D because F1 sits ~1.2 mm from
  the 18650 holder.

Everything is written straight in KiCad's 3D frame -- X = footprint X,
**Y = -footprint Y**, Z up out of the mounting side -- so the `(model ...)`
entries need neither offset nor rotation.

Colours ride along in the STEP (XCAF is unusable in FreeCAD's pythonocc build,
so the AP214 presentation entities are appended by hand, see `colourise()`);
they are representative, the geometry is from the drawings.

Run under an interpreter with pythonocc -- FreeCAD's bundled one has it:

    /Applications/FreeCAD.app/Contents/Resources/bin/python mk3d.py
"""
import os
import re
import sys

from OCC.Core.BRep import BRep_Builder
from OCC.Core.BRepAlgoAPI import BRepAlgoAPI_Cut, BRepAlgoAPI_Fuse
from OCC.Core.BRepPrimAPI import BRepPrimAPI_MakeBox, BRepPrimAPI_MakeCylinder
from OCC.Core.gp import gp_Ax2, gp_Dir, gp_Pnt
from OCC.Core.IFSelect import IFSelect_RetDone
from OCC.Core.Interface import Interface_Static
from OCC.Core.STEPControl import STEPControl_AsIs, STEPControl_Writer
from OCC.Core.TopAbs import TopAbs_SOLID
from OCC.Core.TopExp import TopExp_Explorer
from OCC.Core.TopoDS import TopoDS_Compound

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_3D = os.path.join(HERE, "..", "3d")
SENSOR_3D = os.path.join(HERE, "..", "..", "kicad-sensor", "3d")

IVORY = (0.94, 0.91, 0.80)      # PA66 natural, JST wafers
TIN = (0.78, 0.78, 0.82)        # tin-plated pins/leads
TCO_BODY = (0.86, 0.83, 0.73)   # epoxy-coated TCO can


# --- primitives -----------------------------------------------------------

def box(x0, y0, z0, dx, dy, dz):
    return BRepPrimAPI_MakeBox(gp_Pnt(x0, y0, z0), dx, dy, dz).Shape()


def rod(p0, direction, d, length):
    return BRepPrimAPI_MakeCylinder(
        gp_Ax2(gp_Pnt(*p0), gp_Dir(*direction)), d / 2, length).Shape()


def fuse(*shapes):
    out = shapes[0]
    for s in shapes[1:]:
        out = BRepAlgoAPI_Fuse(out, s).Shape()
    return out


# --- JST ZH B*B-ZR (top entry, through hole) ------------------------------

PITCH = 1.50
WAFER_W = 3.50        # depth across the pin row
WAFER_FRONT = 2.20    # locking side, footprint +y
WAFER_H = 4.50        # above the PCB
END = 1.50            # wafer overhang past the end pins
WALL = 0.45           # shroud wall (JST doesn't dimension it; the cavity has
                      # to clear the 3.4 mm-wide ZHR housing)
FLOOR = 1.30          # cavity floor above the PCB
PIN_D = 0.50
PIN_BELOW = 2.70      # "2.7 mm soldering length"
PIN_TOP = 3.90        # pin tip inside the shroud


def jst_zh(n):
    length = (n - 1) * PITCH + 2 * END
    x0, y0 = -END, -WAFER_FRONT           # world y = -footprint y
    body = box(x0, y0, 0.0, length, WAFER_W, WAFER_H)
    body = BRepAlgoAPI_Cut(body, box(
        x0 + WALL, y0 + WALL, FLOOR,
        length - 2 * WALL, WAFER_W - 2 * WALL, WAFER_H - FLOOR + 1.0)).Shape()
    win = min(length - 2.0, (n - 1) * PITCH + 0.6)      # latch window
    body = BRepAlgoAPI_Cut(body, box(
        (n - 1) * PITCH / 2 - win / 2, y0 - 0.1, 1.9,
        win, WALL + 0.2, 1.4)).Shape()
    pins = [rod((i * PITCH, 0.0, -PIN_BELOW), (0, 0, 1), PIN_D,
                PIN_BELOW + PIN_TOP) for i in range(n)]
    return [([body], IVORY), (pins, TIN)]


# --- Cantherm SDF axial thermal cut-off -----------------------------------

TCO_PITCH = 20.32
TCO_LEN = 10.50
TCO_D = 4.00
TCO_LEAD_D = 1.024    # AWG18
TCO_AXIS = TCO_D / 2  # body resting on the board


def tco_sdf():
    x0 = (TCO_PITCH - TCO_LEN) / 2                      # 4.91, per the F.Fab
    body = rod((x0, 0.0, TCO_AXIS), (1, 0, 0), TCO_D, TCO_LEN)
    leads = []
    for x, run in ((0.0, x0), (TCO_PITCH, -x0)):
        leads.append(fuse(
            rod((x, 0.0, -2.5), (0, 0, 1), TCO_LEAD_D, TCO_AXIS + 2.5),
            rod((x, 0.0, TCO_AXIS), (1 if run > 0 else -1, 0, 0),
                TCO_LEAD_D, abs(run))))
    return [([body], TCO_BODY), (leads, TIN)]


# --- STEP out -------------------------------------------------------------

def n_solids(shape):
    exp, k = TopExp_Explorer(shape, TopAbs_SOLID), 0
    while exp.More():
        k += 1
        exp.Next()
    return k


# One STYLED_ITEM per MANIFOLD_SOLID_BREP, all gathered into a single
# MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_REPRESENTATION on the file's
# geometric context -- the shape KiCad's OCC importer reads colour back from.
_STYLE = """#{a} = COLOUR_RGB('',{r:.6f},{g:.6f},{b:.6f});
#{c} = FILL_AREA_STYLE_COLOUR('',#{a});
#{d} = FILL_AREA_STYLE('',(#{c}));
#{e} = SURFACE_STYLE_FILL_AREA(#{d});
#{f} = SURFACE_SIDE_STYLE('',(#{e}));
#{g_} = SURFACE_STYLE_USAGE(.BOTH.,#{f});
#{h} = PRESENTATION_STYLE_ASSIGNMENT((#{g_}));
#{i} = STYLED_ITEM('colour',(#{h}),#{solid});
"""


def colourise(path, colours):
    """colours = one (r, g, b) per MANIFOLD_SOLID_BREP, in file order."""
    txt = open(path).read()
    stmts = txt.split(";")
    solids = [m.group(1) for s in stmts
              if (m := re.match(r"\s*#(\d+)\s*=\s*MANIFOLD_SOLID_BREP", s))]
    ctx = next(m.group(1) for s in stmts
               if "GEOMETRIC_REPRESENTATION_CONTEXT" in s
               and (m := re.match(r"\s*#(\d+)\s*=", s)))
    if len(solids) != len(colours):
        raise RuntimeError(f"{path}: {len(solids)} solids, "
                           f"{len(colours)} colours")
    nid = max(int(m) for m in re.findall(r"#(\d+)\s*=", txt)) + 1
    block, styled = "", []
    for solid, (r, g, b) in zip(solids, colours):
        ids = {k: nid + j for j, k in enumerate("a c d e f g_ h i".split())}
        nid += 8
        block += _STYLE.format(r=r, g=g, b=b, solid=solid, **ids)
        styled.append(f"#{ids['i']}")
    block += (f"#{nid} = MECHANICAL_DESIGN_GEOMETRIC_PRESENTATION_"
              f"REPRESENTATION('',({','.join(styled)}),#{ctx});\n")
    head, sep, tail = txt.rpartition("ENDSEC;\nEND-ISO-10303-21;")
    if not sep:
        raise RuntimeError(f"{path}: unexpected STEP tail")
    open(path, "w").write(head + block + sep + tail)


def write_step(path, parts):
    """parts = [([shapes], (r, g, b)), ...] -> AP214 STEP, colour per group."""
    builder, comp = BRep_Builder(), TopoDS_Compound()
    builder.MakeCompound(comp)
    colours = []
    for shapes, rgb in parts:
        for s in shapes:
            builder.Add(comp, s)
            colours += [rgb] * n_solids(s)
    Interface_Static.SetCVal("write.step.schema", "AP214IS")
    Interface_Static.SetCVal("write.step.unit", "MM")
    writer = STEPControl_Writer()
    writer.Transfer(comp, STEPControl_AsIs)
    if writer.Write(path) != IFSelect_RetDone:
        raise RuntimeError(f"STEP write failed: {path}")
    colourise(path, colours)


# ${KIPRJMOD} is per project, so the 6-way ZH part is written into both trees.
MODELS = [
    ("JST_ZH_B2B-ZR_1x02_P1.50mm_Vertical.step", [MAIN_3D],
     lambda: jst_zh(2)),                                    # J11
    ("JST_ZH_B6B-ZR_1x06_P1.50mm_Vertical.step", [MAIN_3D, SENSOR_3D],
     lambda: jst_zh(6)),                                    # J7/J10, sensor J1
    ("Cantherm_SDF_TCO.step", [MAIN_3D], tco_sdf),          # F1
]


def main():
    for name, dirs, build in MODELS:
        parts = build()
        for d in dirs:
            path = os.path.abspath(os.path.join(d, name))
            write_step(path, parts)
            print(f"wrote {path} ({os.path.getsize(path) // 1024} KB)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
