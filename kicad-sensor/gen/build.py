"""Assemble the single-page sensor-board schematic (A3).
Run:  python3 mksym.py && python3 build.py     (writes into ../ = kicad-sensor/)

Same generator as the main board — `sch2.Sch` from ../../kicad/gen — so the
lint pass, eeschema-rule junctions and the `kicad-cli sch upgrade`
normalisation (open + save in eeschema = zero diff) all behave identically.
"""
from __future__ import annotations
import importlib
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
MAIN_GEN = os.path.abspath(os.path.join(HERE, "..", "..", "kicad", "gen"))
sys.path.insert(0, MAIN_GEN)
sys.path.insert(0, HERE)

from schlib import SymbolCache         # noqa: E402
from sch2 import Sch                   # noqa: E402
import project                         # noqa: E402


def _main_board_build():
    """Load ../../kicad/gen/build.py by path — same basename as this file, so
    a plain `import build` would find this one instead."""
    spec = importlib.util.spec_from_file_location(
        "clock_main_build", os.path.join(MAIN_GEN, "build.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


normalize = _main_board_build().normalize   # kicad-cli sch upgrade step

SENSOR_SYM = os.path.join(HERE, "sensor_custom.kicad_sym")

BLOCKS = ["b_host", "b_imu", "b_env", "b_als"]


def notes(s):
    s.frame(150, 188, 410, 285, "NOTES")
    L = [
        "BOARD  Small daughterboard on the cube's front/underside, tied to main-board J7 by a 6-way JST ZH",
        "       harness (B6B-ZR both ends, 1:1).  Rails: +3V3 and GND arrive on the cable; no regulator here.",
        "",
        "ASSEMBLY  All three sensors are LEADLESS (LGA-28 / LGA-8 / DFN-6) — this board CANNOT be hand-soldered",
        "       with an iron and is the 'path 2b' SMT-assembled daughterboard of datasheet/README.md.  Order it",
        "       fab-assembled (or stencil + hotplate).  The main board's hand-solder-only rule does not apply here.",
        "",
        "I2C    One bus, three addresses: BNO085 0x4A (R3; R4 -> 0x4B) - BME688 0x77 (R10; R11 -> 0x76) -",
        "       TSL2591 0x29 (fixed).  No clash with the main board's MCP23017 (0x20) or the amp (0x62/0x63).",
        "       Pull-ups: 4.7k on the main board + R1/R2 10k here ~= 3.2k effective.",
        "",
        "IRQ    J7.5 SENSOR_INT = BNO085 H_INTN only (push-pull, active low; main-board R97 10k holds it high",
        "       when the harness is unplugged).  J7.6 ALS_INT = TSL2591 INT, open drain, pulled up by R12 here —",
        "       the main board leaves that pin unconnected today, so it is a wire-in-place-for-later.",
        "",
        "BEFORE FAB  Check the stock LGA-28 land (generic IPC pattern) against BNO08X datasheet Fig. 7-2:",
        "       its pads sit ~0.10-0.13 mm further out than CEVA's drawing.  Numbering/arrangement do match.",
    ]
    for i, t in enumerate(L):
        s.text(t, 154, 196 + i * 4.6, size=1.3)


def main():
    cache = SymbolCache(extra_libs={"sensor": SENSOR_SYM})
    s = Sch(cache, "sensor", "sensor", paper="A3",
            title="Wooden Smart Clock — sensor board",
            date="2026-07-29", rev="A", company="")
    for name in BLOCKS:
        importlib.import_module(name).build(s)
    notes(s)

    merged = s.merge_collinear()
    added, dropped = s.auto_junctions()
    issues = s.lint()
    project.write_sym_lib_table()
    project.write_fp_lib_table()
    project.write_project("sensor")
    sheet = project.write_sheet(s, "sensor.kicad_sch")
    print(f"wrote sensor.kicad_sch: {len(s.comps)} symbols, {len(s.wires)} wires "
          f"({merged} merged), {len(s.juncs)} junctions ({len(added)} auto, "
          f"{len(dropped)} dropped), {len(s.labels)} labels")
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
