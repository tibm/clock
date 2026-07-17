"""Single-page schematic builder for KiCad 10 — fully MANUAL placement/routing.

Unlike the kicad/ generator (auto label stubs + cluster router), every wire,
label, power symbol and text here is placed explicitly by the layout code.
The builder only provides:
  - exact pin geometry (via schlib.SymbolCache) so routes bind to pins,
  - polyline routing helpers whose bend coordinates DERIVE from pin positions
    (never hand-copied, so nothing can be off by a fraction of the grid),
  - automatic junction dots at T-points,
  - a lint pass (dangling wires, body overlaps, wires through bodies).

Grid: everything must land on the 1.27 mm connection grid. comp() asserts it.
"""
from __future__ import annotations
import math
import uuid as _uuid
from sexp import Node, Atom, QStr, dumps
from schlib import SymbolCache

NS = _uuid.UUID("87654321-4321-8765-4321-876543218765")

FP = {
    "R0603": "Resistor_SMD:R_0603_1608Metric",
    "R0805": "Resistor_SMD:R_0805_2012Metric",
    "R2010": "Resistor_SMD:R_2010_5025Metric",
    "C0603": "Capacitor_SMD:C_0603_1608Metric",
    "C0805": "Capacitor_SMD:C_0805_2012Metric",
    "C1210": "Capacitor_SMD:C_1210_3225Metric",
    "CP_bulk": "Capacitor_SMD:CP_Elec_6.3x7.7",
    "L4030": "Inductor_SMD:L_Coilcraft_XAL4030-XXX",
    "L4020": "Inductor_SMD:L_Coilcraft_XAL4020-XXX",
    "SMA": "Diode_SMD:D_SMA",
    "SOD123": "Diode_SMD:D_SOD-123",
    "SOT23-3": "Package_TO_SOT_SMD:SOT-23",
    "SOT23-5": "Package_TO_SOT_SMD:SOT-23-5",
    "SOT23-6": "Package_TO_SOT_SMD:SOT-23-6",
}

GRID = 1.27


def uid(seed: str) -> str:
    return str(_uuid.uuid5(NS, seed))


def ongrid(v: float) -> bool:
    return abs(v / GRID - round(v / GRID)) < 1e-4


def snap(v: float) -> float:
    return round(round(v / GRID) * GRID, 4)


def A(*xs):
    n = Node()
    for x in xs:
        if isinstance(x, (Node, QStr, Atom)):
            n.append(x)
        elif isinstance(x, float):
            s = ("%f" % x).rstrip("0").rstrip(".")
            n.append(Atom(s if s not in ("", "-0") else "0"))
        elif isinstance(x, int):
            n.append(Atom(str(x)))
        else:
            n.append(Atom(str(x)))
    return n


def prop(name, value, x, y, rot=0, hide=True, size=1.27, justify=None):
    eff = A("effects", A("font", A("size", size, size)))
    if justify:
        eff.append(A("justify", *[Atom(j) for j in justify.split()]))
    if hide:
        eff.append(A("hide", Atom("yes")))
    return A("property", QStr(name), QStr(value), A("at", x, y, rot), eff)


_DIR = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}  # sch coords, y down


class Comp:
    def __init__(self, ref, lib_id, x, y, value, footprint, unit, rot, mirror,
                 cache, is_power=False, refpos=None, valpos=None,
                 show_value=True, show_ref=True,
                 refabs=None, valabs=None):
        # refabs/valabs: GUI-harvested absolute (x, y, rot, justify) — win
        # over refpos/valpos and the computed defaults
        self.refabs = refabs
        self.valabs = valabs
        self.ref = ref
        self.lib_id = lib_id
        self.x = x
        self.y = y
        self.value = value
        self.footprint = footprint
        self.unit = unit
        self.rot = rot
        self.mirror = mirror
        self.cache = cache
        self.is_power = is_power
        self.refpos = refpos      # (x, y, justify) absolute
        self.valpos = valpos
        self.show_value = show_value
        self.show_ref = show_ref
        self.uuid = uid(f"comp:{ref}:{lib_id}:{x}:{y}")

    def _xf(self, x, y):
        bx, by = x, -y
        if self.mirror == "x":
            by = -by
        elif self.mirror == "y":
            bx = -bx
        r = self.rot % 360
        if r == 90:
            bx, by = by, -bx
        elif r == 180:
            bx, by = -bx, -by
        elif r == 270:
            bx, by = -by, bx
        return self.x + bx, self.y + by

    def pin_xy(self, number):
        pins = self.cache.pins(self.lib_id, self.unit)
        if number not in pins:
            raise KeyError(f"{self.ref} ({self.lib_id}) has no pin {number}; "
                           f"has {sorted(pins)}")
        p = pins[number]
        cx, cy = self._xf(p["x"], p["y"])
        # KiCad pin `at` = connection point; `angle` points TOWARD the body.
        # outward = away from the body (the side a wire/label should extend).
        a = math.radians(p["angle"])
        bx, by = self._xf(p["x"] - math.cos(a), p["y"] - math.sin(a))
        dx, dy = bx - cx, by - cy            # connection point -> outward point
        if abs(dx) >= abs(dy):
            outward = 0 if dx > 0 else 180
        else:
            outward = 90 if dy > 0 else 270
        return round(cx, 4), round(cy, 4), outward

    def pins(self):
        return self.cache.pins(self.lib_id, self.unit)

    def body_bbox(self):
        """Graphics-only bbox (no pins/text), transformed to schematic coords."""
        bb = self.cache.body_bbox(self.lib_id)
        if bb is None:
            return None
        (x0, y0), (x1, y1) = bb
        pts = [self._xf(px, py) for px in (x0, x1) for py in (y0, y1)]
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        return (min(xs), min(ys), max(xs), max(ys))


class Sch:
    def __init__(self, cache: SymbolCache, name: str, project: str,
                 paper="A1", title="", date="", rev="", company="",
                 cosmetics=None):
        self.cache = cache
        self.name = name
        self.project = project
        self.paper = paper
        self.title = title
        self.date = date
        self.rev = rev
        self.company = company
        self.uuid = uid(f"file:{name}")
        self.comps: list[Comp] = []
        self.wires = []       # (x1,y1,x2,y2)
        self.juncs = []       # manual junctions
        self.noconns = []
        self.labels = []      # (kind, text, x, y, rot)
        self.texts = []       # (text, x, y, rot, size, bold)
        self.rects = []       # (x1,y1,x2,y2)
        self._pwr = 0
        self._refs = set()
        self.parts = {}       # cross-block component handles (e.g. "U8")
        self.cosmetics = cosmetics or {}   # ref -> GUI-harvested placement

    # ---------------- placement ----------------
    def comp(self, ref, lib_id, x, y, value=None, footprint="", unit=1,
             rot=0, mirror=None, is_power=False, refpos=None, valpos=None,
             show_value=True, show_ref=True):
        refabs = valabs = None
        cos = None if is_power else self.cosmetics.get(ref)
        if cos:
            x, y, rot, mirror = cos["at"]
            assert ongrid(x) and ongrid(y), \
                f"{ref} cosmetic origin off-grid: ({x},{y})"
            refabs, valabs = cos["ref"], cos["val"]
        assert ongrid(x) and ongrid(y), f"{ref} origin off-grid: ({x},{y})"
        if not is_power:
            assert ref not in self._refs, f"duplicate ref {ref}"
            self._refs.add(ref)
        c = Comp(ref, lib_id, round(x, 4), round(y, 4),
                 value if value is not None else lib_id.split(":", 1)[1],
                 footprint, unit, rot, mirror, self.cache, is_power,
                 refpos, valpos, show_value, show_ref, refabs, valabs)
        self.comps.append(c)
        return c

    # 2-pin conveniences (vertical at rot=0: pin1 top, pin2 bottom)
    def R(self, ref, x, y, value, fp="R0603", rot=0, **kw):
        return self.comp(ref, "Device:R", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def C(self, ref, x, y, value, fp="C0603", rot=0, **kw):
        return self.comp(ref, "Device:C", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def CP(self, ref, x, y, value, fp="CP_bulk", rot=0, **kw):
        return self.comp(ref, "Device:C_Polarized", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def L(self, ref, x, y, value, fp="L4030", rot=0, **kw):
        return self.comp(ref, "Device:L", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def D(self, ref, x, y, value, fp="SOD123", rot=0, **kw):
        return self.comp(ref, "Device:D", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def D_schottky(self, ref, x, y, value, fp="SMA", rot=0, **kw):
        return self.comp(ref, "Device:D_Schottky", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def D_tvs(self, ref, x, y, value, fp="SMA", rot=0, **kw):
        return self.comp(ref, "Device:D_TVS", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def ntc(self, ref, x, y, value, fp="R0603", rot=0, **kw):
        return self.comp(ref, "Device:Thermistor_NTC", x, y, value, FP.get(fp, fp), rot=rot, **kw)

    def fuse(self, ref, x, y, value, fp="", rot=0, **kw):
        return self.comp(ref, "Device:Fuse", x, y, value, fp, rot=rot, **kw)

    def nmos(self, ref, x, y, value="2N7002", **kw):
        kw.setdefault("refpos", (x + 4.83, y - 1.4, "left"))
        kw.setdefault("valpos", (x - 0.76, y + 6.35, "right"))
        return self.comp(ref, "clock:2N7002", x, y, value, FP["SOT23-3"], **kw)

    # ---------------- geometry helpers ----------------
    def pxy(self, c, p):
        x, y, _ = c.pin_xy(p)
        return x, y

    # ---------------- wiring ----------------
    def seg(self, x1, y1, x2, y2):
        if abs(x1 - x2) < 0.02 and abs(y1 - y2) < 0.02:
            return
        assert abs(x1 - x2) < 0.02 or abs(y1 - y2) < 0.02, \
            f"non-orthogonal wire ({x1},{y1})-({x2},{y2})"
        for v in (x1, y1, x2, y2):
            assert ongrid(v), f"wire point off-grid: ({x1},{y1})-({x2},{y2})"
        self.wires.append((round(x1, 4), round(y1, 4), round(x2, 4), round(y2, 4)))

    def w(self, *pts):
        """Wire through absolute points (must be orthogonal)."""
        for a, b in zip(pts, pts[1:]):
            self.seg(a[0], a[1], b[0], b[1])

    def _walk(self, x, y, steps):
        """Yield successive points from (x,y) applying steps."""
        pts = [(x, y)]
        for st in steps:
            k = st[0]
            if k == "x":
                x = snap(st[1])
            elif k == "y":
                y = snap(st[1])
            elif k == "dx":
                assert ongrid(st[1]), f"dx off grid: {st[1]}"
                x = round(x + st[1], 4)
            elif k == "dy":
                assert ongrid(st[1]), f"dy off grid: {st[1]}"
                y = round(y + st[1], 4)
            elif k == "px":
                x = st[1].pin_xy(st[2])[0]
            elif k == "py":
                y = st[1].pin_xy(st[2])[1]
            elif k == "pin":
                tx, ty, _ = st[1].pin_xy(st[2])
                if abs(tx - x) > 0.02 and abs(ty - y) > 0.02:
                    pts.append((tx, y))   # H then V
                x, y = tx, ty
            else:
                raise ValueError(f"bad step {st}")
            pts.append((x, y))
        return pts

    def pw(self, c, p, *steps):
        """Wire starting at pin endpoint of (c,p), applying steps."""
        x, y, _ = c.pin_xy(p)
        pts = self._walk(x, y, steps)
        self.w(*pts)
        return pts[-1]

    def route(self, c1, p1, c2, p2, mode="H", at=None):
        """Pin-to-pin orthogonal route. mode: H, V, HV, VH, HVH(at=x), VHV(at=y)."""
        x1, y1, _ = c1.pin_xy(p1)
        x2, y2, _ = c2.pin_xy(p2)
        if mode == "H":
            assert abs(y1 - y2) < 0.02, f"{c1.ref}.{p1}->{c2.ref}.{p2} not horizontal ({y1} vs {y2})"
            self.seg(x1, y1, x2, y2)
        elif mode == "V":
            assert abs(x1 - x2) < 0.02, f"{c1.ref}.{p1}->{c2.ref}.{p2} not vertical ({x1} vs {x2})"
            self.seg(x1, y1, x2, y2)
        elif mode == "HV":
            self.w((x1, y1), (x2, y1), (x2, y2))
        elif mode == "VH":
            self.w((x1, y1), (x1, y2), (x2, y2))
        elif mode == "HVH":
            xm = snap(at)
            self.w((x1, y1), (xm, y1), (xm, y2), (x2, y2))
        elif mode == "VHV":
            ym = snap(at)
            self.w((x1, y1), (x1, ym), (x2, ym), (x2, y2))
        else:
            raise ValueError(mode)

    def junction(self, x, y):
        self.juncs.append((round(x, 4), round(y, 4)))

    def jpin(self, c, p):
        x, y, _ = c.pin_xy(p)
        self.junction(x, y)

    def nc(self, c, p):
        x, y, _ = c.pin_xy(p)
        self.noconns.append((x, y))

    # ---------------- power symbols / flags ----------------
    _PWR_LIB = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V",
                "+12V": "power:+12V", "VBUS": "power:VBUS", "VBAT": "clock:VBAT",
                "PVDD": "clock:PVDD"}

    def power_at(self, x, y, net, rot=0, show_value=True):
        assert ongrid(x) and ongrid(y), f"power {net} off-grid ({x},{y})"
        self._pwr += 1
        vp = None
        if net == "GND":
            vp = (x, y + 3.81, None)  # centred under the glyph
        elif rot == 180:
            vp = (x, y + 3.81, None)  # inverted rail: text under the glyph
        return self.comp(f"#PWR{self._pwr:03d}", self._PWR_LIB[net], x, y,
                         value=net, rot=rot, is_power=True, valpos=vp,
                         show_value=show_value)

    def gnd(self, c, p, drop=2.54, via=None, show_value=True):
        """Pin -> GND symbol below. Pin may face any direction; `via` gives an
        optional intermediate bend (dx) sideways before dropping."""
        x, y, o = c.pin_xy(p)
        if o == 90 and via is None:      # facing down: straight drop
            self.w((x, y), (x, y + drop))
            self.power_at(x, y + drop, "GND", show_value=show_value)
            return
        dx = via if via is not None else (2.54 if o == 0 else -2.54 if o == 180 else 0)
        ex = round(x + dx, 4)
        self.w((x, y), (ex, y), (ex, y + drop))
        self.power_at(ex, y + drop, "GND", show_value=show_value)

    def rail(self, c, p, net, rise=2.54, via=None):
        """Pin -> rail symbol above."""
        x, y, o = c.pin_xy(p)
        if o == 270 and via is None:
            self.w((x, y), (x, y - rise))
            self.power_at(x, y - rise, net)
            return
        dx = via if via is not None else (2.54 if o == 0 else -2.54 if o == 180 else 0)
        ex = round(x + dx, 4)
        self.w((x, y), (ex, y), (ex, y - rise))
        self.power_at(ex, y - rise, net)

    def pwr_flag(self, x, y, drop=False):
        """PWR_FLAG symbol at point (pin points down onto the location)."""
        assert ongrid(x) and ongrid(y)
        self._pwr += 1
        self.comp(f"#FLG{self._pwr:03d}", "power:PWR_FLAG", x, y,
                  value="PWR_FLAG", is_power=True)

    # ---------------- labels / text ----------------
    _LROT = {0: 0, 180: 180, 270: 90, 90: 270}

    def glabel(self, c, p, text, stub=2.54, shape="bidirectional"):
        """Global label on a stub extending outward from the pin."""
        x, y, o = c.pin_xy(p)
        dx, dy = _DIR[o]
        ex, ey = round(x + dx * stub, 4), round(y + dy * stub, 4)
        self.w((x, y), (ex, ey))
        self.glabel_at(text, ex, ey, self._LROT[o], shape)
        return ex, ey

    def glabel_at(self, text, x, y, rot=0, shape="bidirectional"):
        assert ongrid(x) and ongrid(y), f"label {text} off-grid ({x},{y})"
        self.labels.append(("global", text, round(x, 4), round(y, 4), rot, shape))

    def label_at(self, text, x, y, rot=0):
        self.labels.append(("local", text, round(x, 4), round(y, 4), rot, None))

    def text(self, s, x, y, rot=0, size=1.5, bold=False):
        self.texts.append((s, x, y, rot, size, bold))

    def rect(self, x1, y1, x2, y2):
        self.rects.append((x1, y1, x2, y2))

    def frame(self, x1, y1, x2, y2, title):
        """Section frame with a title in the top-left corner."""
        self.rect(x1, y1, x2, y2)
        self.text(title, x1 + 2.5, y1 + 4.2, size=2.6, bold=True)

    # ---------------- finalize ----------------
    @staticmethod
    def _interior(pt, seg):
        (x, y), (x1, y1, x2, y2) = pt, seg
        if abs(x1 - x2) < 0.02:  # vertical
            return abs(x - x1) < 0.02 and min(y1, y2) + 0.02 < y < max(y1, y2) - 0.02
        return abs(y - y1) < 0.02 and min(x1, x2) + 0.02 < x < max(x1, x2) - 0.02

    def _pin_points(self):
        """Point -> number of DISTINCT components with a pin there (stacked
        same-symbol pins, e.g. TB6612 AO1 = pins 1+2, count once — that is
        how eeschema weighs them for junction placement)."""
        from collections import defaultdict
        comps_at = defaultdict(set)
        for c in self.comps:
            for num in c.pins():
                x, y, _ = c.pin_xy(num)
                comps_at[(x, y)].add(id(c))
        return {pt: len(s) for pt, s in comps_at.items()}

    def _end_counts(self):
        from collections import defaultdict
        endcnt = defaultdict(int)
        for (x1, y1, x2, y2) in self.wires:
            endcnt[(x1, y1)] += 1
            endcnt[(x2, y2)] += 1
        return endcnt

    def _break_wires_at(self, pt):
        """Split every wire whose interior passes through pt into two segments."""
        out = []
        for seg in self.wires:
            if self._interior(pt, seg):
                (x1, y1, x2, y2) = seg
                out.append((x1, y1, pt[0], pt[1]))
                out.append((pt[0], pt[1], x2, y2))
            else:
                out.append(seg)
        self.wires = out

    def merge_collinear(self):
        """Merge abutting collinear segments whose shared endpoint carries
        nothing else (no 3rd wire, pin, junction or label) — mirrors the
        cleanup eeschema runs on save, so a GUI re-save is a no-op."""
        pinpts = self._pin_points()
        keep = set(pinpts)
        keep |= {(round(x, 4), round(y, 4)) for x, y in self.juncs}
        keep |= {(x, y) for (_, _, x, y, _, _) in self.labels}
        merged = 0
        changed = True
        while changed:
            changed = False
            endmap = {}
            for i, s in enumerate(self.wires):
                for pt in ((s[0], s[1]), (s[2], s[3])):
                    endmap.setdefault(pt, []).append(i)
            for pt, idxs in endmap.items():
                if pt in keep or len(idxs) != 2:
                    continue
                a, b = self.wires[idxs[0]], self.wires[idxs[1]]
                if idxs[0] == idxs[1]:
                    continue
                av = abs(a[0] - a[2]) < 0.02
                bv = abs(b[0] - b[2]) < 0.02
                if av != bv:
                    continue                      # perpendicular corner
                if any(self._interior(pt, s) for s in self.wires):
                    continue
                ends = [(a[0], a[1]), (a[2], a[3]), (b[0], b[1]), (b[2], b[3])]
                far = [e for e in ends if e != pt]
                if len(far) != 2:
                    continue
                (fx1, fy1), (fx2, fy2) = far
                hi, lo = sorted(idxs, reverse=True)
                del self.wires[hi]
                del self.wires[lo]
                self.wires.append((fx1, fy1, fx2, fy2))
                merged += 1
                changed = True
                break
        return merged

    def auto_junctions(self):
        """Junction placement matching eeschema's IsJunctionNeeded, so a GUI
        re-save neither adds nor deletes any:
          - a wire interior passes through AND >=1 wire end / pin on it, OR
          - >=3 items (wire ends + distinct components' pins) meet, with at
            least one wire end.
        A single wire end landing on one component's pin(s) never gets a
        junction.  Manual junctions at pure wire crossings (no endpoint/pin)
        are made canonical by BREAKING both wires there — otherwise
        eeschema's save-cleanup deletes the junction and SPLITS the net."""
        pinpts = self._pin_points()

        # canonicalize manual crossing-joins first
        for (jx, jy) in list(self.juncs):
            pt = (round(jx, 4), round(jy, 4))
            endcnt = self._end_counts()
            if endcnt.get(pt, 0) + pinpts.get(pt, 0) == 0 and \
               any(self._interior(pt, s) for s in self.wires):
                self._break_wires_at(pt)

        endcnt = self._end_counts()

        def needed(pt):
            ends = endcnt.get(pt, 0)
            pins = pinpts.get(pt, 0)
            through = any(self._interior(pt, s) for s in self.wires)
            return (through and (ends + pins) >= 1) \
                or (ends >= 1 and (ends + pins) >= 3)

        pts = set(endcnt) | set(pinpts)
        have = {(round(x, 4), round(y, 4)) for x, y in self.juncs}
        added, dropped = [], []
        for pt in sorted(have):
            if not needed(pt):
                dropped.append(pt)
                self.juncs = [j for j in self.juncs
                              if (round(j[0], 4), round(j[1], 4)) != pt]
        for pt in pts:
            if needed(pt) and pt not in have:
                self.juncs.append(pt)
                added.append(pt)
        return added, dropped

    # ---------------- lint ----------------
    def lint(self):
        issues = []
        # 1. dangling wire endpoints
        from collections import defaultdict
        endcnt = defaultdict(int)
        for s in self.wires:
            endcnt[(s[0], s[1])] += 1
            endcnt[(s[2], s[3])] += 1
        anchors = set()
        for c in self.comps:
            for num in c.pins():
                x, y, _ = c.pin_xy(num)
                anchors.add((x, y))
        for (k, t, x, y, r, sh) in self.labels:
            anchors.add((x, y))

        def on_any_interior(pt):
            # STRICT interior — an endpoint must not match its own segment
            return any(self._interior(pt, s) for s in self.wires)

        for pt, n in endcnt.items():
            if n >= 2 or pt in anchors:
                continue
            if not on_any_interior(pt):
                issues.append(f"dangling wire end at ({pt[0]},{pt[1]})")
        # 2. body overlaps
        boxes = []
        for c in self.comps:
            if c.is_power:
                continue
            bb = c.body_bbox()
            if bb:
                boxes.append((c.ref, bb))
        for i in range(len(boxes)):
            for j in range(i + 1, len(boxes)):
                (r1, a), (r2, b) = boxes[i], boxes[j]
                if a[0] < b[2] - 0.1 and b[0] < a[2] - 0.1 and \
                   a[1] < b[3] - 0.1 and b[1] < a[3] - 0.1:
                    issues.append(f"body overlap: {r1} {a} <-> {r2} {b}")
        # 3. wires crossing symbol bodies
        for (ref, (bx1, by1, bx2, by2)) in boxes:
            for (x1, y1, x2, y2) in self.wires:
                xlo, xhi = min(x1, x2), max(x1, x2)
                ylo, yhi = min(y1, y2), max(y1, y2)
                if xlo < bx2 - 0.05 and xhi > bx1 + 0.05 and \
                   ylo < by2 - 0.05 and yhi > by1 + 0.05:
                    issues.append(f"wire ({x1},{y1})-({x2},{y2}) crosses body of {ref}")
        # 4. duplicate segments
        seen = set()
        for s in self.wires:
            key = tuple(sorted([(s[0], s[1]), (s[2], s[3])]))
            if key in seen:
                issues.append(f"duplicate wire {key}")
            seen.add(key)
        return issues

    # ---------------- serialization ----------------
    def _lib_symbols(self):
        used, seen = [], set()
        for c in self.comps:
            n = self.cache.embed_node(c.lib_id)
            nm = str(n[1])
            if nm in seen:
                continue
            seen.add(nm)
            used.append(n)
        node = Node()
        node.append(Atom("lib_symbols"))
        node.extend(used)
        return node

    def _comp_node(self, c: Comp):
        n = A("symbol",
              A("lib_id", QStr(c.lib_id)),
              A("at", c.x, c.y, c.rot))
        if c.mirror in ("x", "y"):
            n.append(A("mirror", Atom(c.mirror)))
        n.extend([A("unit", c.unit),
                  A("exclude_from_sim", Atom("no")),
                  A("in_bom", Atom("no" if c.is_power else "yes")),
                  A("on_board", Atom("yes")),
                  A("dnp", Atom("no")),
                  A("uuid", QStr(c.uuid))])
        # property angle is RELATIVE to the symbol orientation -> compensate
        # so ref/value text always renders horizontal
        prot = (360 - c.rot) % 360
        if c.is_power:
            n.append(prop("Reference", c.ref, c.x + 2.54, c.y, hide=True))
            if c.valpos:
                vx, vy, vj = c.valpos
            else:
                vx, vy, vj = c.x, c.y - 3.81, None
            n.append(prop("Value", c.value, vx, vy, rot=prot,
                          hide=(c.value == "PWR_FLAG") or not c.show_value,
                          justify=vj))
            n.append(prop("Footprint", "", c.x, c.y, hide=True))
            n.append(prop("Datasheet", "~", c.x, c.y, hide=True))
        else:
            if c.refabs:
                rx, ry, rrot, rj = c.refabs
            elif c.refpos:
                rx, ry, rj = c.refpos
                rrot = prot
            else:
                (rx, ry, rj), rrot = self._default_refpos(c), prot
            n.append(prop("Reference", c.ref, rx, ry, rot=rrot,
                          hide=not c.show_ref, justify=rj))
            if c.valabs:
                vx, vy, vrot, vj = c.valabs
            elif c.valpos:
                vx, vy, vj = c.valpos
                vrot = prot
            else:
                (vx, vy, vj), vrot = self._default_valpos(c), prot
            n.append(prop("Value", c.value, vx, vy, rot=vrot,
                          hide=not c.show_value, justify=vj))
            n.append(prop("Footprint", c.footprint, c.x, c.y, hide=True))
            n.append(prop("Datasheet", "~", c.x, c.y, hide=True))
        # stable per-pin uuids: eeschema assigns random ones on load if absent,
        # which would churn the file on every open/save and every rebuild
        for num in sorted(c.pins()):
            n.append(A("pin", QStr(num),
                       A("uuid", QStr(uid(f"pin:{c.uuid}:{num}")))))
        n.append(A("instances",
                   A("project", QStr(self.project),
                     A("path", QStr("/" + self.uuid),
                       A("reference", QStr(c.ref)),
                       A("unit", c.unit)))))
        return n

    def _default_refpos(self, c):
        bb = c.body_bbox()
        if bb is None:
            return (c.x, c.y - 5.08, None)
        x1, y1, x2, y2 = bb
        w, h = x2 - x1, y2 - y1
        if w < 6 and h >= w:            # small vertical part -> text to the right
            return (x2 + 0.7, c.y - 1.4, "left")
        if h < 6 and w > h:             # small horizontal part -> above
            return (c.x, y1 - 3.4, None)
        return (x1, y1 - 3.2, "left")   # IC -> above top-left

    def _default_valpos(self, c):
        bb = c.body_bbox()
        if bb is None:
            return (c.x, c.y + 5.08, None)
        x1, y1, x2, y2 = bb
        w, h = x2 - x1, y2 - y1
        if w < 6 and h >= w:
            return (x2 + 0.7, c.y + 1.4, "left")
        if h < 6 and w > h:
            return (c.x, y2 + 1.7, None)
        return (x1, y1 - 0.85, "left")

    def _label_node(self, kind, text, x, y, rot, shape):
        tag = {"global": "global_label", "local": "label"}[kind]
        n = A(tag, QStr(text), A("at", x, y, rot))
        if kind == "global":
            n.append(A("shape", Atom(shape or "bidirectional")))
        just = "left" if rot in (0, 90) else "right"
        n.append(A("effects", A("font", A("size", 1.27, 1.27)),
                   A("justify", Atom(just))))
        n.append(A("uuid", QStr(uid(f"label:{text}:{x}:{y}"))))
        return n

    def render(self) -> str:
        root = Node()
        root.append(Atom("kicad_sch"))
        root.append(A("version", 20250114))
        root.append(A("generator", QStr("clock_gen2")))
        root.append(A("generator_version", QStr("10.0")))
        root.append(A("uuid", QStr(self.uuid)))
        root.append(A("paper", QStr(self.paper)))
        tb = A("title_block",
               A("title", QStr(self.title)),
               A("date", QStr(self.date)),
               A("rev", QStr(self.rev)),
               A("company", QStr(self.company)))
        root.append(tb)
        root.append(self._lib_symbols())
        for (x1, y1, x2, y2) in self.rects:
            root.append(A("rectangle", A("start", x1, y1), A("end", x2, y2),
                          A("stroke", A("width", 0.2), A("type", Atom("dash")),
                            A("color", 140, 140, 140, 1)),
                          A("fill", A("type", Atom("none"))),
                          A("uuid", QStr(uid(f"rect:{x1}:{y1}:{x2}:{y2}")))))
        for (x1, y1, x2, y2) in self.wires:
            root.append(A("wire", A("pts", A("xy", x1, y1), A("xy", x2, y2)),
                          A("stroke", A("width", 0), A("type", Atom("default"))),
                          A("uuid", QStr(uid(f"wire:{x1}:{y1}:{x2}:{y2}")))))
        for (x, y) in self.juncs:
            root.append(A("junction", A("at", x, y), A("diameter", 0),
                          A("color", 0, 0, 0, 0),
                          A("uuid", QStr(uid(f"junc:{x}:{y}")))))
        for (x, y) in self.noconns:
            root.append(A("no_connect", A("at", x, y),
                          A("uuid", QStr(uid(f"nc:{x}:{y}")))))
        for (s, x, y, rot, size, bold) in self.texts:
            font = A("font", A("size", size, size))
            if bold:
                font.append(A("bold", Atom("yes")))
            root.append(A("text", QStr(s), A("at", x, y, rot),
                          A("effects", font, A("justify", Atom("left"))),
                          A("uuid", QStr(uid(f"text:{x}:{y}:{s[:10]}")))))
        for (kind, text, x, y, rot, shape) in self.labels:
            root.append(self._label_node(kind, text, x, y, rot, shape))
        for c in self.comps:
            root.append(self._comp_node(c))
        si = A("sheet_instances")
        si.append(A("path", QStr("/"), A("page", QStr("1"))))
        root.append(si)
        root.append(A("embedded_fonts", Atom("no")))
        return dumps(root) + "\n"
