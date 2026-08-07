"""Fill the GND pours in sensor.kicad_pcb.

A separate process on purpose: pcbnew's ZONE_FILLER segfaults when it runs
against a board this script's parent built in memory (the same crash the main
board's PCB_NOTES.md reports), but it is perfectly happy filling a board
LOADED from disk.  pcb_build.py therefore saves first and calls this through
subprocess, so a crash here can never cost the placement.

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_fill.py
"""
import os
import sys

import pcbnew

SENSOR_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(SENSOR_DIR, "sensor.kicad_pcb")
PROJECT = os.path.join(SENSOR_DIR, "sensor.kicad_pro")


def main():
    # board.Save() rewrites the sibling .kicad_pro from pcbnew's own model,
    # dropping every eeschema-only key (ERC severities, bom presets, ngspice).
    # pcb_build.py merges those back afterwards; run standalone -- which is
    # now the normal case, since the board is hand-owned -- nothing did, and
    # the ERC config quietly reverted.  Filling zones changes no project
    # setting, so keep the file byte-for-byte (same idiom as pcb_canon.py).
    keep = open(PROJECT, "rb").read() if os.path.exists(PROJECT) else None

    board = pcbnew.LoadBoard(PCB)
    zones = board.Zones()
    pcbnew.ZONE_FILLER(board).Fill(zones)
    board.Save(PCB)

    if keep is not None and open(PROJECT, "rb").read() != keep:
        open(PROJECT, "wb").write(keep)
        print("  sensor.kicad_pro restored (board.Save rewrote it)")

    for z in zones:
        if z.GetIsRuleArea():
            continue
        print(f"  {z.GetZoneName()} {pcbnew.ToMM(pcbnew.ToMM(z.GetFilledArea())):.0f} mm2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
