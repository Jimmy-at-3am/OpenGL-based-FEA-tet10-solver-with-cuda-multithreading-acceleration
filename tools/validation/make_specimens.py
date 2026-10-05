#!/usr/bin/env python3
"""make_specimens.py -- generate the test pieces of docs/validation/physical-test-plan.md

Writes, in millimetres and in print orientation (Z = build direction):
  <out>/print/*.stl   files to slice and print
  <out>/print/*.step  the same parts as exact CAD
  <out>/sim/*.step    simulation copies for `validate.py predict`
                      (bar cut at the string groove, pad cut at the hole centre)

Needs CadQuery:  pip install cadquery
Run:             python tools/validation/make_specimens.py [--out docs/validation/specimens]
"""
import argparse
import math
import os

import cadquery as cq


def box(x0, x1, y0, y1, z0, z1):
    return cq.Workplane("XY").box(x1 - x0, y1 - y0, z1 - z0, centered=False) \
        .translate((x0, y0, z0))


def edges_in(shape, x, y, z):
    """Edges whose centre lies inside the given ranges."""
    sel = cq.selectors.BoxSelector((x[0], y[0], z[0]), (x[1], y[1], z[1]))
    return shape.edges(sel)


# ---------------------------------------------------------------- dogbone ---
def dogbone(thickness):
    """Length along X, centred; gauge 3 x 30, R40 arcs, 25 x 25 tabs, 6.4 holes."""
    hg, ht, R, g = 1.5, 12.5, 40.0, 15.0           # half-widths, arc radius, half-gauge
    cos_t = (R - (ht - hg)) / R
    xa = g + R * math.sqrt(1 - cos_t ** 2)          # arc end (tab start)
    xe = xa + 25.0                                  # part end
    mid = math.radians(0.5 * math.degrees(math.acos(cos_t)))
    pm = (g + R * math.sin(mid), hg + R - R * math.cos(mid))   # arc midpoint
    w = (cq.Workplane("XY").moveTo(-xe, -ht)
         .lineTo(xe, -ht).lineTo(xe, ht).lineTo(xa, ht)
         .threePointArc(pm, (g, hg)).lineTo(-g, hg)
         .threePointArc((-pm[0], pm[1]), (-xa, ht)).lineTo(-xe, ht).lineTo(-xe, -ht)
         .close())
    # lower half: mirror the arcs
    lower = (cq.Workplane("XY").moveTo(-xe, ht)
             .lineTo(xe, ht).lineTo(xe, -ht).lineTo(xa, -ht)
             .threePointArc((pm[0], -pm[1]), (g, -hg)).lineTo(-g, -hg)
             .threePointArc((-pm[0], -pm[1]), (-xa, -ht)).lineTo(-xe, -ht).lineTo(-xe, ht)
             .close())
    part = w.extrude(thickness).intersect(lower.extrude(thickness))
    for sx in (-1, 1):
        part = part.cut(cq.Workplane("XY").center(sx * (xe - 10.0), 0)
                        .circle(3.2).extrude(thickness))
    return part, 2 * xe


# -------------------------------------------------------------------- bar ---
def bar_flat(sim=False):
    """Block x -12..0, y +-15, z 0..16; 8x8 bar flush with the block bottom."""
    length = 80.0 if sim else 84.0
    part = box(-12, 0, -15, 15, 0, 16).union(box(0, length, -4, 4, 0, 8))
    part = edges_in(part, (-0.1, 0.1), (-4.1, 4.1), (0.5, 8.1)).fillet(3.0)
    if not sim:                                     # string groove at x = 80
        ring = box(79.25, 80.75, -5, 5, -1, 9).cut(box(79.25, 80.75, -3, 3, 1, 7))
        part = part.cut(ring)
    return part


def bar_standing(sim=False):
    """Same part, bar along +Z, block on the bed, flush side at x = 0 (-X)."""
    p = bar_flat(sim).rotate((0, 0, 0), (0, 1, 0), -90)   # bar -> +Z, flush side -> -X... then
    p = p.mirror("YZ")                                     # put the part at x >= 0
    bb = p.val().BoundingBox()
    return p.translate((-bb.xmin, 0, -bb.zmin))


# ------------------------------------------------------------------ twist ---
def twist(sim=False):
    top = 41.5 if sim else 43.0
    part = (box(-15, 15, -15, 15, 0, 12)
            .union(cq.Workplane("XY").workplane(offset=12).circle(4).extrude(20))
            .union(box(-8, 103, -6, 6, 32, 40))
            .union(box(97, 103, -3, 3, 40, top)))
    part = edges_in(part, (-4.1, 4.1), (-4.1, 4.1), (11.9, 12.1)).fillet(3.0)  # root
    part = edges_in(part, (-4.1, 4.1), (-4.1, 4.1), (31.9, 32.1)).fillet(1.5)  # lever end
    if not sim:
        hole = cq.Workplane("YZ").workplane(offset=96).center(0, 41.5).circle(1.0).extrude(8)
        part = part.cut(hole)
        part = part.cut(box(-0.3, 0.3, -6.5, -5.6, 32, 40))   # axis mark on the y = -6 face
    return part


# ----------------------------------------------------------------- C-ring ---
def cring():
    part = (cq.Workplane("XY").circle(17).circle(13).extrude(6)
            .cut(box(0, 20, -3, 3, -1, 7))
            .cut(box(-20, 20, 16.2, 20, -1, 7))
            .cut(box(-20, 20, -20, -16.2, -1, 7)))
    return part


# ---------------------------------------------------------------- columns ---
def column(L):
    """Flat T, 3 thick: stem x -15..L (y +-3), crossbar 8 x 80 centred on x = L."""
    part = box(-15, L, -3, 3, 0, 3).union(box(L - 4, L + 4, -40, 40, 0, 3))
    for y in (-35, 35):
        part = part.cut(cq.Workplane("XY").center(L, y).circle(1.5).extrude(3))
    return part


# --------------------------------------------------------------- fixtures ---
def frame():
    return box(0, 69, 0, 84, 0, 12).cut(box(12, 57, 12, 72, -1, 13))


def anvil():
    return box(0, 200, 0, 20, 0, 20)


PARTS = {
    # name: (builder, kind) kind: print / sim / both
    "dogbone_flat_t2":      (lambda: dogbone(2.0)[0], "both"),
    "dogbone_standing_t4":  (lambda: dogbone(4.0)[0].rotate((0, 0, 0), (0, 1, 0), -90)
                             .translate((0, 0, 0)), "both"),
    "bar_flat":             (lambda: bar_flat(), "print"),
    "bar_standing":         (lambda: bar_standing(), "print"),
    "bar_flat_sim":         (lambda: bar_flat(sim=True), "sim"),
    "bar_standing_sim":     (lambda: bar_standing(sim=True), "sim"),
    "twist":                (lambda: twist(), "print"),
    "twist_free_sim":       (lambda: twist(sim=True), "sim"),
    "cring":                (lambda: cring(), "both"),
    "column60":             (lambda: column(60.0), "print"),
    "column80":             (lambda: column(80.0), "print"),
    "column100":            (lambda: column(100.0), "print"),
    "fixture_cring_frame":  (lambda: frame(), "print"),
    "fixture_anvil_bar":    (lambda: anvil(), "print"),
}


def on_bed(part):
    bb = part.val().BoundingBox()
    return part.translate((0, 0, -bb.zmin))


def main():
    ap = argparse.ArgumentParser(description="Generate the validation test pieces.")
    ap.add_argument("--out", default="docs/validation/specimens")
    args = ap.parse_args()
    for sub in ("print", "sim"):
        os.makedirs(os.path.join(args.out, sub), exist_ok=True)
    for name, (build, kind) in PARTS.items():
        part = build()
        solid = part.val()
        if not solid.isValid():
            raise SystemExit("invalid solid: " + name)
        bb = solid.BoundingBox()
        print("%-22s %-5s %6.1f x %6.1f x %6.1f mm  vol %8.1f mm3"
              % (name, kind, bb.xlen, bb.ylen, bb.zlen, solid.Volume()))
        if kind in ("print", "both"):
            p = on_bed(part)
            cq.exporters.export(p, os.path.join(args.out, "print", name + ".stl"),
                                tolerance=0.01, angularTolerance=0.1)
            cq.exporters.export(p, os.path.join(args.out, "print", name + ".step"))
        if kind in ("sim", "both"):
            sim_name = name if name.endswith("_sim") else name + "_sim"
            cq.exporters.export(part, os.path.join(args.out, "sim", sim_name + ".step"))


if __name__ == "__main__":
    main()
