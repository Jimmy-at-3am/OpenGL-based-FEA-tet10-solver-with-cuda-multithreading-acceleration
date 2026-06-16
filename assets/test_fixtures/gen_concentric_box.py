#!/usr/bin/env python3
# =============================================================================
# gen_concentric_box.py  --  regression fixture for the concentric-hole bug.
#
# Emits an ASCII STL of a 4x4x4 box (X,Y,Z in [-2,2]) with a 1x1 square
# THROUGH-HOLE along Z that is CONCENTRIC with the outer box (hole X,Y in
# [-0.5,0.5]). This is the case that broke the original slicer: the hole
# classifier tested each loop's CENTROID, and the centered outer loop's centroid
# (0,0) falls inside the hole -> the outer loop was misclassified as a hole,
# collapsing outerArea to 0 and yielding a NEGATIVE net area.
#
# The fix (loopProbePoint: classify by a boundary point of each loop instead of
# the centroid) makes this slice correctly: every interior Z plane is a clean
# 2-loop annulus (outer CCW, hole CW), positive net area.
#
# After the loader's centre+scale (3/maxDim = 0.75): outer 3x3 (area 9), hole
# 0.75x0.75 (area 0.5625) -> holeAreaFraction 0.0625, netAreaFraction 0.9375
# (both scale-invariant) -- identical analytic truth to the off-centre fixture,
# but with the hole centred to exercise the concentric classification path.
#
# Run:  python assets/test_fixtures/gen_concentric_box.py
# =============================================================================
import os

OX0, OX1, OY0, OY1, Z0, Z1 = -2.0, 2.0, -2.0, 2.0, -2.0, 2.0
HX0, HX1, HY0, HY1 = -0.5, 0.5, -0.5, 0.5   # CONCENTRIC hole

tris = []

def quad(a, b, c, d):
    tris.append((a, b, c))
    tris.append((a, c, d))

# Outer walls
quad((OX0, OY0, Z0), (OX1, OY0, Z0), (OX1, OY0, Z1), (OX0, OY0, Z1))
quad((OX1, OY0, Z0), (OX1, OY1, Z0), (OX1, OY1, Z1), (OX1, OY0, Z1))
quad((OX1, OY1, Z0), (OX0, OY1, Z0), (OX0, OY1, Z1), (OX1, OY1, Z1))
quad((OX0, OY1, Z0), (OX0, OY0, Z0), (OX0, OY0, Z1), (OX0, OY1, Z1))

# Inner hole walls (facing into the hole)
quad((HX0, HY0, Z0), (HX0, HY1, Z0), (HX0, HY1, Z1), (HX0, HY0, Z1))
quad((HX0, HY1, Z0), (HX1, HY1, Z0), (HX1, HY1, Z1), (HX0, HY1, Z1))
quad((HX1, HY1, Z0), (HX1, HY0, Z0), (HX1, HY0, Z1), (HX1, HY1, Z1))
quad((HX1, HY0, Z0), (HX0, HY0, Z0), (HX0, HY0, Z1), (HX1, HY0, Z1))

def caps(z):
    Omm, Opm, Opp, Omp = (OX0, OY0, z), (OX1, OY0, z), (OX1, OY1, z), (OX0, OY1, z)
    Hmm, Hpm, Hpp, Hmp = (HX0, HY0, z), (HX1, HY0, z), (HX1, HY1, z), (HX0, HY1, z)
    return Omm, Opm, Opp, Omp, Hmm, Hpm, Hpp, Hmp

# Top cap (+Z)
Omm, Opm, Opp, Omp, Hmm, Hpm, Hpp, Hmp = caps(Z1)
quad(Omm, Opm, Hpm, Hmm)
quad(Opm, Opp, Hpp, Hpm)
quad(Opp, Omp, Hmp, Hpp)
quad(Omp, Omm, Hmm, Hmp)

# Bottom cap (-Z)
Omm, Opm, Opp, Omp, Hmm, Hpm, Hpp, Hmp = caps(Z0)
quad(Hmm, Hpm, Opm, Omm)
quad(Hpm, Hpp, Opp, Opm)
quad(Hpp, Hmp, Omp, Opp)
quad(Hmp, Hmm, Omm, Omp)


def normal(a, b, c):
    ux, uy, uz = b[0]-a[0], b[1]-a[1], b[2]-a[2]
    vx, vy, vz = c[0]-a[0], c[1]-a[1], c[2]-a[2]
    nx, ny, nz = uy*vz - uz*vy, uz*vx - ux*vz, ux*vy - uy*vx
    L = (nx*nx + ny*ny + nz*nz) ** 0.5
    if L < 1e-12:
        return (0.0, 0.0, 0.0)
    return (nx/L, ny/L, nz/L)


out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "concentric_box.stl")
with open(out_path, "w", encoding="ascii", newline="\n") as f:
    f.write("solid concentric_box\n")
    for (a, b, c) in tris:
        n = normal(a, b, c)
        f.write("  facet normal %.6e %.6e %.6e\n" % n)
        f.write("    outer loop\n")
        for v in (a, b, c):
            f.write("      vertex %.6e %.6e %.6e\n" % v)
        f.write("    endloop\n")
        f.write("  endfacet\n")
    f.write("endsolid concentric_box\n")

print("wrote %s  (%d triangles)" % (out_path, len(tris)))
