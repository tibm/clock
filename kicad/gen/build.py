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
    subs = []
    # pass 1: build every sheet (declares components + logical nodes)
    for i, (title, fname, mod) in enumerate(SHEETS):
        col, r = i % 3, i // 3
        su = root.add_sheet(title, fname, 30 + col * 90, 30 + r * 45, w=70, h=30)
        sub = Schematic(cache, fname[:-11], ROOT_UUID, su, PROJECT, str(i + 2))
        mod.build(sub)
        subs.append((sub, fname))
        pages.append([su, title])

    # determine cross-sheet nets: a net name appearing on >1 sheet (and not a
    # power rail) is routed with global labels; everything else is on-page wires.
    from collections import defaultdict
    net_sheets = defaultdict(set)
    for sub, _ in subs:
        for _c, _p, net in sub.nodes:
            net_sheets[net].add(sub.name)
    global_nets = {n for n, s in net_sheets.items()
                   if len(s) > 1 and n not in Schematic.POWER_NETS}

    # auto power-flags: one PWR_FLAG per net that has a power-input pin but no
    # power-output driver (rails + resistor-fed local supplies). Added once,
    # globally, so no double-driver conflicts.
    net_etypes = defaultdict(list)
    net_home = {}
    present_power = set()
    for sub, _ in subs:
        for c, p, net in sub.nodes:
            net_etypes[net].append(c.pin_etype(p))
            net_home.setdefault(net, (sub, c, p))
            if net in Schematic.POWER_NETS:
                present_power.add(net)
    # every used power rail is rendered as power-input symbols -> needs one flag,
    # placed on a sheet where the rail is actually used (so it isn't isolated)
    for net in sorted(present_power):
        host = net_home[net][0]
        host.rail_flag(net)
    # on-page supply nets fed only through passives (power_in pin, no power_out)
    for net, ets in net_etypes.items():
        if net in Schematic.POWER_NETS:
            continue
        if "power_in" in ets and not any(e in ("power_out", "power_output") for e in ets):
            sub, c, p = net_home[net]
            sub.local_flag(net, c, p)

    # pass 2: render + write
    for sub, fname in subs:
        sub.finalize(global_nets)
        project.write_sheet(sub, fname)

    project.write_sym_lib_table()
    project.write_project(PROJECT, pages)
    project.write_sheet(root, "clock.kicad_sch")
    print(f"wrote clock.kicad_sch + {len(SHEETS)} sub-sheets")


if __name__ == "__main__":
    main()
