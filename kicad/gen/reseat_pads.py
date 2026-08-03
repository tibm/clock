"""Re-seat an already-placed footprint's pads from its library definition.

`pcb_build.py` rebuilds the board from scratch, so it cannot be re-run on a
hand-routed one.  When a footprint in `<project>/<nick>.pretty` is *corrected*
after routing, this applies exactly that correction to the instance already on
the board and nothing else:

* every pad whose FP-relative position differs from the library's is moved to
  the library position;
* any track or via endpoint sitting exactly on a pad's old centre is moved with
  it (same net, and a layer the pad is on), so the routing stays attached;
* graphic vertices *of the same footprint* that shared the pad's coordinate
  along every axis the pad moved in follow it -- that is what keeps a silk
  pin-1 dot or an F.Fab pin-1 leader on the pad row it marks.  Only shape
  start/end points are considered, not polygon vertices.

Zone fills are NOT recomputed here -- run `pcb_fill.py` (or Edit > Fill All
Zones) afterwards, or DRC will flag the pours still voided around the pads'
old positions.

Run under KiCad's bundled interpreter:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\
Versions/3.9/bin/python3.9 reseat_pads.py ../clock.kicad_pcb U14
"""
import os
import sys
from collections import defaultdict

import pcbnew

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from pcb_io import save_board  # noqa: E402

STOCK_FP = "/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints"


def mm(v):
    return v / 1e6


def lib_of(proj, nick):
    """Project .pretty first (fp-lib-table nickname == directory name)."""
    local = os.path.join(proj, nick + ".pretty")
    return local if os.path.isdir(local) else os.path.join(
        STOCK_FP, nick + ".pretty")


def reseat(board, proj, ref, verbose=True):
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        raise SystemExit(f"{ref}: not on the board")
    fpid = fp.GetFPID()
    libfp = pcbnew.FootprintLoad(lib_of(proj, str(fpid.GetLibNickname())),
                                 str(fpid.GetLibItemName()))
    if libfp is None:
        raise SystemExit(f"{ref}: cannot load {fpid.GetUniStringLibId()}")

    want = defaultdict(list)
    for p in libfp.Pads():
        want[p.GetNumber()].append(p.GetFPRelativePosition())
    seen, moves = defaultdict(int), []
    for p in fp.Pads():
        n = p.GetNumber()
        i, seen[n] = seen[n], seen[n] + 1
        if i >= len(want[n]):
            print(f"  ! {ref}.{n}[{i}] has no counterpart in the library")
            continue
        tgt, cur = want[n][i], p.GetFPRelativePosition()
        if (tgt.x, tgt.y) == (cur.x, cur.y):
            continue
        old = (p.GetPosition().x, p.GetPosition().y)
        p.SetFPRelativePosition(tgt)
        new = (p.GetPosition().x, p.GetPosition().y)
        moves.append((old, new, p))
        if verbose:
            print(f"  pad {n:3s} local ({mm(cur.x):+.3f},{mm(cur.y):+.3f}) -> "
                  f"({mm(tgt.x):+.3f},{mm(tgt.y):+.3f})   board "
                  f"({mm(old[0]):.3f},{mm(old[1]):.3f}) -> "
                  f"({mm(new[0]):.3f},{mm(new[1]):.3f})")
    if not moves:
        print(f"  {ref}: already matches the library")
        return 0

    # 1. routing that landed on the old pad centres
    n_tracks = 0
    for t in board.GetTracks():
        for get, set_ in ((t.GetStart, t.SetStart), (t.GetEnd, t.SetEnd)):
            pt = get()
            for old, new, pad in moves:
                if (pt.x, pt.y) != old or t.GetNetCode() != pad.GetNetCode():
                    continue
                if not t.IsOnLayer(t.GetLayer()) or not (
                        pad.IsOnLayer(t.GetLayer()) or t.GetClass() == "PCB_VIA"):
                    continue
                set_(pcbnew.VECTOR2I(*new))
                n_tracks += 1
                if verbose:
                    print(f"  track {board.GetLayerName(t.GetLayer()):6s} "
                          f"{t.GetNetname()} endpoint follows pad "
                          f"{pad.GetNumber()}")
                break

    # 2. this footprint's own graphics that mark a pad row
    def follow(pt):
        for old, new in ((o, n) for o, n, _ in moves):
            dx, dy = new[0] - old[0], new[1] - old[1]
            if (dx or dy) and (not dx or pt.x == old[0]) \
                    and (not dy or pt.y == old[1]):
                return pcbnew.VECTOR2I(pt.x + dx, pt.y + dy)
        return None

    n_gfx = 0
    for g in fp.GraphicalItems():
        for name in ("Start", "End"):        # texts have neither; shapes do
            get, set_ = getattr(g, "Get" + name, None), \
                getattr(g, "Set" + name, None)
            if get is None or set_ is None:
                continue
            moved = follow(get())
            if moved is not None:
                set_(moved)
                n_gfx += 1
    if verbose and n_gfx:
        print(f"  {n_gfx} graphic point(s) followed the pads")

    # keep the instance's cached library description in step with the .kicad_mod
    for getter, setter in (("GetLibDescription", "SetLibDescription"),
                           ("GetDescription", "SetDescription")):
        if hasattr(libfp, getter) and hasattr(fp, setter):
            getattr(fp, setter)(getattr(libfp, getter)())
            break
    print(f"  {ref}: {len(moves)} pad(s), {n_tracks} track endpoint(s), "
          f"{n_gfx} graphic point(s)")
    return len(moves)


def main(argv):
    if len(argv) < 3:
        sys.exit(__doc__)
    pcb = os.path.abspath(argv[1])
    board = pcbnew.LoadBoard(pcb)
    n = sum(reseat(board, os.path.dirname(pcb), r) for r in argv[2:])
    if n:
        save_board(board, pcb)
        print(f"saved {os.path.basename(pcb)} -- copper moved, so re-fill the "
              f"pours (gen/pcb_fill.py or Edit > Fill All Zones) and re-run DRC")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
