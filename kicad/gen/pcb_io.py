"""Saving a .kicad_pcb from a script without collateral damage.

`board.Save()` also rewrites the sibling `.kicad_pro` from pcbnew's own model,
which drops every key only eeschema knows about (bom presets, ngspice, erc
severities ...) and can reset the design rules to defaults -- after which
`kicad-cli pcb drc` silently checks the board against 0.2 mm/default limits and
reports hundreds of phantom violations.  It touches the `.kicad_prl` too.

Any tool that edits geometry and nothing else should therefore save through
`save_board()`, which puts both sidecars back byte for byte.  (`pcb_build.py`
is the exception: it *does* write design rules, so it merges the eeschema-only
keys back instead -- see the sensor board's `restore_project()`.)
"""
import os


def save_board(board, pcb, verbose=True):
    pcb = os.path.abspath(pcb)
    sidecars = {}
    for ext in (".kicad_pro", ".kicad_prl"):
        p = os.path.splitext(pcb)[0] + ext
        if os.path.exists(p):
            with open(p, "rb") as f:
                sidecars[p] = f.read()
    board.Save(pcb)
    for p, blob in sidecars.items():
        with open(p, "rb") as f:
            if f.read() == blob:
                continue
        with open(p, "wb") as f:
            f.write(blob)
        if verbose:
            print(f"  restored {os.path.basename(p)} "
                  f"(pcbnew had rewritten it)")
    return pcb
