"""Assemble the whole KiCad project: root sheet + all sub-sheets.
Run:  python3 build.py   (writes into ../ , the kicad/ project dir)
"""
from schlib import SymbolCache
from sch import Schematic, uid
import project

import s_power_in
import s_charger
import s_rails
import s_mcu
import s_audio
import s_motor
import s_display_sd
import s_io

PROJECT = "clock"
ROOT_UUID = uid("file:clock")

# (Sheet title, filename, module)
SHEETS = [
    ("Power In (USB-C / PD)", "power_in.kicad_sch", s_power_in),
    ("Charger & Battery Safety", "charger.kicad_sch", s_charger),
    ("Rails (5V / 3V3 / 12V)", "rails.kicad_sch", s_rails),
    ("MCU (ESP32-S3)", "mcu.kicad_sch", s_mcu),
    ("Audio (TAS5760M + PVDD mux)", "audio.kicad_sch", s_audio),
    ("Motor (2x TB6612 + stepper)", "motor.kicad_sch", s_motor),
    ("Display & microSD", "display_sd.kicad_sch", s_display_sd),
    ("IO Expander / Sensors / LED", "io.kicad_sch", s_io),
]


def main():
    cache = SymbolCache()
    Schematic._pwr_count = 0
    Schematic._flg_count = 0
    root = Schematic(cache, "clock", ROOT_UUID, None, PROJECT, "1")

    pages = []
    # lay out sheet symbols in the root in a 3-wide grid
    for i, (title, fname, mod) in enumerate(SHEETS):
        col, r = i % 3, i // 3
        x = 30 + col * 90
        y = 30 + r * 45
        su = root.add_sheet(title, fname, x, y, w=70, h=30)
        sub = Schematic(cache, fname[:-11], ROOT_UUID, su, PROJECT, str(i + 2))
        mod.build(sub)
        project.write_sheet(sub, fname)
        pages.append([su, title])

    project.write_sym_lib_table()
    project.write_project(PROJECT, pages)
    project.write_sheet(root, "clock.kicad_sch")
    print(f"wrote clock.kicad_sch + {len(SHEETS)} sub-sheets")


if __name__ == "__main__":
    main()
