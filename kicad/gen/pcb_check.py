"""Loads clock.kicad_pcb and reports (a) any footprint whose courtyard
intersects the motor's centre keepout circle, and (b) any pair of
same-side footprints whose courtyards overlap. Run after pcb_build.py.
"""
import itertools
import os
import pcbnew

HERE = os.path.dirname(os.path.abspath(__file__))
PCB = os.path.join(os.path.dirname(HERE), "clock.kicad_pcb")

CX, CY, KEEPOUT_R = 55.0, 55.0, 20.0  # 1mm margin beyond the ~19mm courtyard


def bbox_mm(fp):
    """True physical extent from pad geometry, NOT fp.GetBoundingBox() --
    that includes reference/value silkscreen text, which for many footprints
    here (custom clock:* parts, and stock parts whose Value is the full
    lib:footprint string) is far wider than the part and was producing
    massive false-positive overlaps/keepout hits."""
    pads = list(fp.Pads())
    if not pads:
        b = fp.GetBoundingBox()
        return (pcbnew.ToMM(b.GetLeft()), pcbnew.ToMM(b.GetTop()),
                pcbnew.ToMM(b.GetRight()), pcbnew.ToMM(b.GetBottom()))
    xs0, ys0, xs1, ys1 = [], [], [], []
    for p in pads:
        b = p.GetBoundingBox()
        xs0.append(b.GetLeft()); xs1.append(b.GetRight())
        ys0.append(b.GetTop()); ys1.append(b.GetBottom())
    return (pcbnew.ToMM(min(xs0)), pcbnew.ToMM(min(ys0)),
            pcbnew.ToMM(max(xs1)), pcbnew.ToMM(max(ys1)))


def rects_overlap(a, b, pad=0.0):
    ax0, ay0, ax1, ay1 = a
    bx0, by0, bx1, by1 = b
    return not (ax1 + pad <= bx0 or bx1 + pad <= ax0 or
                ay1 + pad <= by0 or by1 + pad <= ay0)


def circle_rect_overlap(cx, cy, r, rect):
    x0, y0, x1, y1 = rect
    nx = max(x0, min(cx, x1))
    ny = max(y0, min(cy, y1))
    return (nx - cx) ** 2 + (ny - cy) ** 2 < r * r


def main():
    board = pcbnew.LoadBoard(PCB)
    fps = list(board.GetFootprints())
    print(f"{len(fps)} footprints on board")

    print("\n--- motor keepout violations (excluding M1) ---")
    n = 0
    for fp in fps:
        if fp.GetReference() == "M1":
            continue
        r = bbox_mm(fp)
        if circle_rect_overlap(CX, CY, KEEPOUT_R, r):
            print(f"  {fp.GetReference():6s} {fp.GetLayerName():5s} bbox={r}")
            n += 1
    print(f"  {n} violations")

    print("\n--- board-edge overflow (outside 0..110) ---")
    n = 0
    for fp in fps:
        x0, y0, x1, y1 = bbox_mm(fp)
        if x0 < -0.1 or y0 < -0.1 or x1 > 110.1 or y1 > 110.1:
            print(f"  {fp.GetReference():6s} bbox=({x0:.1f},{y0:.1f})-({x1:.1f},{y1:.1f})")
            n += 1
    print(f"  {n} violations")

    print("\n--- same-side courtyard overlaps ---")
    # A through-hole part (e.g. a plated mounting hole) occupies both
    # F.Cu and B.Cu regardless of its nominal footprint layer, so it must
    # be checked against both sides -- checking only its own side missed a
    # real MH<->BT1 collision earlier.
    def is_thru(fp):
        return any(p.GetAttribute() == pcbnew.PAD_ATTRIB_PTH for p in fp.Pads())

    by_side = {"F": [], "B": []}
    for fp in fps:
        entry = (fp.GetReference(), bbox_mm(fp))
        if is_thru(fp):
            by_side["F"].append(entry)
            by_side["B"].append(entry)
        else:
            side = "F" if fp.GetLayerName() == "F.Cu" else "B"
            by_side[side].append(entry)

    n = 0
    for side, lst in by_side.items():
        for (r1, b1), (r2, b2) in itertools.combinations(lst, 2):
            if rects_overlap(b1, b2, pad=-0.05):  # allow 0.05mm touch tolerance
                print(f"  [{side}] {r1:6s} <-> {r2:6s}")
                n += 1
    print(f"  {n} overlaps")


if __name__ == "__main__":
    main()
