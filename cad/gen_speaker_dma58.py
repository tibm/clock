#!/usr/bin/env python3
"""Generate a simplified volume envelope of the Dayton Audio DMA58-4 (2", 4 ohm)
as a watertight binary STL, for enclosure fit checks in FreeCAD.

Source: datasheet/speaker_dma58-4.pdf p.1 outline drawing. Labelled values are
used as-is (56.0 flange, 4-O3.3 on O64.3 BCD, 3.0 flange, 31.8 depth, 50.0
cutout); unlabelled steps of the basket/magnet profile were scaled off the same
drawing (12.915 px/mm at 400 dpi) and are approximate.

Frame: origin = centre of the flange FRONT face, +Z out of the baffle.
  z = +2.5   top of cone/surround (highest point)
  z =  0.0   flange front face
  z = -3.0   flange rear face (datum for the 31.8)
  z = -4.9   gasket rear face  = baffle mounting plane
  z = -34.8  rear of magnet
NOT a true displacement model: the real basket is an open 8-spoke frame, so the
solid here (~61 cm3) over-states the volume it actually steals from the box.
"""
import math
import struct

# ---------------------------------------------------------------- dimensions
FLANGE = 56.0          # square flange, across flats            [datasheet]
CORNER_R = 4.0         # flange corner radius                   (scaled)
PLATE_T = 3.0          # flange plate thickness                 [datasheet]
GASKET_T = 1.9         # rear gasket band, full 56 wide         (scaled)
BCD = 64.3             # bolt circle diameter, holes at 45 deg  [datasheet]
HOLE_D = 3.3           # 4x mounting holes                      [datasheet]
DEPTH = 31.8           # flange rear face -> rear of magnet     [datasheet]
CUTOUT = 50.0          # recommended baffle cutout              [datasheet]

DOME_H = 2.5           # cone/surround proud of the flange      (scaled)
DOME_D_BASE = 48.5     # its diameter at the flange face        (scaled)
DOME_D_TOP = 45.2      # its diameter at the top                (scaled)
BASKET_D = 48.6        # basket OD under the flange             (scaled)

SEG = 96               # segments on the body/dome revolutions
HOLE_SEG = 24          # segments per mounting hole
ARC_SEG = 12           # segments per flange corner arc (even -> 45 deg node)

Z_FRONT = 0.0
Z_PLATE_REAR = -PLATE_T
Z_MOUNT = -(PLATE_T + GASKET_T)        # -4.9, baffle face
Z_REAR = Z_PLATE_REAR - DEPTH          # -34.8

# (z, radius) of the surface of revolution behind the flange. Equal-z pairs
# make a flat annular step.
REAR_PROFILE = [
    (Z_MOUNT, BASKET_D / 2),
    (-13.2, BASKET_D / 2),             # straight basket wall
    (-22.3, 20.15),                    # taper down to the magnet top plate
    (-23.8, 20.15),
    (-23.8, 17.70),                    # step onto the magnet
    (-30.5, 17.70),
    (Z_REAR, 14.00),                   # rounded rear, simplified to a taper
]
FRONT_PROFILE = [
    (Z_FRONT, DOME_D_BASE / 2),
    (DOME_H, DOME_D_TOP / 2),
]

HOLE_R = HOLE_D / 2
HOLE_OFF = BCD / 2 * math.cos(math.radians(45))     # 22.734
HOLE_CENTRES = [(HOLE_OFF, HOLE_OFF), (-HOLE_OFF, HOLE_OFF),
                (-HOLE_OFF, -HOLE_OFF), (HOLE_OFF, -HOLE_OFF)]
HOLE_DIRS = [45.0, 135.0, 225.0, 315.0]             # outward diagonal


# ------------------------------------------------------------------ polygons
def rounded_square():
    """Flange outline, CCW, starting at (+28, 0). Includes the 45 deg node on
    every corner arc (used as a bridge anchor)."""
    h = FLANGE / 2
    f = h - CORNER_R
    pts = [(h, 0.0)]
    for cx, cy, a0 in ((f, f, 0.0), (-f, f, 90.0), (-f, -f, 180.0), (f, -f, 270.0)):
        pts.append((cx + CORNER_R * math.cos(math.radians(a0)),
                    cy + CORNER_R * math.sin(math.radians(a0))))
        for i in range(1, ARC_SEG + 1):
            a = math.radians(a0 + 90.0 * i / ARC_SEG)
            pts.append((cx + CORNER_R * math.cos(a), cy + CORNER_R * math.sin(a)))
    pts.append((h, -0.0))
    # drop consecutive duplicates and the closing point
    out = []
    for p in pts:
        if not out or (abs(p[0] - out[-1][0]) > 1e-9 or abs(p[1] - out[-1][1]) > 1e-9):
            out.append(p)
    if abs(out[0][0] - out[-1][0]) < 1e-9 and abs(out[0][1] - out[-1][1]) < 1e-9:
        out.pop()
    return out


def circle(cx, cy, r, n, start_deg=0.0, cw=False):
    pts = []
    for i in range(n):
        a = math.radians(start_deg) + 2 * math.pi * i / n * (-1 if cw else 1)
        pts.append((cx + r * math.cos(a), cy + r * math.sin(a)))
    return pts


def find_index(poly, pt, eps=1e-6):
    for i, q in enumerate(poly):
        if abs(q[0] - pt[0]) < eps and abs(q[1] - pt[1]) < eps:
            return i
    raise ValueError("bridge anchor %r not on outline" % (pt,))


def merge_hole(outer, hole, oi, hi):
    """Splice a CW hole loop into a CCW outer loop with a zero-width bridge."""
    return outer[:oi + 1] + hole[hi:] + hole[:hi + 1] + outer[oi:]


def area2(poly):
    s = 0.0
    for i in range(len(poly)):
        x0, y0 = poly[i]
        x1, y1 = poly[(i + 1) % len(poly)]
        s += x0 * y1 - x1 * y0
    return s


def earclip(poly):
    """Ear clipping for a CCW (weakly) simple polygon. Returns index triples
    into `poly`."""
    n = len(poly)
    idx = list(range(n))
    tris = []
    eps = 1e-9

    def cross(a, b, c):
        return ((poly[b][0] - poly[a][0]) * (poly[c][1] - poly[a][1]) -
                (poly[b][1] - poly[a][1]) * (poly[c][0] - poly[a][0]))

    def inside(a, b, c, p):
        d1 = cross(a, b, p)
        d2 = cross(b, c, p)
        d3 = cross(c, a, p)
        return d1 > eps and d2 > eps and d3 > eps

    guard = 0
    while len(idx) > 3:
        guard += 1
        if guard > 4 * n:
            raise RuntimeError("ear clipping stalled")
        reflex = []
        for k in range(len(idx)):
            a, b, c = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            if cross(a, b, c) <= 0:
                reflex.append(b)
        clipped = False
        for k in range(len(idx)):
            a, b, c = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
            if cross(a, b, c) <= eps:
                continue
            if any(r not in (a, b, c) and inside(a, b, c, r) for r in reflex):
                continue
            tris.append((a, b, c))
            idx.pop(k)
            clipped = True
            break
        if not clipped:                       # relax: take any convex corner
            for k in range(len(idx)):
                a, b, c = idx[k - 1], idx[k], idx[(k + 1) % len(idx)]
                if cross(a, b, c) > eps:
                    tris.append((a, b, c))
                    idx.pop(k)
                    clipped = True
                    break
        if not clipped:
            raise RuntimeError("no convex corner left")
    tris.append(tuple(idx))
    return tris


def flange_face(inner_r):
    """Flange outline minus the central body opening minus the 4 bolt holes,
    triangulated. Returns a list of (p0, p1, p2) 2-D triples, CCW."""
    poly = rounded_square()
    hole_loops = []
    # central opening: bridge along +X from (inner_r, 0) to (28, 0)
    hole_loops.append((circle(0.0, 0.0, inner_r, SEG, 0.0, cw=True), (FLANGE / 2, 0.0)))
    # bolt holes: bridge outward along each diagonal to the corner-arc node
    f = FLANGE / 2 - CORNER_R
    for (cx, cy), ang in zip(HOLE_CENTRES, HOLE_DIRS):
        a = math.radians(ang)
        anchor = (math.copysign(f, cx) + CORNER_R * math.cos(a),
                  math.copysign(f, cy) + CORNER_R * math.sin(a))
        hole_loops.append((circle(cx, cy, HOLE_R, HOLE_SEG, ang, cw=True), anchor))
    for loop, anchor in hole_loops:
        poly = merge_hole(poly, loop, find_index(poly, anchor), 0)
    return [(poly[a], poly[b], poly[c]) for a, b, c in earclip(poly)]


# --------------------------------------------------------------------- mesh
tris = []


def tri(p0, p1, p2):
    tris.append((p0, p1, p2))


def quad(p0, p1, p2, p3):
    tri(p0, p1, p2)
    tri(p0, p2, p3)


def revolve(profile, n=SEG):
    """Lateral surface of a profile [(z, r), ...], outward normals."""
    for (z0, r0), (z1, r1) in zip(profile, profile[1:]):
        for i in range(n):
            a0 = 2 * math.pi * i / n
            a1 = 2 * math.pi * (i + 1) / n
            c0, s0 = math.cos(a0), math.sin(a0)
            c1, s1 = math.cos(a1), math.sin(a1)
            p00 = (r0 * c0, r0 * s0, z0)
            p01 = (r0 * c1, r0 * s1, z0)
            p10 = (r1 * c0, r1 * s0, z1)
            p11 = (r1 * c1, r1 * s1, z1)
            if z1 < z0 or (z1 == z0 and r1 < r0):
                quad(p00, p10, p11, p01)
            else:
                quad(p00, p01, p11, p10)


def cap(r, z, up, n=SEG):
    for i in range(n):
        a0 = 2 * math.pi * i / n
        a1 = 2 * math.pi * (i + 1) / n
        p0 = (r * math.cos(a0), r * math.sin(a0), z)
        p1 = (r * math.cos(a1), r * math.sin(a1), z)
        tri((0.0, 0.0, z), p0, p1) if up else tri((0.0, 0.0, z), p1, p0)


# flange faces
for a, b, c in flange_face(DOME_D_BASE / 2):
    tri((a[0], a[1], Z_FRONT), (b[0], b[1], Z_FRONT), (c[0], c[1], Z_FRONT))
for a, b, c in flange_face(BASKET_D / 2):
    tri((a[0], a[1], Z_MOUNT), (c[0], c[1], Z_MOUNT), (b[0], b[1], Z_MOUNT))

# flange outer wall
outline = rounded_square()
for i in range(len(outline)):
    x0, y0 = outline[i]
    x1, y1 = outline[(i + 1) % len(outline)]
    quad((x0, y0, Z_MOUNT), (x1, y1, Z_MOUNT), (x1, y1, Z_FRONT), (x0, y0, Z_FRONT))

# mounting hole walls (normals point into the hole)
for (cx, cy), ang in zip(HOLE_CENTRES, HOLE_DIRS):
    loop = circle(cx, cy, HOLE_R, HOLE_SEG, ang, cw=True)
    for i in range(len(loop)):
        x0, y0 = loop[i]
        x1, y1 = loop[(i + 1) % len(loop)]
        quad((x0, y0, Z_MOUNT), (x1, y1, Z_MOUNT), (x1, y1, Z_FRONT), (x0, y0, Z_FRONT))

# cone/surround proud of the flange, and the basket/magnet behind it
revolve(FRONT_PROFILE)
cap(DOME_D_TOP / 2, DOME_H, up=True)
revolve(REAR_PROFILE)
cap(REAR_PROFILE[-1][1], Z_REAR, up=False)


# ----------------------------------------------------------------- validate
def check():
    edges = {}
    vol = 0.0
    for p0, p1, p2 in tris:
        vol += (p0[0] * (p1[1] * p2[2] - p2[1] * p1[2]) -
                p1[0] * (p0[1] * p2[2] - p2[1] * p0[2]) +
                p2[0] * (p0[1] * p1[2] - p1[1] * p0[2])) / 6.0
        for a, b in ((p0, p1), (p1, p2), (p2, p0)):
            ka = tuple(round(v, 6) for v in a)
            kb = tuple(round(v, 6) for v in b)
            if ka == kb:
                continue
            edges[(ka, kb)] = edges.get((ka, kb), 0) + 1
    bad = [e for e, n in edges.items() if n != 1 or edges.get((e[1], e[0]), 0) != 1]
    return vol, len(bad)


volume, unpaired = check()
zs = [p[2] for t in tris for p in t]
xs = [p[0] for t in tris for p in t]
print("triangles   : %d" % len(tris))
print("bbox        : %.1f x %.1f x %.1f mm" %
      (max(xs) - min(xs), max(xs) - min(xs), max(zs) - min(zs)))
print("z range     : %.1f .. %.1f" % (min(zs), max(zs)))
print("volume      : %.1f cm3 (solid envelope)" % (volume / 1000.0))
print("open edges  : %d" % unpaired)
assert unpaired == 0, "mesh is not watertight"
assert volume > 0, "inverted normals"

# --------------------------------------------------------------------- write
out = "/Users/tibo/Developer/PRIVATE/clock/cad/speaker_dma58-4.stl"
with open(out, "wb") as fh:
    fh.write(b"Dayton DMA58-4 volume envelope - generated, see gen_speaker_dma58.py".ljust(80, b" "))
    fh.write(struct.pack("<I", len(tris)))
    for p0, p1, p2 in tris:
        ux, uy, uz = (p1[i] - p0[i] for i in range(3))
        vx, vy, vz = (p2[i] - p0[i] for i in range(3))
        nx, ny, nz = uy * vz - uz * vy, uz * vx - ux * vz, ux * vy - uy * vx
        ln = math.sqrt(nx * nx + ny * ny + nz * nz) or 1.0
        fh.write(struct.pack("<3f", nx / ln, ny / ln, nz / ln))
        for p in (p0, p1, p2):
            fh.write(struct.pack("<3f", *p))
        fh.write(struct.pack("<H", 0))
print("wrote       : %s" % out)
