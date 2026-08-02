"""Re-save sensor.kicad_pcb through pcbnew's own writer.

`stamp_bom.py` splices the BOM fields in as text, and text cannot know two
things KiCad's writer does: mandatory fields (Reference / Value / Datasheet /
Description) are always emitted in their fixed slots, and user fields pick up
the board's default text thickness.  A spliced file therefore *loads* fine but
is not what the GUI would write, so the first "save" in pcbnew would show a
thousand-line diff that has nothing to do with the design.  One load + save
through pcbnew makes it canonical again.

A separate process because pcbnew only exists in KiCad's own interpreter, the
same reason `pcb_fill.py` is separate:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/3.9/bin/python3.9 pcb_canon.py
"""
import os
import sys

import pcbnew

SENSOR_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PCB = os.path.join(SENSOR_DIR, "sensor.kicad_pcb")
PROJECT = os.path.join(SENSOR_DIR, "sensor.kicad_pro")


def main():
    before = open(PCB, "rb").read()
    keep = open(PROJECT, "rb").read() if os.path.exists(PROJECT) else None

    board = pcbnew.LoadBoard(PCB)
    board.Save(PCB)

    # board.Save() also rewrites the .kicad_pro from pcbnew's model, dropping
    # every eeschema-only key (bom presets, ngspice, erc severities).  Nothing
    # here changes the board, so the pre-existing project file is still right:
    # put it back byte-for-byte.  (pcb_build.py does the merge-and-restore
    # version of this, because a real build *does* add design rules.)
    if keep is not None and open(PROJECT, "rb").read() != keep:
        open(PROJECT, "wb").write(keep)
        print("  sensor.kicad_pro restored (board.Save rewrote it)")

    after = open(PCB, "rb").read()
    print(f"  sensor.kicad_pcb {'re-serialized' if after != before else 'already canonical'}"
          f" ({len(after)} bytes)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
