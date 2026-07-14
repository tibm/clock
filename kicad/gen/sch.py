"""Schematic document builder for KiCad 10 (.kicad_sch).

Label-driven netlisting: components are placed on a grid; nets are made by
attaching global/local labels to pin endpoints via short stubs. Geometry for
pins comes from schlib.SymbolCache so labels land exactly on the pins.
"""
from __future__ import annotations
import math
import uuid as _uuid
from sexp import Node, Atom, QStr, dumps
from schlib import SymbolCache

NS = _uuid.UUID("12345678-1234-5678-1234-567812345678")

# Standard KiCad footprint identifiers used across the board.
FP = {
    "R0603": "Resistor_SMD:R_0603_1608Metric",
    "R0805": "Resistor_SMD:R_0805_2012Metric",
    "R1206": "Resistor_SMD:R_1206_3216Metric",
    "R2010": "Resistor_SMD:R_2010_5025Metric",
    "R2512": "Resistor_SMD:R_2512_6332Metric",
    "C0603": "Capacitor_SMD:C_0603_1608Metric",
    "C0805": "Capacitor_SMD:C_0805_2012Metric",
    "C1206": "Capacitor_SMD:C_1206_3216Metric",
    "C1210": "Capacitor_SMD:C_1210_3225Metric",
    "CP_bulk": "Capacitor_SMD:CP_Elec_6.3x7.7",
    "L4030": "Inductor_SMD:L_Coilcraft_XAL4030-XXX",
    "L4020": "Inductor_SMD:L_Coilcraft_XAL4020-XXX",
    "SMA": "Diode_SMD:D_SMA",
    "SOD123": "Diode_SMD:D_SOD-123",
    "SOD323": "Diode_SMD:D_SOD-323",
    "SOT23": "Package_TO_SOT_SMD:SOT-23",
    "SOT23-3": "Package_TO_SOT_SMD:SOT-23",
    "SOT23-5": "Package_TO_SOT_SMD:SOT-23-5",
    "SOT23-6": "Package_TO_SOT_SMD:SOT-23-6",
}


def uid(seed: str) -> str:
    return str(_uuid.uuid5(NS, seed))


def A(*xs):
    """Build a Node from tag + children, coercing plain str/num to Atom."""
    n = Node()
    for i, x in enumerate(xs):
        if isinstance(x, (Node, QStr, Atom)):
            n.append(x)
        elif isinstance(x, float):
            # trim trailing zeros like KiCad
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
        eff.append(A("justify", Atom(justify)))
    if hide:
        eff.append(A("hide", Atom("yes")))
    return A("property", QStr(name), QStr(value), A("at", x, y, rot), eff)


# direction unit vectors in schematic coords (y down) keyed by angle
_DIR = {0: (1, 0), 90: (0, 1), 180: (-1, 0), 270: (0, -1)}


class Comp:
    def __init__(self, ref, lib_id, x, y, value, footprint, unit, rot, mirror,
                 fields, cache, is_power=False):
        self.ref = ref
        self.lib_id = lib_id
        self.x = x
        self.y = y
        self.value = value
        self.footprint = footprint
        self.unit = unit
        self.rot = rot
        self.mirror = mirror
        self.fields = fields or {}
        self.cache = cache
        self.is_power = is_power
        self.uuid = uid(f"comp:{ref}:{lib_id}:{x}:{y}")

    def pin_etype(self, number):
        return self.cache.pins(self.lib_id, self.unit)[number]["etype"]

    def _xf(self, x, y):
        """Library (Y-up) point -> schematic (Y-down) point for this instance's
        placement (rot in {0,90,180,270}, optional mirror x|y)."""
        bx, by = x, -y                       # Y flip (lib -> sch base)
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
        px, py, lib_angle = p["x"], p["y"], p["angle"]
        cx, cy = self._xf(px, py)
        # a point 1 mm toward the body (opposite the pin direction) to get outward
        a = math.radians(lib_angle)
        bx, by = self._xf(px - math.cos(a), py - math.sin(a))
        dx, dy = cx - bx, cy - by
        if abs(dx) >= abs(dy):
            outward = 0 if dx > 0 else 180
        else:
            outward = 90 if dy > 0 else 270
        return round(cx, 4), round(cy, 4), outward


class Schematic:
    # project-global counters so #PWR/#FLG refs are unique across all sheets
    _pwr_count = 0
    _flg_count = 0

    def __init__(self, cache: SymbolCache, name: str, root_uuid: str,
                 sheet_symbol_uuid: str | None, project: str, page: str):
        self.cache = cache
        self.name = name
        self.root_uuid = root_uuid
        self.sheet_symbol_uuid = sheet_symbol_uuid  # None => this is the root
        self.project = project
        self.page = page
        self.uuid = uid(f"file:{name}")
        self.comps: list[Comp] = []
        self.nodes = []       # (comp, pin, netname) — logical net declarations
        self._fy = 40.0       # cursor for the auto power-flag strip
        self.wires = []
        self.labels = []      # (kind, text, x, y, rot)
        self.juncs = []
        self.noconns = []
        self.texts = []       # (text, x, y, rot, size)
        self.sheets = []      # child sheet symbols (root only)
        self.rects = []

    # ---- placement ----
    def comp(self, ref, lib_id, x, y, value=None, footprint="", unit=1,
             rot=0, mirror=None, fields=None, is_power=False):
        # snap origin to the 1.27 mm connection grid so pins land on-grid
        x = round(x / 1.27) * 1.27
        y = round(y / 1.27) * 1.27
        c = Comp(ref, lib_id, x, y,
                 value if value is not None else lib_id.split(":", 1)[1],
                 footprint, unit, rot, mirror, fields, self.cache, is_power)
        self.comps.append(c)
        return c

    # ---- passive/discrete convenience creators ----
    def R(self, ref, x, y, value, fp="R0603"):
        return self.comp(ref, "Device:R", x, y, value, FP.get(fp, fp))

    def C(self, ref, x, y, value, fp="C0603"):
        return self.comp(ref, "Device:C", x, y, value, FP.get(fp, fp))

    def CP(self, ref, x, y, value, fp="CP_bulk"):
        return self.comp(ref, "Device:C_Polarized", x, y, value, FP.get(fp, fp))

    def L(self, ref, x, y, value, fp="L4030"):
        return self.comp(ref, "Device:L", x, y, value, FP.get(fp, fp))

    def D_schottky(self, ref, x, y, value, fp="SMA"):
        return self.comp(ref, "Device:D_Schottky", x, y, value, FP.get(fp, fp))

    def D_tvs(self, ref, x, y, value, fp="SMA"):
        return self.comp(ref, "Device:D_TVS", x, y, value, FP.get(fp, fp))

    def D(self, ref, x, y, value, fp="SOD123"):
        return self.comp(ref, "Device:D", x, y, value, FP.get(fp, fp))

    def nmos(self, ref, x, y, value="2N7002", fp="SOT23"):
        return self.comp(ref, "Transistor_FET:2N7002", x, y, value, FP.get(fp, fp))

    def ntc(self, ref, x, y, value, fp="R0603"):
        return self.comp(ref, "Device:Thermistor_NTC", x, y, value, FP.get(fp, fp))

    def fuse(self, ref, x, y, value, fp=""):
        return self.comp(ref, "Device:Fuse", x, y, value, fp)

    def wire(self, x1, y1, x2, y2):
        if abs(x1 - x2) < 0.05 and abs(y1 - y2) < 0.05:
            return                      # skip degenerate/zero-length wires
        self.wires.append((x1, y1, x2, y2))

    def junction(self, x, y):
        self.juncs.append((x, y))

    def noconn(self, x, y):
        self.noconns.append((x, y))

    def label(self, text, x, y, rot=0, kind="global"):
        self.labels.append((kind, text, x, y, rot))

    def text(self, s, x, y, rot=0, size=1.5):
        self.texts.append((s, x, y, rot, size))

    def rect(self, x1, y1, x2, y2):
        self.rects.append((x1, y1, x2, y2))

    # nets rendered as power-port symbols (span the whole hierarchy)
    POWER_NETS = {"GND", "+3V3", "+5V", "+12V", "VBUS", "VBAT", "PVDD"}
    _PWR_LIB = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V",
                "+12V": "power:+12V", "VBUS": "power:VBUS", "VBAT": "clock:VBAT",
                "PVDD": "clock:PVDD"}

    # ---- logical-net declaration (rendered later by finalize) ----
    def node(self, comp, pin, net):
        """Declare that a component pin belongs to a net. Rendering (power
        symbol / cross-sheet label / on-page wire) is decided in finalize()."""
        self.nodes.append((comp, pin, net))

    # back-compat aliases: both just declare a node now
    def net(self, comp, pin, text, **kw):
        self.node(comp, pin, text)

    def power(self, comp, pin, sym, **kw):
        self.node(comp, pin, sym)

    def nc(self, comp, pin):
        """Place a no-connect flag on a pin endpoint."""
        x, y, _ = comp.pin_xy(pin)
        self.noconn(x, y)

    def power_at(self, x, y, sym):
        lib = self._PWR_LIB[sym]
        Schematic._pwr_count += 1
        ref = f"#PWR{Schematic._pwr_count:03d}"
        self.comp(ref, lib, x, y, value=sym, unit=1, rot=0, is_power=True)
        return x, y

    def pwr_flag(self, x, y):
        Schematic._flg_count += 1
        ref = f"#FLG{Schematic._flg_count:03d}"
        self.comp(ref, "power:PWR_FLAG", x, y, value="PWR_FLAG", unit=1,
                  rot=0, is_power=True)

    def flag(self, net, x, y):
        """A rail/GND power symbol + PWR_FLAG wired together (marks a source /
        derived-supply net as driven for ERC). Self-contained; not a node."""
        x = round(x / 1.27) * 1.27       # keep on grid so the wire meets the pins
        y = round(y / 1.27) * 1.27
        self.power_at(x, y, net)
        self.wire(x, y, x + 5.08, y)
        self.pwr_flag(x + 5.08, y)

    def rail_flag(self, net):
        """Auto-placed power-rail flag in a right-margin strip (once per rail)."""
        self.flag(net, 372.11, self._fy)
        self._fy += 10.16

    def local_flag(self, net, comp, pin):
        """PWR_FLAG for an on-page supply net (e.g. a resistor-fed VDD): placed
        just below the given member pin and declared onto the net so the router
        wires it in."""
        x, y, _ = comp.pin_xy(pin)
        self.pwr_flag(x, y + 10.16)
        # node the flag's pin onto the net (its pin sits at y+10.16)
        fl = self.comps[-1]
        self.node(fl, "1", net)

    # ---- rendering (called by finalize) ----
    def _stub(self, comp, pin, length=2.54):
        x, y, outward = comp.pin_xy(pin)
        dx, dy = _DIR[outward]
        return x, y, x + dx * length, y + dy * length, outward

    def _render_power(self, comp, pin, net):
        x, y, ex, ey, _ = self._stub(comp, pin)
        self.wire(x, y, ex, ey)
        self.power_at(ex, ey, net)

    _LROT = {0: 0, 180: 180, 270: 90, 90: 270}
    WIRE_SPAN = 20.0   # cluster tighter than this -> wire; else use labels

    def _render_label(self, comp, pin, net, kind="global"):
        x, y, ex, ey, outward = self._stub(comp, pin)
        self.wire(x, y, ex, ey)
        self.label(net, ex, ey, self._LROT[outward], kind)

    def _span(self, members):
        pts = [c.pin_xy(p)[:2] for c, p in members]
        s = 0.0
        for i in range(len(pts)):
            for j in range(i + 1, len(pts)):
                s = max(s, abs(pts[i][0] - pts[j][0]) + abs(pts[i][1] - pts[j][1]))
        return s

    def _render_global(self, net, members):
        """Cross-sheet net. Compact members are wired together with ONE label;
        spread members each get their own global label (never cross-merges)."""
        if len(members) > 1 and self._span(members) <= self.WIRE_SPAN:
            self._wire_cluster(members)
            c, p = members[0]
            x, y, outward = c.pin_xy(p)
            dx, dy = _DIR[outward]
            lx, ly = x + dx * 5.08, y + dy * 5.08
            self.wire(x, y, lx, ly)
            self.label(net, lx, ly, self._LROT[outward], "global")
        else:
            for c, p in members:
                self._render_label(c, p, net, "global")

    def _route_local(self, net, members):
        """On-page net: wire clustered members; spread members get local labels
        (connect by name within the sheet, never cross-merge with other wires)."""
        if len(members) > 1 and self._span(members) <= self.WIRE_SPAN:
            self._wire_cluster(members)
        else:
            for c, p in members:
                self._render_label(c, p, net, "local")

    def _wire_cluster(self, members):
        """Orthogonally wire a compact set of member pins."""
        stubs = []
        for c, p in members:
            x, y, ex, ey, _ = self._stub(c, p)
            self.wire(x, y, ex, ey)
            stubs.append((ex, ey))
        if len(stubs) == 2:
            (x1, y1), (x2, y2) = stubs
            if x1 == x2 or y1 == y2:
                self.wire(x1, y1, x2, y2)
            else:
                self.wire(x1, y1, x1, y2)
                self.wire(x1, y2, x2, y2)
            return
        xs = sorted(s[0] for s in stubs)
        ys = [s[1] for s in stubs]
        tx = xs[len(xs) // 2]
        self.wire(tx, min(ys), tx, max(ys))
        for sx, sy in stubs:
            if sx != tx:
                self.wire(sx, sy, tx, sy)
            self.junction(tx, sy)

    def finalize(self, global_nets):
        from collections import defaultdict
        bynet = defaultdict(list)
        for c, p, net in self.nodes:
            bynet[net].append((c, p))
        for net, members in bynet.items():
            if net in self.POWER_NETS:
                for c, p in members:
                    self._render_power(c, p, net)
            elif net in global_nets:
                self._render_global(net, members)
            else:
                self._route_local(net, members)

    def add_sheet(self, name, filename, x, y, w=40, h=20):
        su = uid(f"sheet:{name}")
        self.sheets.append(dict(name=name, file=filename, x=x, y=y, w=w, h=h,
                                uuid=su))
        return su

    # ---- serialization ----
    def _lib_symbols(self):
        used = []
        seen = set()          # by embedded symbol name (incl. parents)
        for c in self.comps:
            for n in self.cache.embed_nodes(c.lib_id):
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
        in_bom = "no" if c.is_power else "yes"
        n = A("symbol",
              A("lib_id", QStr(c.lib_id)),
              A("at", c.x, c.y, c.rot))
        if c.mirror in ("x", "y"):
            n.append(A("mirror", Atom(c.mirror)))
        n.extend([
              A("unit", c.unit),
              A("exclude_from_sim", Atom("no")),
              A("in_bom", Atom(in_bom)),
              A("on_board", Atom("yes")),
              A("dnp", Atom("no")),
              A("uuid", QStr(c.uuid))])
        if c.is_power:
            # reference hidden; value = net name shown
            n.append(prop("Reference", c.ref, c.x + 2.54, c.y, hide=True))
            n.append(prop("Value", c.value, c.x + 2.54, c.y, hide=(c.value == "PWR_FLAG"),
                          justify="left"))
            n.append(prop("Footprint", "", c.x, c.y, hide=True))
            n.append(prop("Datasheet", "~", c.x, c.y, hide=True))
        else:
            n.append(prop("Reference", c.ref, c.x, c.y - 5.08, hide=False,
                          justify="left"))
            n.append(prop("Value", c.value, c.x, c.y + 5.08, hide=False,
                          justify="left"))
            n.append(prop("Footprint", c.footprint, c.x, c.y + 7.62, hide=True))
            n.append(prop("Datasheet", "~", c.x, c.y, hide=True))
            yoff = 10.16
            for k, v in c.fields.items():
                n.append(prop(k, v, c.x, c.y + yoff, hide=True))
                yoff += 2.54
        # instances
        path = "/" + self.root_uuid
        if self.sheet_symbol_uuid:
            path += "/" + self.sheet_symbol_uuid
        n.append(A("instances",
                   A("project", QStr(self.project),
                     A("path", QStr(path),
                       A("reference", QStr(c.ref)),
                       A("unit", c.unit)))))
        return n

    def _label_node(self, kind, text, x, y, rot):
        tag = {"global": "global_label", "local": "label",
               "hier": "hierarchical_label"}[kind]
        n = A(tag, QStr(text), A("at", x, y, rot))
        if kind == "global":
            n.append(A("shape", Atom("bidirectional")))
        n.append(A("effects", A("font", A("size", 1.27, 1.27)),
                  A("justify", Atom("left"))))
        n.append(A("uuid", QStr(uid(f"label:{self.name}:{text}:{x}:{y}"))))
        return n

    def _sheet_node(self, s):
        n = A("sheet",
              A("at", s["x"], s["y"]),
              A("size", s["w"], s["h"]),
              A("exclude_from_sim", Atom("no")),
              A("in_bom", Atom("yes")),
              A("on_board", Atom("yes")),
              A("dnp", Atom("no")),
              A("stroke", A("width", 0.1524), A("type", Atom("solid"))),
              A("fill", A("color", 0, 0, 0, 0.0)),
              A("uuid", QStr(s["uuid"])),
              prop("Sheetname", s["name"], s["x"], s["y"] - 1.0, hide=False,
                   justify="left"),
              prop("Sheetfile", s["file"], s["x"], s["y"] + s["h"] + 1.0,
                   hide=False, justify="left"))
        return n

    def render(self) -> str:
        root = Node()
        root.append(Atom("kicad_sch"))
        root.append(A("version", 20250114))
        root.append(A("generator", QStr("clock_gen")))
        root.append(A("generator_version", QStr("10.0")))
        root.append(A("uuid", QStr(self.uuid)))
        root.append(A("paper", QStr("A3")))
        root.append(self._lib_symbols())
        # graphics
        for (x1, y1, x2, y2) in self.rects:
            root.append(A("rectangle", A("start", x1, y1), A("end", x2, y2),
                          A("stroke", A("width", 0.127), A("type", Atom("default"))),
                          A("fill", A("type", Atom("none"))),
                          A("uuid", QStr(uid(f"rect:{self.name}:{x1}:{y1}")))))
        for (x1, y1, x2, y2) in self.wires:
            root.append(A("wire", A("pts", A("xy", x1, y1), A("xy", x2, y2)),
                          A("stroke", A("width", 0), A("type", Atom("default"))),
                          A("uuid", QStr(uid(f"wire:{self.name}:{x1}:{y1}:{x2}:{y2}")))))
        for (x, y) in self.juncs:
            root.append(A("junction", A("at", x, y), A("diameter", 0),
                          A("color", 0, 0, 0, 0),
                          A("uuid", QStr(uid(f"junc:{self.name}:{x}:{y}")))))
        for (x, y) in self.noconns:
            root.append(A("no_connect", A("at", x, y),
                          A("uuid", QStr(uid(f"nc:{self.name}:{x}:{y}")))))
        for (s, x, y, rot, size) in self.texts:
            root.append(A("text", QStr(s), A("at", x, y, rot),
                          A("effects", A("font", A("size", size, size)),
                            A("justify", Atom("left"))),
                          A("uuid", QStr(uid(f"text:{self.name}:{x}:{y}:{s[:8]}")))))
        for (kind, text, x, y, rot) in self.labels:
            root.append(self._label_node(kind, text, x, y, rot))
        for c in self.comps:
            root.append(self._comp_node(c))
        for s in self.sheets:
            root.append(self._sheet_node(s))
        # sheet_instances
        si = A("sheet_instances")
        if self.sheet_symbol_uuid is None:
            si.append(A("path", QStr("/"), A("page", QStr(self.page))))
            for i, s in enumerate(self.sheets):
                si.append(A("path", QStr("/" + s["uuid"]),
                            A("page", QStr(str(i + 2)))))
        else:
            si.append(A("path", QStr("/"), A("page", QStr(self.page))))
        root.append(si)
        root.append(A("embedded_fonts", Atom("no")))
        return dumps(root) + "\n"


def _pwr_net(sym):
    return {"GND": "GND", "GNDA": "GND", "GNDPWR": "GND"}.get(sym, sym)
