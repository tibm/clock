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

PCB = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                   "sensor.kicad_pcb")


def main():
    board = pcbnew.LoadBoard(PCB)
    zones = board.Zones()
    pcbnew.ZONE_FILLER(board).Fill(zones)
    board.Save(PCB)
    for z in zones:
        if z.GetIsRuleArea():
            continue
        print(f"  {z.GetZoneName()} {pcbnew.ToMM(pcbnew.ToMM(z.GetFilledArea())):.0f} mm2")
    return 0


if __name__ == "__main__":
    sys.exit(main())
