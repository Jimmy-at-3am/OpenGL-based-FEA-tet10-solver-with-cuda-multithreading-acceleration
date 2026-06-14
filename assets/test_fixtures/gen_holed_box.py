#!/usr/bin/env python3
# =============================================================================
# gen_holed_box.py  --  new_TODO_04 slice-contour test fixture generator.
#
# Emits an ASCII STL of a 4x4x4 box (X,Y,Z in [-2,2]) with a 1x1 square
# THROUGH-HOLE along the Z (build) axis. The hole is deliberately OFF-CENTRE
# (X in [0.5,1.5], Y in [-0.5,0.5]) so that the outer square's centroid (0,0)
# lies in solid material rather than inside the hole -- the slicer classifies
# holes by centroid even-odd containment, and a concentric hole would push the
# outer loop's centroid into the void and misclassify it.
#
# Every interior Z plane therefore cuts a clean 2-loop annulus: an outer 4x4
# square (CCW outer) and an inner 1x1 square (CW hole).
#
# After the loader's centre+scale (3/maxDim = 0.75): outer 3x3 (area 9),
# hole 0.75x0.75 (area 0.5625) -> holeAreaFraction 0.0625, netAreaFraction
# 0.9375 (both scale-invariant ratios).
#
# Run:  python assets/test_fixtures/gen_holed_box.py
# =============================================================================
import os

# Outer box extents and offset hole extents (original model units).
OX0, OX1, OY0, OY1, Z0, Z1 = -2.0, 2.0, -2.0, 2.0, -2.0, 2.0
HX0, HX1, HY0, HY1 = 0.5, 1.5, -0.5, 0.5

tris = []  # list of (v0, v1, v2)

def quad(a, b, c, d):
    """Two triangles for quad a->b->c->d (winding as given)."""
    tris.append((a, b, c))
    tris.append((a, c, d))

# ---- Outer walls (4 vertical faces, outward winding) --------------------
# y = OY0 (front, -Y)
quad((OX0, OY0, Z0), (OX1, OY0, Z0), (OX1, OY0, Z1), (OX0, OY0, Z1))
# x = OX1 (right, +X)
quad((OX1, OY0, Z0), (OX1, OY1, Z0), (OX1, OY1, Z1), (OX1, OY0, Z1))
# y = OY1 (back, +Y)
quad((OX1, OY1, Z0), (OX0, OY1, Z0), (OX0, OY1, Z1), (OX1, OY1, Z1))
# x = OX0 (left, -X)
quad((OX0, OY1, Z0), (OX0, OY0, Z0), (OX0, OY0, Z1), (OX0, OY1, Z1))

# ---- Inner hole walls (4 vertical faces, facing INTO the hole) ----------
# x = HX0 (hole left wall)
quad((HX0, HY0, Z0), (HX0, HY1, Z0), (HX0, HY1, Z1), (HX0, HY0, Z1))
# y = HY1 (hole back wall)
quad((HX0, HY1, Z0), (HX1, HY1, Z0), (HX1, HY1, Z1), (HX0, HY1, Z1))
# x = HX1 (hole right wall)
quad((HX1, HY1, Z0), (HX1, HY0, Z0), (HX1, HY0, Z1), (HX1, HY1, Z1))
# y = HY0 (hole front wall)
quad((HX1, HY0, Z0), (HX0, HY0, Z0), (HX0, HY0, Z1), (HX1, HY0, Z1))

# ---- Annulus caps (top z=Z1, bottom z=Z0): 4 frame quads each -----------
# Outer corners / inner corners at a given z, named by (x-sign, y-sign).
def caps(z):
    Omm, Opm, Opp, Omp = (OX0, OY0, z), (OX1, OY0, z), (OX1, OY1, z), (OX0, OY1, z)
    Hmm, Hpm, Hpp, Hmp = (HX0, HY0, z), (HX1, HY0, z), (HX1, HY1, z), (HX0, HY1, z)
    return Omm, Opm, Opp, Omp, Hmm, Hpm, Hpp, Hmp

# Top cap (+Z): frame of 4 quads tiling the annulus.
Omm, Opm, Opp, Omp, Hmm, Hpm, Hpp, Hmp = caps(Z1)
quad(Omm, Opm, Hpm, Hmm)   # bottom strip
quad(Opm, Opp, Hpp, Hpm)   # right strip
quad(Opp, Omp, Hmp, Hpp)   # top strip
quad(Omp, Omm, Hmm, Hmp)   # left strip

# Bottom cap (-Z): reverse winding (faces -Z).
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


out_path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "holed_box.stl")
with open(out_path, "w", encoding="ascii") as f:
    f.write("solid holed_box\n")
    for (a, b, c) in tris:
        n = normal(a, b, c)
        f.write("  facet normal %.6e %.6e %.6e\n" % n)
        f.write("    outer loop\n")
        for v in (a, b, c):
            f.write("      vertex %.6e %.6e %.6e\n" % v)
        f.write("    endloop\n")
        f.write("  endfacet\n")
    f.write("endsolid holed_box\n")

print("wrote %s  (%d triangles)" % (out_path, len(tris)))
