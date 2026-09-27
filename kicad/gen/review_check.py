#!/usr/bin/env python3
"""Standing assertions for the defects found in the 2026-08-02 design review.

Runs on plain python3 (no pcbnew needed); uses kicad-cli to export the netlist.

    python3 review_check.py            # schematic checks + PCB checks
    python3 review_check.py --sch-only # skip the PCB (e.g. mid-rework)

Each check maps to a numbered finding in ../REVIEW.md.  A check that has not
been fixed yet reports TODO (expected, non-fatal); a check that WAS fixed and
has regressed reports FAIL and sets a non-zero exit code.

The `V<n>` checks are the v0.4 list, and they exist because ERC and DRC passed
every one of the three bring-up defects (V13, V14, V15) without a murmur.  A
prose finding is not a guarantee; this file is.  Two of them are the shape of
bug that documentation cannot catch:

  * V13  a legal net on the wrong pin      -> assert the pin's rail
  * V14  a correct net at the wrong WIDTH  -> assert computed resistance
  * V15  a sense tap on the wrong NODE     -> assert the topology
  * V16  the margin arithmetic itself      -> assert I x R < threshold

⚠ V16 is the one to read first.  It is the check that says whether the battery
protector can survive the 5 V boost's startup surge, and it is the reason V14
and V15 alone are not sufficient -- see ../REVIEW.md.
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
PRO = os.path.join(KICAD_DIR, "clock.kicad_pro")
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"

# Findings that are known-not-yet-fixed: reported as TODO instead of FAIL.
# V13-V16 are rev0.3 defects fixed by rework on build #1 but NOT yet in gen/ --
# the generator keeps matching the board as built until the respin.
OPEN = {4, 5, 8, 9, 24, "V13", "V14", "V14b", "V15", "V15b", "V16", "V16b", "V17"}

# ---- physical constants for the resistance / margin checks -----------------
# 1 oz copper, 35 um, at 20 C: rho / t = 1.72e-8 / 35e-6 = 0.491 mOhm/square.
MOHM_PER_SQ = 0.491

# The two nets that carry the FULL cell current.  On rev0.3 they are auto-named
# and were therefore invisible to the POWER netclass pattern list (V14); v0.4
# renames them, so accept either spelling.
CELL_PATH_NETS = {
    "cell-": ("Net-(BT1-Pin_2)", "CELL-"),
    "pack-": ("Net-(U4-S2)", "PACK-"),
}
# v0.4 target per net.  rev0.3 as built: 43 and 69 mOhm.
CELL_PATH_BUDGET_MOHM = 10.0

# V16 inputs.  Sources, so the arithmetic can be re-checked rather than trusted:
I_BOOST_STARTUP_A = 3.7      # TPS61023 valley current limit, typ (boost_5v_tps61023.pdf)
I_ALARM_PEAK_A = 2.3         # sunrise alarm peak from the cell (FIRMWARE.md R-AUDIO-1)
R_FET_PAIR_MOHM = 33.0       # AO4838 pair, derated to VGS = V_cell (REVIEW.md V9)
R_TCO_MOHM = 30.0            # F1 + joints; NOT specified in tco_sdf_df077s.pdf
V_DIP_MIN_MV = {             # HY2111 discharge-OC floor = nominal - tolerance
    "-GB": 125.0,            # 150 +/- 25
    "-HB": 175.0,            # 200 +/- 25
    "-KB": 195.0,            # 225 +/- 30
}
U3_SUFFIX_FITTED = "-GB"     # build #1 as assembled; read the marking, log it

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

    # -- V13: AVDD (U9.1) is a 4.5-26.4 V pin, same range as PVDD -- NOT +3V3.
    #    The defect that made board #1 silent.  ERC cannot see it: +3V3 is a
    #    perfectly legal net to put on a pin.  So assert the RAIL, by identity
    #    with a pin known to be on it.
    avdd, pvdd, dvdd = (net_of(nets, "U9.1"), net_of(nets, "U9.28"),
                        net_of(nets, "U9.10"))
    check("V13", "U9.1 AVDD on the PVDD rail (4.5 V min), not +3V3",
          avdd is not None and avdd == pvdd and avdd != dvdd,
          f"AVDD={avdd} PVDD(28)={pvdd} DVDD(10)={dvdd}")

    # -- V15: the protector senses VSS(cell-) against CS.  R21's cold end must
    #    land on P- = U4.1 (the FET pair's far source), NOT on GND: F1, the
    #    TCO, sits between U4.1 and the pour, and its resistance is not a
    #    specified parameter in its datasheet -- an unbounded term in a loop
    #    whose threshold is 125 mV.  hy2111 datasheet section 10 puts R2 on P-.
    cs_far = net_of(nets, "R21.2")
    pack_minus = net_of(nets, "U4.1")
    check("V15", "R21 cold end on P- (U4.1), not GND -- TCO out of the sense loop",
          cs_far is not None and cs_far == pack_minus and cs_far != "GND",
          f"R21.2={cs_far} U4.1={pack_minus}"
          + ("  <- R21 absent (bodged on build #1)" if cs_far is None else ""))

    # -- V15b: R21 forms a divider against the protector's INTERNAL CS pull-up
    #    to VDD, measured at 250 kOhm on build #1 2026-09-27.  The standing
    #    offset it puts on CS eats the V_DIP budget before any current flows:
    #        2 kOhm -> 27 mV of 150 mV     10 kOhm -> 131 mV, i.e. at the trip
    #    The datasheet's 2 kOhm max is this, and smaller is strictly better.
    r21 = to_ohms(vals.get("R21", "") or "0")
    if vals.get("R21"):
        offset_mv = 4.2e3 * r21 / (250e3 + r21)
        check("V15b", "R21 <= 2k so the CS pull-up offset stays small",
              r21 <= 2e3, f"R21={vals['R21']} -> {offset_mv:.0f} mV standing offset")


def to_ohms(v):
    m = re.match(r"^([\d.]+)\s*([kKmM]?)R?$", v.strip())
    if not m:
        return None
    return float(m.group(1)) * {"": 1, "k": 1e3, "K": 1e3, "m": 1e6, "M": 1e6}[m.group(2)]


# --------------------------------------------------------------------------
# PCB
# --------------------------------------------------------------------------
def segments_by_net(txt):
    """{net: [(length_mm, width_mm, layer), ...]} for every routed track."""
    out = {}
    pat = re.compile(r'\(segment\s+\(start ([-\d.]+) ([-\d.]+)\)\s+'
                     r'\(end ([-\d.]+) ([-\d.]+)\)\s+\(width ([\d.]+)\)\s+'
                     r'\(layer "([^"]+)"\)\s+\(net "([^"]*)"\)', re.S)
    for x1, y1, x2, y2, w, layer, net in pat.findall(txt):
        length = ((float(x2) - float(x1)) ** 2 + (float(y2) - float(y1)) ** 2) ** 0.5
        out.setdefault(net, []).append((length, float(w), layer))
    return out


def net_series_mohm(segs):
    """Worst-case series resistance: every segment treated as in-line.

    Conservative on purpose -- a branchy net reads higher than it really is,
    and over-estimating is the safe direction for a safety gate.
    """
    return sum(MOHM_PER_SQ * (length / w) for length, w, _ in segs)


def resolve_cell_net(segs_by_net, spellings):
    for name in spellings:
        if name in segs_by_net:
            return name
    return None


def net_of_pad(txt, ref, pad):
    """Net on one footprint pad, straight out of the .kicad_pcb.

    The netlist route (net_of) is the one to prefer for topology, but a pad
    that has been REMOVED from the schematic still has to be answerable here,
    and a bodge is only ever visible on the board.
    """
    for chunk in txt.split("\n\t(footprint ")[1:]:
        m = re.search(r'\(property "Reference" "([^"]+)"', chunk)
        if not m or m.group(1) != ref:
            continue
        for pm in re.finditer(r'\(pad "([^"]*)".*?(?=\n\t\t\(pad |\n\t\t\(model|\Z)',
                              chunk, re.S):
            if pm.group(1) != pad:
                continue
            nm = re.search(r'\(net "([^"]*)"\)', pm.group(0))
            return nm.group(1) if nm else None
    return None


def pcb_checks():
    txt = open(PCB).read()
    segs = segments_by_net(txt)

    # -- #4: one 0.25 mm net class for everything, incl. VBAT/+12V/PVDD/+5V.
    # only TRACK widths -- (width ...) also appears on silk/courtyard graphics
    widths = sorted({w for net in segs for _, w, _ in segs[net]})
    check(4, "power nets routed wider than the 0.25 mm default",
          any(w >= 0.5 for w in widths), f"track widths present: {widths}")

    # -- V14: the two nets carrying the FULL cell current were left at the
    #    0.25 mm default, because the POWER netclass matches net NAMES and both
    #    of these are auto-named.  #4 above passed throughout -- it only asks
    #    whether SOME net got widened.  This asks about the right ones.
    #    rev0.3 as built: cell- 43 mOhm, pack- 69 mOhm (32.7 mm of it on In2).
    loop = {}
    for role, spellings in CELL_PATH_NETS.items():
        name = resolve_cell_net(segs, spellings)
        if name is None:
            check("V14", f"cell-current net {role} present and routed", False,
                  f"none of {spellings} has any track")
            continue
        r = net_series_mohm(segs[name])
        loop[role] = r
        thin = sum(length for length, w, _ in segs[name] if w <= 0.25)
        check("V14", f"{role} ({name}) <= {CELL_PATH_BUDGET_MOHM:.0f} mOhm",
              r <= CELL_PATH_BUDGET_MOHM,
              f"{r:.0f} mOhm worst-case series, {thin:.1f} mm of it at <=0.25 mm")

    # -- V14b: and the naming fix, so the widening pass can never miss them
    #    again.  A string match is what failed; assert the string.
    try:
        pro = open(PRO).read()
        patterns = set(re.findall(r'"netclass":\s*"POWER",\s*"pattern":\s*"([^"]+)"',
                                  pro.replace("\n", " ")))
    except OSError:
        patterns = set()
    missing = [role for role, spellings in CELL_PATH_NETS.items()
               if not (set(spellings) & patterns)]
    check("V14b", "cell-current nets are in the POWER netclass patterns",
          not missing, f"missing: {missing}" if missing else f"{sorted(patterns)}")

    # -- V16: the margin arithmetic, which is the check none of the others
    #    replace.  The protector measures V(VSS) - V(CS), so the sense loop is
    #    the FET pair PLUS whatever copper and parts sit between those two
    #    nodes.  rev0.3 senses FETs + both traces + the TCO = 113-145 mOhm,
    #    trips at 0.86-1.55 A, and meets a 3.7 A boost startup.
    #
    #    ⚠ Read the two rows below together.  Fixing V14 and V15 is NOT
    #    sufficient: at a 10 mOhm/net budget the loop is still 43 mOhm and
    #    3.7 A lands at 159 mV, over a -GB's 125 mV floor.  Either the surge
    #    goes away (V8 gates the boost -> worst case becomes the 2.3 A alarm)
    #    or the threshold moves (-HB/-KB).  This check asserts BOTH framings so
    #    a respin cannot quietly satisfy neither.
    #
    #    ⚠ This number is deliberately HIGHER than REVIEW.md's 113-145 mOhm.
    #    net_series_mohm() counts each net whole, but only the part of `cell-`
    #    downstream of U3's VSS tap is really in the loop.  A gate should
    #    over-estimate, so it does; the doc figure is the measured-path one.
    r_cell = loop.get("cell-", float("nan"))
    r_pack = loop.get("pack-", float("nan"))
    cs_on_pack = net_of_pad(txt, "R21", "2") == net_of_pad(txt, "U4", "1")
    r_loop = r_cell + R_FET_PAIR_MOHM + (0.0 if cs_on_pack else r_pack + R_TCO_MOHM)
    floor = V_DIP_MIN_MV[U3_SUFFIX_FITTED]
    for label, amps in (("boost startup", I_BOOST_STARTUP_A),
                        ("alarm peak", I_ALARM_PEAK_A)):
        mv = amps * r_loop          # mOhm x A = mV
        check("V16", f"{label} {amps} A x loop < V_DIP floor ({U3_SUFFIX_FITTED})",
              mv < floor,
              f"loop {r_loop:.0f} mOhm -> {mv:.0f} mV vs {floor:.0f} mV"
              f"  (trips at {floor / r_loop:.2f} A)")

    # -- V16b: and the charge direction, which is the one that decides whether
    #    the cell can be charged at all.  V_CIP is -100 mV typ, -60 mV WORST,
    #    and suffix-independent -- so no protector choice rescues this one.
    charge_mv = 1.0 * r_loop        # I_CHG = 100 mV / R18 = 1.0 A
    check("V16b", "1 A charge current < V_CIP worst case (60 mV)",
          charge_mv < 60.0,
          f"loop {r_loop:.0f} mOhm -> {charge_mv:.0f} mV vs 60 mV worst case")

    # -- V17: the board must have a DELIBERATE cell-insertion release.
    #    hy2111 section 11.1: "Discharging may not be enacted when the battery
    #    is first time connected.  To regain normal status, CS pin and VSS pin
    #    must be shorted or the charger must be connected."  Measured on
    #    build #1 2026-09-27: the charger route does NOT work here, because the
    #    LT3652 floats at 4.05 V and charge current has to cross the discharge
    #    FET's body diode -- so it only conducts below V_cell ~= 3.35 V.
    #    Two ways to satisfy this, and a respin must pick one:
    #      (a) a momentary CS<->VSS short brought out to a switch, or
    #      (b) an auto-recovery protector suffix (-KB).
    release_sw = any(net_of_pad(txt, ref, "1") is not None
                     for ref in ("SW3", "SW4"))
    check("V17", "deliberate cell-insertion release exists (switch or -KB)",
          release_sw or U3_SUFFIX_FITTED == "-KB",
          f"release switch={release_sw} U3{U3_SUFFIX_FITTED}"
          "  -- build #1 needs tweezers on U4.1/U4.3 at every insertion")

    # -- #10: J7 (+3V3 on pin 2) and J10 (+5V on pin 2) take the same 6-way ZH
    #         cable and sit 13.5 mm apart; swapping them kills the sensor board.
    #         Resolved by silkscreen rather than by keying, so check the labels.
    silk = re.findall(r'\(gr_text "(SENSOR|KNOB)"', txt)
    check(10, "J7/J10 disambiguated by SENSOR/KNOB silkscreen",
          {"SENSOR", "KNOB"} <= set(silk), f"B.SilkS labels found: {sorted(set(silk))}")

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
