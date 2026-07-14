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

    def pin_xy(self, number):
        pins = self.cache.pins(self.lib_id, self.unit)
        if number not in pins:
            raise KeyError(f"{self.ref} ({self.lib_id}) has no pin {number}; "
                           f"has {sorted(pins)}")
        p = pins[number]
        px, py, lib_angle = p["x"], p["y"], p["angle"]
        if self.rot not in (0,) or self.mirror:
            raise NotImplementedError("only rot=0, no mirror supported")
        sx = self.x + px
        sy = self.y - py
        sch_pin_angle = (360 - lib_angle) % 360
        outward = (sch_pin_angle + 180) % 360
        return sx, sy, outward


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

    def net(self, comp, pin, text, kind="global", stub=2.54, add_junction=False):
        """Attach a named net to a pin via a short stub + label."""
        x, y, outward = comp.pin_xy(pin)
        dx, dy = _DIR[outward]
        ex, ey = x + dx * stub, y + dy * stub
        if stub:
            self.wire(x, y, ex, ey)
        # label rotation for readability
        lrot = {0: 0, 180: 180, 270: 90, 90: 270}[outward]
        self.label(text, ex, ey, lrot, kind)
        if add_junction:
            self.junction(x, y)
        return ex, ey

    _PWR_LIB = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V",
                "+12V": "power:+12V", "VBUS": "power:VBUS", "VBAT": "clock:VBAT"}

    def nc(self, comp, pin):
        """Place a no-connect flag on a pin endpoint."""
        x, y, _ = comp.pin_xy(pin)
        self.noconn(x, y)

    def power(self, comp, pin, sym, stub=2.54):
        """Attach a real power-port symbol (GND/+3V3/+5V/+12V/VBUS/VBAT) to a pin.
        All power symbols have their pin at local (0,0), so the symbol drops
        directly on the stub end with no rotation."""
        x, y, outward = comp.pin_xy(pin)
        dx, dy = _DIR[outward]
        ex, ey = x + dx * stub, y + dy * stub
        if stub:
            self.wire(x, y, ex, ey)
        return self.power_at(ex, ey, sym)

    def power_at(self, x, y, sym):
        lib = self._PWR_LIB[sym]
        Schematic._pwr_count += 1
        ref = f"#PWR{Schematic._pwr_count:03d}"
        self.comp(ref, lib, x, y, value=sym, unit=1, rot=0, is_power=True)
        return x, y

    def pwr_flag(self, x, y):
        """Drop a PWR_FLAG at (x,y) so ERC sees the net as driven."""
        Schematic._flg_count += 1
        ref = f"#FLG{Schematic._flg_count:03d}"
        self.comp(ref, "power:PWR_FLAG", x, y, value="PWR_FLAG", unit=1,
                  rot=0, is_power=True)

    def flag(self, net, x, y):
        """Place a PWR_FLAG + named global label to mark a net as driven (ERC).
        The label attaches to the flag pin via a short wire stub."""
        Schematic._flg_count += 1
        ref = f"#FLG{Schematic._flg_count:03d}"
        c = self.comp(ref, "power:PWR_FLAG", x, y, value="PWR_FLAG", unit=1,
                      rot=0, is_power=True)
        self.net(c, "1", net, stub=2.54)

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
              A("at", c.x, c.y, c.rot),
              A("unit", c.unit),
              A("exclude_from_sim", Atom("no")),
              A("in_bom", Atom(in_bom)),
              A("on_board", Atom("yes")),
              A("dnp", Atom("no")),
              A("uuid", QStr(c.uuid)))
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
