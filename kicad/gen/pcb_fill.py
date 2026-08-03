"""Fill the copper pours of a .kicad_pcb (default: ../clock.kicad_pcb).

The ZONE_FILLER crash PCB_NOTES.md warned about is specific to filling a board
the *same process* has just built in memory; on a board LOADED FROM DISK it is
fine, which is why the sensor board already fills through its own
`pcb_fill.py` child process.  Verified on 10.0.4 (2026-08-02): re-filling the
routed main board from here reproduces the fill eeschema/pcbnew's GUI left in
the file **byte for byte** (0-line diff), so this is a safe substitute for
Edit > Fill All Zones after a script has moved copper.

Saves through `pcb_io.save_board()` -- filling must not rewrite the project's
design rules, or the next `kicad-cli pcb drc` will check against defaults.

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\
Versions/3.9/bin/python3.9 pcb_fill.py [../clock.kicad_pcb]
"""
import os
import sys

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcb_io import save_board  # noqa: E402

DEFAULT = os.path.join(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))), "clock.kicad_pcb")


def main(argv):
    pcb = os.path.abspath(argv[1]) if len(argv) > 1 else DEFAULT
    board = pcbnew.LoadBoard(pcb)
    zones = board.Zones()
    if not pcbnew.ZONE_FILLER(board).Fill(zones):
        print("ZONE_FILLER reported failure")
        return 1
    save_board(board, pcb)
    for z in zones:
        if z.GetIsRuleArea():
            continue
        layers = ",".join(board.GetLayerName(l) for l in z.GetLayerSet().Seq())
        print(f"  {z.GetZoneName() or z.GetNetname():8s} {layers:6s} "
              f"{pcbnew.ToMM(pcbnew.ToMM(z.GetFilledArea())):8.1f} mm2")
    print(f"filled {os.path.basename(pcb)}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
