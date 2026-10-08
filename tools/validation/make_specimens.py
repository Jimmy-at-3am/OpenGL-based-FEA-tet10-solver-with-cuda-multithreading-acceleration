#!/usr/bin/env python3
"""make_specimens.py -- generate the test pieces of docs/validation/physical-test-plan.md

Writes, in millimetres and in print orientation (Z = build direction):
  <out>/print/*.stl   files to slice and print
  <out>/print/*.step  the same parts as exact CAD
  <out>/sim/*.step    simulation copies for `validate.py predict`
                      (load heads cut at the line where the load acts)

The measured sections (dogbone gauge, 8 x 8 bar, 8 mm shaft, ring section) are
small on purpose; everything that is clamped or loaded is oversized so it can be
gripped and hung from with a vise, nylon cord and the printed PLA loading kit.

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


def hole_y(x, z, d, y0=-60, y1=60):
    """Cylinder along Y (for cutting)."""
    return cq.Workplane("XZ").workplane(offset=-y1).center(x, z).circle(d / 2) \
        .extrude(y1 - y0)


def hole_x(y, z, d, x0, x1):
    return cq.Workplane("YZ").workplane(offset=x0).center(y, z).circle(d / 2) \
        .extrude(x1 - x0)


def hole_z(x, y, d, z0=-1, z1=200):
    return cq.Workplane("XY").workplane(offset=z0).center(x, y).circle(d / 2) \
        .extrude(z1 - z0)


# ---------------------------------------------------------------- dogbone ---
TAB_W, TAB_L, HOLE_D, HOLE_FROM_END = 30.0, 40.0, 8.5, 15.0


def dogbone(thickness):
    """Length along X, centred. Gauge 3 x 30, R40 arcs, 30 x 40 tabs, holes for the 8 mm PLA pin."""
    hg, ht, R, g = 1.5, TAB_W / 2, 40.0, 15.0
    cos_t = (R - (ht - hg)) / R
    xa = g + R * math.sqrt(1 - cos_t ** 2)
    xe = xa + TAB_L
    mid = 0.5 * math.acos(cos_t)
    pm = (g + R * math.sin(mid), hg + R - R * math.cos(mid))

    def half(sign):
        s = sign
        return (cq.Workplane("XY").moveTo(-xe, -s * ht)
                .lineTo(xe, -s * ht).lineTo(xe, s * ht).lineTo(xa, s * ht)
                .threePointArc((pm[0], s * pm[1]), (g, s * hg)).lineTo(-g, s * hg)
                .threePointArc((-pm[0], s * pm[1]), (-xa, s * ht))
                .lineTo(-xe, s * ht).close().extrude(thickness))

    part = half(1).intersect(half(-1))
    for sx in (-1, 1):
        part = part.cut(hole_z(sx * (xe - HOLE_FROM_END), 0, HOLE_D))
    return part


# -------------------------------------------------------------------- bar ---
BLOCK_L, BLOCK_W, BLOCK_H = 25.0, 40.0, 24.0


def bar_flat(sim=False):
    """Block x -25..0, y +-20, z 0..24 (vise grip); 8 x 8 bar flush with the
    block bottom, span 80 mm to the load line; 16 mm load head at the tip with
    a 6.5 mm cross hole for the 6 mm PLA pin on the load line (x = 80)."""
    end = 80.0 if sim else 88.0
    part = box(-BLOCK_L, 0, -BLOCK_W / 2, BLOCK_W / 2, 0, BLOCK_H).union(
        box(0, 64.5, -4, 4, 0, 8))
    taper = (cq.Workplane("YZ").workplane(offset=64).center(0, 4).rect(8, 8)
             .workplane(offset=8).center(0, 4).rect(16, 16).loft())
    part = part.union(taper).union(box(72, end, -8, 8, 0, 16))
    part = edges_in(part, (-0.1, 0.1), (-4.1, 4.1), (0.5, 8.1)).fillet(3.0)
    if not sim:
        part = part.cut(hole_y(80, 8, 6.5))
    return part


def bar_standing(sim=False):
    """Same part, bar along +Z, block on the bed, flush side at x = 0 (-X)."""
    p = bar_flat(sim).rotate((0, 0, 0), (0, 1, 0), -90).mirror("YZ")
    bb = p.val().BoundingBox()
    return p.translate((-bb.xmin, 0, -bb.zmin))


# ------------------------------------------------------------------ twist ---
def twist(sim=False):
    """Block x,y +-20, z 0..20 (vise grip); 8 mm shaft z 20..40; lever 16 x 12
    reaching x = 108; load block on the lever tip with a 6.5 mm hole (6 mm PLA pin) along X at
    x = 100 (lever arm), z = 58."""
    top = 58.0 if sim else 64.0
    part = (box(-20, 20, -20, 20, 0, 20)
            .union(cq.Workplane("XY").workplane(offset=20).circle(4).extrude(20))
            .union(box(-10, 108, -8, 8, 40, 52))
            .union(box(92, 108, -8, 8, 52, top)))
    part = edges_in(part, (-4.1, 4.1), (-4.1, 4.1), (19.9, 20.1)).fillet(3.0)
    part = edges_in(part, (-4.1, 4.1), (-4.1, 4.1), (39.9, 40.1)).fillet(3.0)
    if not sim:
        part = part.cut(hole_x(0, 58, 6.5, 90, 110))
        part = part.cut(box(-0.4, 0.4, -8.5, -7.4, 40, 52))   # shaft-axis mark, y = -8 face
    return part


# ----------------------------------------------------------------- C-ring ---
def cring():
    """R13/R17 ring, 10 wide (z), 6 mm gap at +X, 16 x 10 flat load pads at
    y = +-19 for the anvil and the frame."""
    ring = cq.Workplane("XY").circle(17).circle(13).extrude(10)
    pads = box(-8, 8, 14, 19, 0, 10).union(box(-8, 8, -19, -14, 0, 10))
    part = ring.union(pads.cut(cq.Workplane("XY").circle(13).extrude(10)))
    return part.cut(box(0, 25, -3, 3, -1, 11))


# ---------------------------------------------------------------- columns ---
def column(L):
    """Flat T, 3 thick: 25 x 30 x 10 clamp block, 6 x 3 stem of free length L
    (block face to crossbar centreline), 14 x 100 crossbar with 6 mm holes."""
    part = (box(-25, 0, -15, 15, 0, 10).union(box(0, L, -3, 3, 0, 3))
            .union(box(L - 7, L + 7, -50, 50, 0, 3)))
    for y in (-44, 44):
        part = part.cut(hole_z(L, y, 6.0))
    return part


# --------------------------------------------------------------- fixtures ---
def frame():
    """C-ring frame: 60 x 80 opening, 16 x 16 bars."""
    return box(0, 92, 0, 112, 0, 16).cut(box(16, 76, 16, 96, -1, 17)) \
        .edges("|Z").fillet(2)


def anvil():
    """Anvil bar 200 x 25 x 20 with 8 mm cord holes near the ends."""
    p = box(0, 200, 0, 25, 0, 20)
    for x in (10, 190):
        p = p.cut(hole_y(x, 10, 8.0))
    return p


# ------------------------------------------------- all-PLA loading kit ---
# No steel and no screws. Load is lowered onto the specimen by a pair of wedges
# (compression only), and every connector runs >= 10x below the specimen's
# stress: the specimen breaks because its measured section is tiny, not
# because PLA is weak.

def grip_clevis(slot):
    """Dogbone grip. U-block (extruded in Z, so the pull along X is in the layer
    plane): the tab slides into the slot until it touches the slot bottom, an
    8 mm PLA pin goes through the tab hole, and the cord ties through the
    12 mm eye. slot = tab thickness + 0.4."""
    c = box(0, 62, -12, 12, 0, 40).cut(box(-1, 32, -slot / 2, slot / 2, -1, 41))
    c = c.cut(hole_y(17, 20, 8.6, -13, 13))      # pin, 15 mm from the slot bottom
    c = c.cut(hole_y(47, 20, 12.0, -13, 13))     # cord eye
    return c


def pin(d, length, head_d):
    """Printed pin, lying on the bed (flat 0.8 mm cut underneath) so its fibres
    run along it; the head stops it sliding through."""
    p = (cq.Workplane("YZ").circle(d / 2).extrude(length)
         .union(cq.Workplane("YZ").workplane(offset=-3).circle(head_d / 2).extrude(3)))
    return p.cut(box(-5, length + 5, -head_d, head_d, -head_d, -d / 2 + 0.8))


def load_yoke():
    """U-yoke that hangs on both ends of the 6 mm PLA pin through a bar or twist
    load head (cradles open upward), with a 10.5 mm eye for the cord."""
    y = (box(-19, -11, 0, 70, 0, 12).union(box(11, 19, 0, 70, 0, 12))
         .union(box(-19, 19, -16, 0, 0, 12)))
    for x in (-15, 15):
        y = y.cut(box(x - 3.4, x + 3.4, 62, 71, -1, 13))
        y = y.cut(cq.Workplane("XY").center(x, 62).circle(3.4).extrude(12))
    return y.cut(hole_z(0, -8, 10.5, -1, 13))


SLOPE = 8.0          # wedge drop: 1 mm per 8 mm of pull
THIN = 12.0          # bottom-wedge thin end; the top wedge's lowest edge sits this high
PLATFORM = 52.0      # top-wedge platform height above the base plate
CH_X = 56.0          # toggle-bar channel centre, kept toward the thick end so the
                     # load stays over the contact patch at full pull
MAX_PULL = 64.0      # stop tabs hit the base posts here: 8 mm of drop


def _slope_z(x):
    """Bottom-wedge top surface: thick end (29.5) at x = 0 near the pull lug,
    thin end (12) at x = 140."""
    return THIN + (140 - x) / SLOPE


def wedge_lower():
    """Bottom wedge, 140 x 50: top falls 1:8 from the pull end (x = 0) to
    x = 140, with a 3 mm alignment rib along the middle. Pulling it toward you
    (-X) brings its thin part under the top wedge, so the platform LOWERS.
    Two stop tabs hit the base posts after MAX_PULL mm, so it cannot be pulled
    far enough for the top wedge to tip or touch the base."""
    w = (cq.Workplane("XZ").polyline([(0, 0), (140, 0), (140, _slope_z(140)), (0, _slope_z(0))])
         .close().extrude(25, both=True))
    rib = (cq.Workplane("XZ").polyline([(6, _slope_z(6) - 1), (134, _slope_z(134) - 1),
                                         (134, _slope_z(134) + 3), (6, _slope_z(6) + 3)])
           .close().extrude(4, both=True))
    lug = box(-36, 1, -25, 25, 0, 9).cut(box(-28, -8, -17, 17, -1, 10))
    w = w.union(rib).union(lug)
    for s in (1, -1):
        w = w.union(box(MAX_PULL, MAX_PULL + 8, *sorted((s * 24, s * 40)), 0, 8))
    return w


def wedge_upper():
    """Top wedge, 140 x 88 (overhangs the bottom wedge 19 mm each side, so its
    end faces bear on the base posts beside the bottom wedge), underside on
    the 1:8 slope with a groove for the rib, flat platform at PLATFORM, and an
    18-deep channel across it at CH_X for the toggle bar. Printed upside down
    (platform on the bed)."""
    w = (cq.Workplane("XZ").polyline([(0, _slope_z(0)), (140, _slope_z(140)), (140, PLATFORM), (0, PLATFORM)])
         .close().extrude(44, both=True))
    groove = (cq.Workplane("XZ").polyline([(-1, _slope_z(-1) - 1), (141, _slope_z(141) - 1),
                                            (141, _slope_z(141) + 3.6), (-1, _slope_z(-1) + 3.6)])
              .close().extrude(4.6, both=True))
    w = w.cut(groove)
    return w.cut(box(CH_X - 12, CH_X + 12, -50, 50, PLATFORM - 18, PLATFORM + 1))


def wedge_base():
    """Base plate the bottom wedge slides on (4 mm, 168 x 88). Two posts at the
    pull end (beside the 54 mm lane for the bottom wedge and its lug) and a
    wall at the far end hold the top wedge in X, so it can only move up and
    down; the wall is also the push-in stop for the bottom wedge. The full
    load presses the base onto the floor, so it does not walk."""
    b = box(-14.5, 154.5, -44, 44, 0, 4)
    for s in (1, -1):
        b = b.union(box(-14.5, -0.5, *sorted((s * 27, s * 44)), 4, 44))
    return b.union(box(140.5, 154.5, -44, 44, 4, 34))


def toggle_bar():
    """Goes under the plate stack, in the platform channel; the cord loops
    around its middle (seat notch) and runs up through the plate holes."""
    t = box(-55, 55, -10, 10, 0, 16)
    return t.cut(box(-4, 4, -11, 11, -1, 2))


PARTS = {
    "dogbone_flat_t2":      (lambda: dogbone(2.0), "both"),
    "dogbone_standing_t4":  (lambda: dogbone(4.0).rotate((0, 0, 0), (0, 1, 0), -90), "both"),
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
    "loader_grip_clevis_flat_t2":     (lambda: grip_clevis(2.4), "print"),
    "loader_grip_clevis_standing_t4": (lambda: grip_clevis(4.4), "print"),
    "loader_pin_d8":        (lambda: pin(8.0, 28.0, 14.0), "print"),
    "loader_pin_d6":        (lambda: pin(6.0, 44.0, 11.0), "print"),
    "fixture_cring_frame":  (lambda: frame(), "print"),
    "fixture_anvil_bar":    (lambda: anvil(), "print"),
    "loader_load_yoke":     (lambda: load_yoke(), "print"),
    "loader_wedge_lower":   (lambda: wedge_lower(), "print"),
    "loader_wedge_upper":   (lambda: wedge_upper().rotate((0, 0, 0), (1, 0, 0), 180), "print"),
    "loader_toggle_bar":    (lambda: toggle_bar(), "print"),
    "loader_wedge_base":    (lambda: wedge_base(), "print"),
}


def on_bed(part):
    bb = part.val().BoundingBox()
    return part.translate((0, 0, -bb.zmin))


def main():
    ap = argparse.ArgumentParser(description="Generate the validation test pieces.")
    ap.add_argument("--out", default="docs/validation/specimens")
    args = ap.parse_args()
    for sub in ("print", "sim"):
        d = os.path.join(args.out, sub)
        os.makedirs(d, exist_ok=True)
        for f in os.listdir(d):                       # drop files from older layouts
            if f.endswith((".stl", ".step")):
                os.remove(os.path.join(d, f))
    for name, (build, kind) in PARTS.items():
        part = build()
        solid = part.val()
        if not solid.isValid() or len(part.solids().vals()) != 1:
            raise SystemExit("invalid or multi-body solid: " + name)
        bb = solid.BoundingBox()
        print("%-28s %-5s %6.1f x %6.1f x %6.1f mm  vol %8.1f mm3"
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
