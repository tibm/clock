"""Stamp MPN / Manufacturer / Package / Description / Notes — and the DNP /
not-a-part flags — onto every part of the sensor board.

Writes into BOTH `../sensor.kicad_sch` (so eeschema / `kicad-cli sch export
bom` carry the data) and `../sensor.kicad_pcb` (so the PCBWay plug-in, which
reads *footprint* fields, emits a complete BOM).  Surgical in-place splices —
no re-generation — so hand DRC fixes on the PCB survive.  Idempotent.

    python3 stamp_bom.py            # patch both files
    python3 stamp_bom.py --dry-run  # report only

Run it again after any `build.py` / `pcb_build.py` regeneration: both
generators rewrite their file from scratch and drop every field below.

The field splicer is the main board's (`../../kicad/gen/stamp_bom.py`, loaded
by path because the basenames collide), driven by this project's
`parts_db.py`.  Two flag passes are added on top, because this board is
ordered **fab-assembled** and the BOM is what the assembly house works from:

  * `parts_db.DNP` -> `(dnp yes)` on the symbol and the `dnp` footprint
    attribute.  R4/R11 are the *alternate* I2C-address straps: only the value
    string said "(DNP)", so an assembler reading the BOM would have fitted
    them next to R3/R10 and shorted +3V3 to GND through 0 ohm.
  * `parts_db.EXCLUDE_FROM_BOM` -> `(in_bom no)` + `(in_pos_files no)` on the
    symbol, matching the `exclude_from_bom exclude_from_pos_files` the test-pad
    footprints already carry, so the schematic-side and PCB-side BOMs agree
    (and "Update PCB from Schematic" cannot put TP1/TP2 back).

Both files are then re-serialized by KiCad itself — `kicad-cli sch upgrade`
(the same `normalize()` `build.py` runs) and `pcb_canon.py` — because a text
splice cannot reproduce the writer's field order and default text thickness,
and this project's files are expected to be byte-identical to what the GUI
would save.
"""
import argparse
import importlib.util
import os
import re
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_GEN = os.path.abspath(os.path.join(HERE, "..", "..", "kicad", "gen"))
sys.path.insert(0, MAIN_GEN)
sys.path.insert(0, HERE)          # this project's parts_db must win

import parts_db                   # noqa: E402

SENSOR_DIR = os.path.dirname(HERE)
SCH = os.path.join(SENSOR_DIR, "sensor.kicad_sch")
PCB = os.path.join(SENSOR_DIR, "sensor.kicad_pcb")

KICAD_PY = ("/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework"
            "/Versions/3.9/bin/python3.9")

SCH_MARKER = "\n\t(symbol\n"
PCB_MARKER = "\n\t(footprint "

# pcbnew's own order for the footprint attribute list — keep it, or the next
# GUI save re-orders the line and the file stops round-tripping byte-identical.
ATTR_ORDER = ["through_hole", "smd", "board_only", "exclude_from_pos_files",
              "exclude_from_bom", "dnp", "allow_missing_courtyard",
              "allow_soldermask_bridges"]


def _stamper():
    """The main board's field splicer, loaded by path (same basename as this
    file).  Its module-level `import parts_db` binds to ours because HERE is
    first on sys.path — asserted below, since silently stamping the main
    board's part data onto this board would be plausible and wrong."""
    spec = importlib.util.spec_from_file_location(
        "clock_stamp_bom", os.path.join(MAIN_GEN, "stamp_bom.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if mod.parts_db is not parts_db:
        raise RuntimeError("the shared stamper bound the wrong parts_db")
    mod.HERE, mod.SCH, mod.PCB = HERE, SCH, PCB
    return mod


sb = _stamper()


def _write(path, edits, dry):
    if dry or not edits:
        return
    buf, prev = [], 0
    src = open(path).read()
    for start, end, blk in edits:
        buf.append(src[prev:start])
        buf.append(blk)
        prev = end
    buf.append(src[prev:])
    open(path, "w").write("".join(buf))


def _report(label, changes, unseen, dry):
    for ref in sorted(unseen):
        print(f"  !! {label}: {ref} is in parts_db but not in the file")
    print(f"{label} flags: {changes} written{' (dry run)' if dry else ''}, "
          f"{len(unseen)} refs not found")
    return len(unseen)


def _sch_flag(blk, token, want, ref):
    """Set one bare `(token yes|no)` flag — they all precede the properties."""
    head = blk.index('(property "Reference"')
    m = re.search(r"\(%s (yes|no)\)" % token, blk[:head])
    if not m:
        raise RuntimeError(f"{ref}: symbol has no ({token} ...) flag")
    if m.group(1) == want:
        return blk, False
    return blk[:m.start()] + f"({token} {want})" + blk[m.end():], True


def flag_sch(dry):
    """`(dnp yes)` on the address straps, `(in_bom no)` on the test pads."""
    src = open(SCH).read()
    unseen = set(parts_db.DNP) | set(parts_db.EXCLUDE_FROM_BOM)
    edits, changes = [], 0
    for start, end in sb.blocks(src, SCH_MARKER):
        blk = src[start:end]
        ref = sb.prop_value(blk, "Reference")
        want = []
        if ref in parts_db.DNP:
            want.append(("dnp", "yes"))
        if ref in parts_db.EXCLUDE_FROM_BOM:
            want += [("in_bom", "no"), ("in_pos_files", "no")]
        if not want:
            continue
        unseen.discard(ref)
        for token, value in want:
            blk, ch = _sch_flag(blk, token, value, ref)
            if ch:
                changes += 1
                print(f"  -- sch: {ref} ({token} {value})")
        edits.append((start, end, blk))
    _write(SCH, edits, dry)
    return _report("sch", changes, unseen, dry)


def _pcb_attrs(blk, add, ref):
    """Merge `add` into the footprint's `(attr ...)` list, in pcbnew's order."""
    m = re.search(r"\(attr ([^)]*)\)", blk)
    if not m:
        raise RuntimeError(f"{ref}: footprint has no (attr ...) line")
    have = m.group(1).split()
    if add <= set(have):
        return blk, False
    unknown = (set(have) | add) - set(ATTR_ORDER)
    if unknown:
        raise RuntimeError(f"{ref}: unknown footprint attribute(s) {unknown}")
    toks = sorted(set(have) | add, key=ATTR_ORDER.index)
    return blk[:m.start()] + "(attr %s)" % " ".join(toks) + blk[m.end():], True


def flag_pcb(dry):
    """`dnp` attribute on the address straps.  The test pads already carry
    `exclude_from_bom exclude_from_pos_files` from their library footprint —
    re-asserted here so the two files can never drift apart."""
    src = open(PCB).read()
    unseen = set(parts_db.DNP) | set(parts_db.EXCLUDE_FROM_BOM)
    edits, changes = [], 0
    for start, end in sb.blocks(src, PCB_MARKER):
        blk = src[start:end]
        ref = sb.prop_value(blk, "Reference")
        add = set()
        if ref in parts_db.DNP:
            add.add("dnp")
        if ref in parts_db.EXCLUDE_FROM_BOM:
            add |= {"exclude_from_bom", "exclude_from_pos_files"}
        if not add:
            continue
        unseen.discard(ref)
        blk, ch = _pcb_attrs(blk, add, ref)
        if ch:
            changes += 1
            print(f"  -- pcb: {ref} attr += {' '.join(sorted(add))}")
        edits.append((start, end, blk))
    _write(PCB, edits, dry)
    return _report("pcb", changes, unseen, dry)


def canonicalize():
    """Hand both files back to KiCad's own serializers (see the module
    docstring).  Never fatal: the data is already in, this is formatting."""
    bad = 0
    spec = importlib.util.spec_from_file_location(
        "clock_main_build", os.path.join(MAIN_GEN, "build.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    if not mod.normalize(SCH):
        bad += 1

    if not os.path.exists(KICAD_PY):
        print(f"WARNING: {KICAD_PY} not found — sensor.kicad_pcb left in "
              f"splice format; run pcb_canon.py under KiCad's python before "
              f"opening it in pcbnew.")
        return bad + 1
    r = subprocess.run([KICAD_PY, os.path.join(HERE, "pcb_canon.py")],
                       capture_output=True, text=True)
    print(r.stdout.rstrip() or r.stderr.rstrip())
    return bad + (r.returncode != 0)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    bad = sb.patch(PCB, PCB_MARKER, "pcb", a.dry_run)
    bad += sb.patch(SCH, SCH_MARKER, "sch", a.dry_run)
    bad += flag_pcb(a.dry_run)
    bad += flag_sch(a.dry_run)
    if not a.dry_run:
        bad += canonicalize()
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
