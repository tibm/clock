"""Parse a KiCad netlist and print nets -> nodes. Usage: netcheck.py file.net"""
import sys
sys.path.insert(0, ".")
from sexp import parse, Node


def nets(path):
    root = parse(open(path).read())
    out = {}
    comps = {}
    design = root.find("components")
    if design:
        for comp in design.findall("comp"):
            ref = str(comp.find("ref")[1])
            val = comp.find("value")
            comps[ref] = str(val[1]) if val else ""
    netsnode = root.find("nets")
    if netsnode:
        for net in netsnode.findall("net"):
            name = str(net.find("name")[1])
            nodes = []
            for nd in net.findall("node"):
                nodes.append(f"{str(nd.find('ref')[1])}.{str(nd.find('pin')[1])}")
            out[name] = nodes
    return out, comps


if __name__ == "__main__":
    out, comps = nets(sys.argv[1])
    real = {n: v for n, v in out.items() if not n.startswith("unconnected-")}
    unc = [n for n in out if n.startswith("unconnected-")]
    print(f"components: {len(comps)}   named-nets: {len(real)}   unconnected-pins: {len(unc)}")
    for n in sorted(real):
        print(f"  {n:16} : {', '.join(out[n])}")
    if unc:
        print(f"  [unconnected]: {len(unc)} single-pin nets")
