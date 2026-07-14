"""Load KiCad symbols from the installed standard libraries (and a local
custom lib), flatten `extends` inheritance, and expose pin geometry so the
schematic generator can place net labels exactly on pin endpoints.
"""
from __future__ import annotations
import copy
import glob
import os
from sexp import parse, Node, Atom, QStr

STD_SYM_DIR = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/symbols"
LOCAL_SYM = os.path.join(os.path.dirname(__file__), "clock_custom.kicad_sym")


class SymbolCache:
    def __init__(self, extra_libs=None):
        # nickname -> file path
        self.libfiles = {}
        for p in glob.glob(os.path.join(STD_SYM_DIR, "*.kicad_sym")):
            self.libfiles[os.path.splitext(os.path.basename(p))[0]] = p
        if os.path.exists(LOCAL_SYM):
            self.libfiles["clock"] = LOCAL_SYM
        for nick, path in (extra_libs or {}).items():
            self.libfiles[nick] = path
        self._raw = {}         # (nick) -> {name: Node}
        self._standalone = {}  # (nick, name) -> flattened Node (base-named)

    def _load_lib(self, nick):
        if nick in self._raw:
            return self._raw[nick]
        path = self.libfiles.get(nick)
        if not path:
            raise KeyError(f"unknown symbol library nickname: {nick}")
        with open(path) as f:
            root = parse(f.read())
        d = {}
        for c in root.findall("symbol"):
            d[str(c[1])] = c
        self._raw[nick] = d
        return d

    def _standalone_node(self, nick, name):
        key = (nick, name)
        if key in self._standalone:
            return self._standalone[key]
        d = self._load_lib(nick)
        if name not in d:
            raise KeyError(f"symbol {name!r} not found in lib {nick!r}")
        node = copy.deepcopy(d[name])
        ext = node.find("extends")
        if ext is not None:
            parent_name = str(ext[1])
            parent = copy.deepcopy(self._standalone_node(nick, parent_name))
            # rename parent -> this name at top and in sub-symbols
            _rename_symbol(parent, parent_name, name)
            # remove the extends marker from child props set, then overlay
            child_overrides = [c for c in node[2:] if isinstance(c, Node)
                               and c.tag != "extends"]
            _overlay(parent, child_overrides)
            flat = parent
        else:
            flat = node
        self._standalone[key] = flat
        return flat

    def embed_node(self, lib_id):
        """Return a fully-flattened Node for a schematic lib_symbols block:
        top symbol named 'LIB:NAME', sub-symbols keep base name. KiCad flattens
        `extends` on embed, so we do too (correct pin geometry)."""
        nick, name = lib_id.split(":", 1)
        flat = copy.deepcopy(self._standalone_node(nick, name))
        flat[1] = QStr(lib_id)
        return flat

    def embed_nodes(self, lib_id):
        return [self.embed_node(lib_id)]

    def pins(self, lib_id, unit=1):
        """Return dict: pin_number -> dict(name, x, y, angle, etype, shape).
        Coordinates are library-local (Y up)."""
        nick, name = lib_id.split(":", 1)
        flat = self._standalone_node(nick, name)
        result = {}
        for sub in flat.findall("symbol"):
            subname = str(sub[1])
            # subname format BASE_<unit>_<style>
            suffix = subname[len(name) + 1:] if subname.startswith(name + "_") else ""
            try:
                u, _style = (int(x) for x in suffix.split("_"))
            except ValueError:
                continue
            if u not in (0, unit):
                continue
            for pin in sub.findall("pin"):
                etype = str(pin[1])
                shape = str(pin[2])
                at = pin.find("at")
                x, y = float(at[1]), float(at[2])
                angle = float(at[3]) if len(at) > 3 else 0.0
                num_node = pin.find("number")
                name_node = pin.find("name")
                number = str(num_node[1]) if num_node else "?"
                pname = str(name_node[1]) if name_node else ""
                result[number] = dict(name=pname, x=x, y=y, angle=angle,
                                      etype=etype, shape=shape)
        return result


def _rename_symbol(node, old, new):
    node[1] = QStr(new)
    for sub in node.findall("symbol"):
        sn = str(sub[1])
        if sn.startswith(old + "_"):
            sub[1] = QStr(new + sn[len(old):])
    # fix Value property default if it equals old name
    for prop in node.findall("property"):
        if str(prop[1]) == "Value" and str(prop[2]) == old:
            prop[2] = QStr(new)


def _overlay(base, overrides):
    """Overlay override child-nodes (properties, pin_names, ...) onto base."""
    for ov in overrides:
        if ov.tag == "property":
            pname = str(ov[1])
            existing = None
            for prop in base.findall("property"):
                if str(prop[1]) == pname:
                    existing = prop
                    break
            if existing is not None:
                existing[2] = ov[2]
            else:
                # insert before first sub-symbol
                idx = next((i for i, c in enumerate(base)
                            if isinstance(c, Node) and c.tag == "symbol"), len(base))
                base.insert(idx, copy.deepcopy(ov))
        elif ov.tag in ("pin_names", "pin_numbers"):
            ex = base.find(ov.tag)
            if ex is not None:
                base[base.index(ex)] = copy.deepcopy(ov)


if __name__ == "__main__":
    c = SymbolCache()
    for lib_id in ["Device:R", "Regulator_Switching:TLV62569DBV",
                   "RF_Module:ESP32-S3-WROOM-1"]:
        p = c.pins(lib_id)
        print(f"\n{lib_id}: {len(p)} pins")
        for num in list(p)[:6]:
            d = p[num]
            print(f"  {num:>4} {d['name']:<12} at ({d['x']},{d['y']}) a={d['angle']} {d['etype']}")
