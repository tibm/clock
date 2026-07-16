"""Assemble the single-page kicad2 schematic.
Run:  python3 mksym.py && python3 build.py   (writes into ../ = kicad2/)
"""
import importlib
import sys

from schlib import SymbolCache
from sch2 import Sch
import project2

BLOCKS = [
    "b_powerin",
    "b_charger",
    "b_rails",
    "b_led",
    "b_mcu",
    "b_io",
    "b_display",
    "b_audio",
    "b_motor",
]


def main():
    cache = SymbolCache()
    s = Sch(cache, "clock", "clock", paper="A1",
            title="Wooden Smart Clock — main board",
            date="2026-07-14", rev="A", company="")
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
    s.text("strips, cell) enter through connectors J1..J9 / BT1.", 20, 353, size=1.3)

    added = s.auto_junctions()
    issues = s.lint()
    project2.write_sym_lib_table()
    project2.write_project("clock")
    project2.write_sheet(s, "clock.kicad_sch")
    print(f"wrote clock.kicad_sch: {len(s.comps)} symbols, {len(s.wires)} wires, "
          f"{len(s.juncs)} junctions ({len(added)} auto), {len(s.labels)} labels")
    if issues:
        print(f"\nLINT ({len(issues)}):")
        for i in issues:
            print("  -", i)
        return 1
    print("lint: clean")
    return 0


if __name__ == "__main__":
    sys.exit(main())
