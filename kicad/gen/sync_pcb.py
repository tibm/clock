"""Apply schematic changes to the ROUTED clock.kicad_pcb — a headless F8.

KiCad 10 exposes no netlist-updater to Python and `kicad-cli pcb` has no
"update from schematic", so this is the stand-in for
*Tools -> Update PCB from Schematic*.  Run it under KiCad's own interpreter:

    /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/\\
        Versions/3.9/bin/python3.9 sync_pcb.py [--dry-run]

What it does, in order:
  1. VALUES      — schematic Value -> footprint Value.
  2. FOOTPRINTS  — swaps a footprint whose library id changed, preserving
                   position, rotation, side, and per-pad nets.
  3. NEW PARTS   — adds footprints for parts that exist only in the schematic,
                   at a position given in NEW_PART_PLACEMENT.
  4. PAD NETS    — assigns every pad the net the schematic says it has,
                   creating nets that do not exist on the board yet.
  5. SHORTS      — after (4) a track can bridge two pads that are no longer on
                   the same net.  Every copper island is unioned and any island
                   touching more than one net has its tracks and vias deleted,
                   because leaving them is a short, not a stale route.

It never adds copper.  Whatever step 5 removes (and whatever the new nets
need) comes back as ratsnest for interactive routing.

Placement/rotation of existing parts is NOT touched — the board is hand-owned
(see pcb_build.py's guard).
"""
from __future__ import annotations
import os
import subprocess
import sys
import tempfile

import pcbnew  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
KICAD_DIR = os.path.dirname(HERE)
SCH = os.path.join(KICAD_DIR, "clock.kicad_sch")
PCB = os.path.join(KICAD_DIR, "clock.kicad_pcb")
KICAD_CLI = "/Applications/KiCad/KiCad.app/Contents/MacOS/kicad-cli"
FPLIB = ("/Applications/KiCad/KiCad.app/Contents/SharedSupport/footprints")

MM = pcbnew.FromMM

# Where a part that is new to the board should land.  Chosen by searching for
# a slot clear of every pad bbox by >=0.35 mm and >=2 mm from the board edge,
# then minimising the distance to the copper of the nets it has to reach.
NEW_PART_PLACEMENT = {
    # D14 = VBAT_SENSE -> +3V3 clamp (REVIEW.md #15).  0.9 mm from VBAT_SENSE
    # copper, 7.6 mm from +3V3.  The slot is clear of every COURTYARD (not
    # just pad boxes), of every B.Cu track of another net, and of every via —
    # a pad-box-only search put it on top of a CELL_TEST track and inside J3's
    # courtyard.
    "D14": (76.50, 80.00, 0, "B.Cu"),
}


def sch_netlist():
    """-> (comps{ref: (value, fpid)}, pads{(ref, pad): netname})."""
    import re
    fd, path = tempfile.mkstemp(suffix=".net")
    os.close(fd)
    subprocess.run([KICAD_CLI, "sch", "export", "netlist", "--format",
                    "kicadsexpr", "-o", path, SCH],
                   capture_output=True, text=True, check=True)
    txt = open(path).read()
    os.unlink(path)
    comps = {m.group(1): (m.group(2), m.group(3)) for m in re.finditer(
        r'\(comp\s+\(ref "([^"]+)"\)\s+\(value "([^"]*)"\)\s+'
        r'\(footprint "([^"]*)"\)', txt)}
    pads = {}
    for m in re.finditer(r'\(net\s+\(code "\d+"\)\s+\(name "([^"]*)"\)(.*?)'
                         r'\n\t\t\)', txt, re.S):
        for a, b in re.findall(r'\(ref "([^"]+)"\)\s+\(pin "([^"]+)"\)',
                               m.group(2)):
            pads[(a, b)] = m.group(1)
    return comps, pads


def get_net(board, name):
    net = board.FindNet(name)
    if net is None:
        net = pcbnew.NETINFO_ITEM(board, name)
        board.Add(net)
    return net


def load_fp(fpid):
    """Load by full library id AND stamp the id back on.

    FootprintLoad() returns a footprint whose FPID carries only the bare
    name, so without SetFPID the board records "D_SOD-123" instead of
    "Diode_SMD:D_SOD-123" — and this script would then see a mismatch and
    swap the part again on every run."""
    lib, name = fpid.split(":", 1)
    fp = pcbnew.FootprintLoad(os.path.join(FPLIB, lib + ".pretty"), name)
    if fp is not None:
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
    return fp


def main():
    dry = "--dry-run" in sys.argv
    comps, spads = sch_netlist()
    board = pcbnew.LoadBoard(PCB)
    log = []

    # ---- 1/2/3: values, footprint swaps, new parts ----
    for ref, (value, fpid) in sorted(comps.items()):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            if ref not in NEW_PART_PLACEMENT:
                log.append(f"!! {ref} is new but has no NEW_PART_PLACEMENT entry")
                continue
            x, y, rot, side = NEW_PART_PLACEMENT[ref]
            nfp = load_fp(fpid)
            if nfp is None:
                log.append(f"!! {ref}: cannot load {fpid}")
                continue
            nfp.SetReference(ref)
            nfp.SetValue(value)
            board.Add(nfp)
            nfp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
            if side == "B.Cu":
                nfp.Flip(nfp.GetPosition(), False)
            nfp.SetOrientationDegrees(rot)
            log.append(f"ADD  {ref} ({fpid}) at ({x}, {y}) {side}")
            continue
        if fp.GetValue() != value:
            log.append(f"VAL  {ref}: {fp.GetValue()!r} -> {value!r}")
            if not dry:
                fp.SetValue(value)
        if fp.GetFPIDAsString() != fpid:
            keep = {p.GetNumber(): p.GetNetname() for p in fp.Pads()}
            pos, rot = fp.GetPosition(), fp.GetOrientation()
            flipped = fp.IsFlipped()
            nfp = load_fp(fpid)
            if nfp is None:
                log.append(f"!! {ref}: cannot load {fpid}")
                continue
            nfp.SetReference(ref)
            nfp.SetValue(value)
            board.Add(nfp)
            nfp.SetPosition(pos)
            if flipped:
                nfp.Flip(pos, False)
            nfp.SetOrientation(rot)
            for p in nfp.Pads():
                nm = keep.get(p.GetNumber())
                if nm:
                    p.SetNet(get_net(board, nm))
            board.Remove(fp)
            log.append(f"FP   {ref}: -> {fpid} (position/rotation/side/nets kept)")

    # ---- 4: pad nets ----
    changed_pads = set()
    for (ref, num), want in sorted(spads.items()):
        fp = board.FindFootprintByReference(ref)
        if fp is None:
            continue
        for p in fp.Pads():
            if p.GetNumber() != num:
                continue
            if p.GetNetname() != want:
                log.append(f"NET  {ref}.{num}: {p.GetNetname()!r} -> {want!r}")
                changed_pads.add((ref, num))
                if not dry:
                    p.SetNet(get_net(board, want))

    # ---- 5: delete copper that now bridges two nets ----
    # A pad whose net changed leaves its old stub bridging two nets.  Cutting
    # the whole offending island would be wrong — Q4's two-pad swap makes the
    # ENTIRE +5V tree one mixed island — so cut MINIMALLY: only tracks that
    # actually touch a changed pad, re-evaluating until nothing is mixed.
    #
    # Layer-aware: a via ties all copper layers at its XY, a through-hole pad
    # likewise, an SMD pad only its own side.  KiCad 10 numbering is not
    # contiguous (F_Cu=0, B_Cu=2, In1_Cu=4, In2_Cu=6) and pad.GetLayer()
    # reports F_Cu even for a back-side pad, so only IsOnLayer() is truthful.
    # Zones are ignored: they are all GND, so a GND island cannot mix nets.
    LAYERS = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)

    def key(pt, layer):
        return (round(pt.x / 1000), round(pt.y / 1000), layer)   # 1 um grid

    def pad_keys(p):
        lays = [L for L in LAYERS if p.IsOnLayer(L)] or [pcbnew.F_Cu]
        return [key(p.GetPosition(), L) for L in lays]

    cut_keys = set()
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if (fp.GetReference(), p.GetNumber()) in changed_pads:
                cut_keys.update(pad_keys(p))

    removed = 0
    alive = list(board.GetTracks())
    while True:
        parent = {}

        def find(a):
            parent.setdefault(a, a)
            while parent[a] != a:
                parent[a] = parent[parent[a]]
                a = parent[a]
            return a

        def union(a, b):
            ra, rb = find(a), find(b)
            if ra != rb:
                parent[ra] = rb

        for t in alive:
            if t.Type() == pcbnew.PCB_VIA_T:
                ks = [key(t.GetStart(), L) for L in LAYERS]
                for k in ks[1:]:
                    union(ks[0], k)
            else:
                union(key(t.GetStart(), t.GetLayer()),
                      key(t.GetEnd(), t.GetLayer()))

        islands = {}
        for fp in board.GetFootprints():
            for p in fp.Pads():
                want = spads.get((fp.GetReference(), p.GetNumber()),
                                 p.GetNetname())
                if not want or want.startswith("unconnected-"):
                    continue
                ks = pad_keys(p)
                for k in ks[1:]:
                    union(ks[0], k)
                islands.setdefault(find(ks[0]), set()).add(want)

        mixed = {g for g, nets in islands.items() if len(nets) > 1}
        if not mixed:
            break

        def roots_of(t):
            if t.Type() == pcbnew.PCB_VIA_T:
                return [find(key(t.GetStart(), L)) for L in LAYERS]
            L = t.GetLayer()
            return [find(key(t.GetStart(), L)), find(key(t.GetEnd(), L))]

        def touches_changed(t):
            if t.Type() == pcbnew.PCB_VIA_T:
                return key(t.GetStart(), pcbnew.F_Cu)[:2] in \
                    {k[:2] for k in cut_keys}
            L = t.GetLayer()
            return (key(t.GetStart(), L) in cut_keys
                    or key(t.GetEnd(), L) in cut_keys)

        batch = [t for t in alive
                 if any(r in mixed for r in roots_of(t)) and touches_changed(t)]
        if not batch:
            # nothing incident to a changed pad can explain it — cut the
            # island wholesale rather than leave a short, and say so loudly.
            batch = [t for t in alive if any(r in mixed for r in roots_of(t))]
            log.append(f"!! no changed-pad track explains a mixed island; "
                       f"cutting {len(batch)} items wholesale")
        for t in batch:
            log.append(f"CUT  {'via' if t.Type() == pcbnew.PCB_VIA_T else 'track'}"
                       f" {t.GetNetname()!r} "
                       f"({t.GetStart().x/1e6:.2f},{t.GetStart().y/1e6:.2f})"
                       f"-({t.GetEnd().x/1e6:.2f},{t.GetEnd().y/1e6:.2f})")
            alive.remove(t)
            if not dry:
                board.Remove(t)
            removed += 1

    # ---- 6: report stubs left dangling by the cuts ----
    # A cut can leave a short fragment attached at one end only.  It is not a
    # short, but it is stale copper that trips track_dangling and can violate
    # clearance against the pad it no longer reaches.  Deliberately NOT fixed
    # here: KiCad ships Edit -> Cleanup Tracks & Vias, which does this
    # properly (and re-fetching tracks through SWIG after Remove() hands back
    # unusable wrappers).  Run that in the GUI after this script.
    log.append("NOTE run Edit -> Cleanup Tracks & Vias in pcbnew afterwards "
               "to sweep up dangling fragments left by the cuts")

    for line in log:
        print(" ", line)
    print(f"\n{len(log)} actions, {removed} tracks/vias cut"
          + ("  [DRY RUN — nothing written]" if dry else ""))
    if not dry:
        board.Save(PCB)
        print(f"saved {PCB}")


if __name__ == "__main__":
    main()
