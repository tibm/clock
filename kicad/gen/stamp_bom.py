"""Stamp MPN / Manufacturer / Package / Description / Notes onto every part.

Writes the fields from `parts_db.py` into BOTH `../clock.kicad_sch` (so the
schematic is the source of truth and eeschema's BOM export carries them) and
`../clock.kicad_pcb` (so the PCBWay plug-in, which reads *footprint* fields,
emits a complete BOM).  The edit is a surgical in-place splice — no
re-generation — so hand DRC fixes on the PCB survive.

Idempotent: re-running only rewrites values that changed.

    python3 stamp_bom.py            # patch both files
    python3 stamp_bom.py --dry-run  # report only

Run it again after any `build.py` / `pcb_build.py` regeneration.
"""
import argparse
import os
import re
import sys
import uuid as _uuid

import parts_db

HERE = os.path.dirname(os.path.abspath(__file__))
SCH = os.path.join(HERE, "..", "clock.kicad_sch")
PCB = os.path.join(HERE, "..", "clock.kicad_pcb")

FIELDS = ["Package", "MPN", "Manufacturer", "Description", "Notes"]
NS = _uuid.UUID("6f9619ff-8b86-d011-b42d-00c04fc964ff")


def block_end(src, start):
    """Index just past the s-expression that opens at `start`."""
    depth = 0
    p = start
    while True:
        c = src[p]
        if c == '"':
            p += 1
            while src[p] != '"' or src[p - 1] == "\\":
                p += 1
        elif c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                return p + 1
        p += 1


def blocks(src, marker):
    """Spans of every s-expression starting with `marker` (e.g. '\\n\\t(footprint ')."""
    out = []
    i = 0
    while True:
        j = src.find(marker, i)
        if j < 0:
            return out
        start = src.index("(", j)
        end = block_end(src, start)
        out.append((start, end))
        i = end


def prop_span(blk, name):
    m = re.search(r'\(property "%s"[\s"]' % re.escape(name), blk)
    if not m:
        return None
    return m.start(), block_end(blk, m.start())


def prop_value(blk, name):
    m = re.search(r'\(property "%s" "([^"]*)"' % re.escape(name), blk)
    return m.group(1) if m else None


def set_prop(blk, name, value, ref):
    """Return (new_blk, changed) with property `name` = `value`."""
    span = prop_span(blk, name)
    if span is not None:
        old = blk[span[0]:span[1]]
        cur = re.search(r'\(property "%s" "([^"]*)"' % re.escape(name), old)
        if cur.group(1) == value:
            return blk, False
        if cur.group(1) or not value:
            new = old[:cur.start(1)] + value + old[cur.end(1):]
            return blk[:span[0]] + new + blk[span[1]:], True
        # empty placeholder (KiCad's stock "Description"): drop it, then fall
        # through to the insert path so the field lands in FIELDS order
        line_start = blk.rfind("\n", 0, span[0]) + 1
        blk = blk[:line_start] + blk[span[1] + 1:]

    if not value:
        return blk, False

    # clone the (hidden) Datasheet property as the template
    tpl = prop_span(blk, "Datasheet")
    if tpl is None:
        raise RuntimeError(f"{ref}: no Datasheet property to clone")
    text = blk[tpl[0]:tpl[1]]
    text = re.sub(r'^\(property "Datasheet" "[^"]*"',
                  '(property "%s" "%s"' % (name, value), text, count=1)
    text = re.sub(r'\(uuid "[^"]*"\)',
                  '(uuid "%s")' % _uuid.uuid5(NS, f"{ref}:{name}"), text,
                  count=1)
    if "(hide yes)" not in text:
        text = text.replace("(effects", "(hide yes)\n\t\t\t(effects", 1)
    # indentation of the template line, so the clone lines up
    line_start = blk.rfind("\n", 0, tpl[0]) + 1
    indent = blk[line_start:tpl[0]]
    return blk[:tpl[1]] + "\n" + indent + text + blk[tpl[1]:], True


def set_value_pos(blk, x, y, justify, rot=0):
    """Move a schematic symbol's Value text (position, rotation, justify)."""
    span = prop_span(blk, "Value")
    old = blk[span[0]:span[1]]
    new = re.sub(r'\(at [-\d.]+ [-\d.]+ \d+\)', '(at %s %s %s)' % (x, y, rot),
                 old, count=1)
    new = re.sub(r'\(justify \w+\)', '(justify %s)' % justify, new, count=1)
    if new == old:
        return blk, 0
    return blk[:span[0]] + new + blk[span[1]:], 1


def patch(path, marker, label, dry):
    src = open(path).read()
    out = []
    prev = 0
    hits = misses = changes = 0
    for start, end in blocks(src, marker):
        blk = src[start:end]
        ref = prop_value(blk, "Reference")
        if not ref or ref.startswith(parts_db.SKIP_PREFIXES):
            continue
        value = prop_value(blk, "Value") or ""
        want = parts_db.VALUES.get(ref)
        if want and want != value:
            blk, _ = set_prop(blk, "Value", want, ref)
            print(f"  -- {label}: {ref} value {value!r} -> {want!r}")
            value = want
            changes += 1
        pos = parts_db.VALUE_POS.get(ref)
        if pos and label == "sch":
            blk, ch = set_value_pos(blk, *pos)
            changes += ch
        fp = prop_value(blk, "Footprint")
        if fp is None:                      # pcb: name is in the header
            m = re.match(r'\(footprint "([^"]*)"', blk)
            fp = m.group(1) if m else ""
        part = parts_db.part_for(ref, value, fp)
        if part is None:
            misses += 1
            print(f"  !! {label}: no part data for {ref} ({value}, {fp})")
            continue
        hits += 1
        # reversed: each insert lands right after Datasheet, so the file ends
        # up in FIELDS order
        for name in reversed(FIELDS):
            blk, ch = set_prop(blk, name, part.get(name, ""), ref)
            changes += ch
        out.append((start, end, blk))

    if dry:
        print(f"{label}: {hits} parts matched, {misses} unmatched, "
              f"{changes} field writes (dry run)")
        return misses

    buf = []
    for start, end, blk in out:
        buf.append(src[prev:start])
        buf.append(blk)
        prev = end
    buf.append(src[prev:])
    open(path, "w").write("".join(buf))
    print(f"{label}: {hits} parts matched, {misses} unmatched, "
          f"{changes} fields written -> {os.path.relpath(path, HERE)}")
    return misses


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dry-run", action="store_true")
    a = ap.parse_args()
    misses = 0
    misses += patch(PCB, "\n\t(footprint ", "pcb", a.dry_run)
    misses += patch(SCH, "\n\t(symbol\n", "sch", a.dry_run)
    return 1 if misses else 0


if __name__ == "__main__":
    sys.exit(main())
