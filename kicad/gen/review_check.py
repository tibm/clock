#!/usr/bin/env python3
"""Standing assertions for the defects found in the 2026-08-02 design review.

Runs on plain python3 (no pcbnew needed); uses kicad-cli to export the netlist.

    python3 review_check.py            # schematic checks + PCB checks
    python3 review_check.py --sch-only # skip the PCB (e.g. mid-rework)

Each check maps to a numbered finding in ../REVEIW.md.  A check that has not
been fixed yet reports TODO (expected, non-fatal); a check that WAS fixed and
has regressed reports FAIL and sets a non-zero exit code.
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.dirname(HERE)
SCH = os.path.join(KICAD_DIR, "clock.kicad_sch")
PCB = os.path.join(KICAD_DIR, "clock.kicad_pcb")
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

# Findings that are known-not-yet-fixed: reported as TODO instead of FAIL.
OPEN = {4, 5, 8, 9, 10, 24}

results = []


def check(num, name, ok, detail=""):
    results.append((num, name, ok, detail))


# --------------------------------------------------------------------------
# schematic / netlist
# --------------------------------------------------------------------------
def netlist():
    fd, path = tempfile.mkstemp(suffix=".net")
    os.close(fd)
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format",
                    "kicadsexpr", "-o", path, SCH],
                   capture_output=True, text=True, check=True)
    txt = open(path).read()
    os.unlink(path)
    nets = {}
    for m in re.finditer(r'\(net\s+\(code "\d+"\)\s+\(name "([^"]*)"\)(.*?)\n\t\t\)',
                         txt, re.S):
        nets[m.group(1)] = {f"{a}.{b}" for a, b in
                            re.findall(r'\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)',
                                       m.group(2))}
    vals = dict(re.findall(r'\(comp\s+\(ref "([^"]+)"\)\s+\(value "([^"]*)"\)', txt))
    return nets, vals


def net_of(nets, node):
    for name, members in nets.items():
        if node in members:
            return name
    return None


def to_farads(v):
    m = re.match(r"^([\d.]+)\s*([pnu]?)F?$", v.strip(), re.I)
    if not m:
        return None
    return float(m.group(1)) * {"p": 1e-12, "n": 1e-9, "u": 1e-6, "": 1.0}[m.group(2).lower()]


def sch_checks():
    nets, vals = netlist()

    # -- #1: LTC4412 pass FET must have DRAIN on the input (+5V) and SOURCE on
    #        the output (PVDD), so the body diode blocks when 12 V lifts PVDD.
    d, s = net_of(nets, "Q4.3"), net_of(nets, "Q4.2")
    check(1, "Q4 drain->+5V, source->PVDD (LTC4412 body-diode blocks)",
          d == "+5V" and s == "PVDD", f"pad3={d} pad2={s}")

    # -- #2: PBTL is pre-filter: OUTA+||OUTA- is one leg, OUTB+||OUTB- the
    #        other, and each BSTRP cap lands on its OWN leg (TI SLOS772F f.64).
    legA, legB = net_of(nets, "U9.29"), net_of(nets, "U9.20")
    pair_ok = (legA is not None and legB is not None and legA != legB
               and net_of(nets, "U9.26") == legA
               and net_of(nets, "U9.23") == legB)
    boot_ok = (net_of(nets, "C180.2") == legA and net_of(nets, "C183.2") == legB
               and net_of(nets, net_of(nets, "U9.25") and "U9.25") is not None)
    # BSTRPA-/BSTRPB+ caps: far pad must sit on the matching leg
    a_minus = next((c for c in ("C181", "C182")
                    if f"{c}.1" in nets.get(net_of(nets, "U9.25"), set())), None)
    b_plus = next((c for c in ("C181", "C182")
                   if f"{c}.1" in nets.get(net_of(nets, "U9.19"), set())), None)
    boot_ok = boot_ok and a_minus and b_plus \
        and net_of(nets, f"{a_minus}.2") == legA \
        and net_of(nets, f"{b_plus}.2") == legB
    check(2, "TAS5760M PBTL legs A+/A- and B+/B- (+ bootstraps on own leg)",
          pair_ok and boot_ok,
          f"legA={legA} legB={legB} BSTRPA-={a_minus} BSTRPB+={b_plus}")

    # -- #3: LT3652 t_EOC = C_TIMER * 4.4e6 hours; need >= ~3 h so a 3 Ah cell
    #        can finish and a flat cell can clear precondition (t_EOC/8).
    c = to_farads(vals.get("C104", ""))
    check(3, "LT3652 C_TIMER gives >= 3 h EOC / >= 22 min precondition",
          c is not None and c >= 0.68e-6,
          f"C104={vals.get('C104')} -> {c * 4.4e6:.2f} h EOC" if c else "unparsed")

    # -- #10: J7 (sensor, +3V3 on pin 2) and J10 (knob, +5V on pin 2) take the
    #         same 6-way ZH cable; swapping them puts 5 V on the sensor rail.
    check(10, "J7/J10 not both 6-pin ZH with different rail on pin 2",
          net_of(nets, "J7.2") == net_of(nets, "J10.2"),
          f"J7.2={net_of(nets, 'J7.2')} J10.2={net_of(nets, 'J10.2')}")

    # -- #15: with the divider FET off, R22 pulls the ADC node to V_cell.
    clamp = next((n for n in ("D14", "D15")
                  if net_of(nets, f"{n}.1") == "+3V3"
                  and net_of(nets, f"{n}.2") == "VBAT_SENSE"), None)
    check(15, "VBAT_SENSE clamped to +3V3 (divider-off leakage)",
          clamp is not None,
          f"clamp = {clamp}" if clamp else "no clamp diode to +3V3")

    # -- #11: the sensor board drives ALS_INT (TSL2591 INT) onto J7 pin 6.
    check(11, "J7 pin 6 (ALS_INT) landed on a GPIO, not NC",
          net_of(nets, "J7.6") == net_of(nets, "U13.4") is not None,
          f"J7.6={net_of(nets, 'J7.6')}")

    # -- #12: the sensor board carries a BNO085, not a LIS3DH.
    check(12, "J7 value names the parts actually on the sensor board",
          "BNO085" in vals.get("J7", "") and "LIS3DH" not in vals.get("J7", ""),
          vals.get("J7", ""))

    # -- #17: 100k/200k = 66.7k source impedance into PCNT across a noisy board.
    enc = [vals.get(r) for r in ("R111", "R112", "R114", "R115")]
    check(17, "encoder A/B divider <= 20k/40k", all(
        v and to_ohms(v) and to_ohms(v) <= 40e3 for v in enc), f"{enc}")


def to_ohms(v):
    m = re.match(r"^([\d.]+)\s*([kKmM]?)R?$", v.strip())
    if not m:
        return None
    return float(m.group(1)) * {"": 1, "k": 1e3, "K": 1e3, "m": 1e6, "M": 1e6}[m.group(2)]


# --------------------------------------------------------------------------
# PCB
# --------------------------------------------------------------------------
def pcb_checks():
    txt = open(PCB).read()

    # -- #4: one 0.25 mm net class for everything, incl. VBAT/+12V/PVDD/+5V.
    # only TRACK widths -- (width ...) also appears on silk/courtyard graphics
    widths = sorted({float(w) for w in
                     re.findall(r"\(segment[\s\S]{0,400}?\(width ([\d.]+)\)", txt)})
    check(4, "power nets routed wider than the 0.25 mm default",
          any(w >= 0.5 for w in widths), f"track widths present: {widths}")

    # -- #5: exposed pads are the only heat path for U7/U9/U2.
    vias = [(float(a), float(b)) for a, b in
            re.findall(r"\(via\s*\n?\s*\(at ([\d.-]+) ([\d.-]+)\)", txt)]
    for num, ref, cx, cy, w, h, want in [
            (5, "U7", 76.29, 64.84, 3.40, 5.00, 6),
            (5, "U9", 96.50, 78.56, 5.20, 11.00, 8),
            (5, "U2", 96.28, 49.84, 1.65, 2.85, 3)]:
        n = sum(1 for x, y in vias
                if abs(x - cx) <= w / 2 and abs(y - cy) <= h / 2)
        check(num, f"{ref} exposed pad has >= {want} thermal vias",
              n >= want, f"{n} vias inside the EP")

    return


def main():
    sch_only = "--sch-only" in sys.argv
    if not os.path.exists(KICAD_CLI):
        sys.exit(f"kicad-cli not found at {KICAD_CLI}")
    sch_checks()
    if not sch_only:
        pcb_checks()

    fails = 0
    print(f"{'':4} {'#':>3}  {'check':<62} detail")
    for num, name, ok, detail in results:
        if ok:
            tag = "PASS"
        elif num in OPEN:
            tag = "TODO"
        else:
            tag = "FAIL"
            fails += 1
        print(f"{tag:4} {num:>3}  {name:<62} {detail}")
    print(f"\n{sum(1 for r in results if r[2])}/{len(results)} passing, "
          f"{fails} regression(s)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
