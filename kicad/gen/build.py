"""Assemble the single-page kicad2 schematic.
Run:  python3 mksym.py && python3 build.py   (writes into ../ = kicad2/)

After writing, the sheet is normalized through `kicad-cli sch upgrade` so the
on-disk file is in the CURRENT KiCad format (same serializer the GUI uses):
opening + saving in eeschema then produces zero diff (no "older version"
banner, no reordering, no junction/wire cleanup, no fresh pin uuids).
"""
import importlib
import os
import subprocess
import sys

from schlib import SymbolCache
from sch2 import Sch
import project2

KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"


def normalize(path):
    """Re-serialize with KiCad itself (format version bump + canonical order)."""
    if not os.path.exists(KICAD_CLI):
        print(f"WARNING: {KICAD_CLI} not found — {path} left in generator "
              f"format; KiCad will show an 'older version' notice and re-save "
              f"with deltas.")
        return False
    r = subprocess.run([KICAD_CLI, "sch", "upgrade", "--force", path],
                       capture_output=True, text=True)
    if r.returncode != 0:
        print(f"kicad-cli upgrade FAILED:\n{r.stdout}\n{r.stderr}")
        return False
    with open(path) as f:
        head = f.read(200)
    ver = head.split("(version", 1)[1].split(")")[0].strip() if "(version" in head else "?"
    print(f"normalized via kicad-cli: format version {ver}")
    return True

BLOCKS = [
    "b_powerin",
    "b_charger",
    "b_rails",
    "b_led",
    "b_mcu",
    "b_io",
    "b_sd",
    "b_audio",
    "b_motor",
]


def main():
    cache = SymbolCache()
    try:
        from cosmetics import COSMETICS
    except ImportError:
        COSMETICS = {}
    s = Sch(cache, "clock", "clock", paper="A1",
            title="Wooden Smart Clock — main board",
            date="2026-07-14", rev="A", company="",
            cosmetics=COSMETICS)
    for name in BLOCKS:
        try:
            mod = importlib.import_module(name)
        except ModuleNotFoundError:
            print(f"  (skip {name}: not written yet)")
            continue
        mod.build(s)

    # reading guide (bottom-left, under the battery block)
    s.frame(15, 318, 170, 357, "HOW TO READ THIS SHEET")
    s.text("One page, laid out like block_diagram.drawio: power flows left,", 20, 329, size=1.3)
    s.text("MCU centre, peripherals right.  Power paths and MCU buses (SPI,", 20, 334, size=1.3)
    s.text("I2S, MCPWM, encoder) are drawn as wires; GND / +3V3 / +5V taps", 20, 339, size=1.3)
    s.text("and named labels connect by name (same name = same net).", 20, 344, size=1.3)
    s.text("Off-board parts (display, SD, sensors, speaker, motor, LED", 20, 348.5, size=1.3)
    s.text("strips, encoder+buttons, cell) enter via connectors J1..J10 / BT1.", 20, 353, size=1.3)

    merged = s.merge_collinear()
    added, dropped = s.auto_junctions()
    issues = s.lint()
    project2.write_sym_lib_table()
    project2.write_fp_lib_table()
    project2.write_project("clock")
    sheet = project2.write_sheet(s, "clock.kicad_sch")
    print(f"wrote clock.kicad_sch: {len(s.comps)} symbols, {len(s.wires)} wires "
          f"({merged} merged), {len(s.juncs)} junctions ({len(added)} auto, "
          f"{len(dropped)} dropped), {len(s.labels)} labels")
    if dropped:
        for pt in dropped:
            print(f"  dropped redundant junction at {pt}")
    if issues:
        print(f"\nLINT ({len(issues)}):")
        for i in issues:
            print("  -", i)
        return 1
    print("lint: clean")
    if not normalize(sheet):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
