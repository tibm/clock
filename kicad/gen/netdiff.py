"""Compare connectivity of kicad2/clock.kicad_sch against the old kicad/
project: same partition of (ref, pin) into nets (net names ignored).
Whitelists the intended changes. Run after build.py.
"""
import subprocess
import sys
import os

sys.path.insert(0, os.path.dirname(__file__))
from sexp import parse  # noqa: E402

KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
SCRATCH = ("/private/tmp/claude-501/-Users-tibo-Developer-PRIVATE-clock/"
           "9a0b3077-20bd-4646-a449-dbf246521d91/scratchpad")
OLD = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                   "../../kicad/clock.kicad_sch"))
NEW = os.path.abspath(os.path.join(os.path.dirname(__file__),
                                   "../clock.kicad_sch"))

# intended differences: (ref, pin) present only in NEW / only in OLD
NEW_ONLY = {
    ("J7", "5"),        # sensor connector now carries SENSOR_INT
}
OLD_ONLY = set()


import re

# 2-pin symmetric parts: pins 1/2 interchangeable -> normalize the pin
SYMMETRIC = re.compile(r"^(R|C|L|F|RT|Y|SW)\d")


def norm(ref, pin):
    if pin in ("1", "2") and SYMMETRIC.match(ref) and not ref.startswith("SW3"):
        return (ref, "*")
    return (ref, pin)


def netlist(sch, out):
    subprocess.run([KICAD_CLI, "sch", "export", "netlist",
                    "--format", "kicadsexpr", "-o", out, sch],
                   check=True, capture_output=True)
    root = parse(open(out).read())
    nets = {}
    for n in root.find("nets").findall("net"):
        name = str(n.find("name")[1])
        members = frozenset(norm(str(x.find("ref")[1]), str(x.find("pin")[1]))
                            for x in n.findall("node")
                            if not str(x.find("ref")[1]).startswith(("#",)))
        if members:
            nets[name] = members
    return nets


def main():
    old = netlist(OLD, f"{SCRATCH}/old.net")
    new = netlist(NEW, f"{SCRATCH}/new.net")

    oldset = {members - OLD_ONLY for members in old.values()}
    newset = {members - NEW_ONLY for members in new.values()}
    oldset = {frozenset(m) for m in oldset if m}
    newset = {frozenset(m) for m in newset if m}

    # Intended topology FIX vs the old project: the old schematic ran L1
    # straight to BAT and left R_SENSE dangling between SENSE and BAT (no
    # charge current through it). New follows the LT3652 datasheet:
    # SW -> L1 -> R18 -> BAT, SENSE taps the L1/R18 node.
    oldset = {m - {("L1", "*")} if ("U2", "9") in m else m for m in oldset}
    oldset -= {frozenset({("R18", "*"), ("U2", "10")})}
    newset -= {frozenset({("L1", "*"), ("R18", "*"), ("U2", "10")})}

    ok = True
    for m in sorted(oldset - newset, key=len):
        ok = False
        print("net only in OLD:", sorted(m))
    for m in sorted(newset - oldset, key=len):
        ok = False
        print("net only in NEW:", sorted(m))
    print(f"\n{len(oldset)} old nets, {len(newset)} new nets")
    print("netdiff:", "OK — connectivity identical (modulo whitelist)"
          if ok else "DIFFERENCES FOUND")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
