#!/usr/bin/env python3
"""make_specimens.py -- generate the test pieces of docs/validation/physical-test-plan.md

Writes, in millimetres and in print orientation (Z = build direction):
  <out>/print/*.stl   files to slice and print
  <out>/print/*.step  the same parts as exact CAD
  <out>/sim/*.step    simulation copies for `validate.py predict`
                      (load heads cut at the line where the load acts)

The measured sections (dogbone gauge, 8 x 8 bar, 8 mm shaft, ring section) are
small on purpose; everything that is clamped or loaded is oversized so it can be
gripped and hung from with ordinary hardware (vise, M8/M6 bolts, carabiners).

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
    """Length along X, centred. Gauge 3 x 30, R40 arcs, 30 x 40 tabs, M8 holes."""
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


def grip_plate():
    """Clamp plate for a dogbone tab; print 4 (2 per end). Tab enters from x=0,
    tab end at x=40; M8 through the tab hole at x=25; M5 side bolts beside the
    tab; a 14 mm hole for a carabiner or shackle beyond the tab."""
    p = box(0, 66, -27, 27, 0, 10)
    p = p.cut(hole_z(25, 0, 8.5))
    for x in (10, 34):
        for y in (-21, 21):
            p = p.cut(hole_z(x, y, 5.5))
    p = p.cut(hole_z(54, 0, 14))
    return p.edges("|Z").fillet(4)


# -------------------------------------------------------------------- bar ---
BLOCK_L, BLOCK_W, BLOCK_H = 25.0, 40.0, 24.0


def bar_flat(sim=False):
    """Block x -25..0, y +-20, z 0..24 (vise grip); 8 x 8 bar flush with the
    block bottom, span 80 mm to the load line; 16 mm load head at the tip with
    an M6 cross hole on the load line (x = 80)."""
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
    reaching x = 108; load block on the lever tip with an M6 hole along X at
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


# ------------------------------------------------- shock-free loading kit ---
# The load path is steel: specimen -> shackle -> M10 eye nut -> M10 threaded rod
# -> 50 mm fender washer + nut under the tray. The printed parts carry the
# plates (tray), guide them (boss / sleeve), give you handles, and hold the
# carrier on a screw stand while you add weight, so your hands never load the
# specimen.

def carrier_tray():
    """150 mm tray, 14 thick, with two hand loops and a 25 mm boss that standard
    1-inch plates slide over; M10 rod through the centre."""
    tray = cq.Workplane("XY").circle(75).extrude(14)
    for sx in (-1, 1):
        loop = box(min(sx * 60, sx * 115), max(sx * 60, sx * 115), -55, 55, 0, 14)
        loop = loop.cut(box(min(sx * 78, sx * 103), max(sx * 78, sx * 103), -42, 42, -1, 15))
        tray = tray.union(loop)
    tray = tray.union(cq.Workplane("XY").workplane(offset=14).circle(12.5).extrude(40))
    tray = tray.cut(hole_z(0, 0, 10.6))
    return tray


def plate_sleeve_olympic():
    """Slips over the 25 mm boss so 2-inch (Olympic) plates sit centred."""
    return cq.Workplane("XY").circle(25.0).circle(12.9).extrude(38)


def stand_base():
    """140 x 140 x 30 base with a captive M12 nut (hex pocket from the top)."""
    b = box(-70, 70, -70, 70, 0, 30).edges("|Z").fillet(8)
    b = b.cut(hole_z(0, 0, 13.0))
    hexp = cq.Workplane("XY").workplane(offset=18).polygon(6, 19.6 / math.cos(math.pi / 6)) \
        .extrude(13)
    return b.cut(hexp)


def stand_knob():
    """70 mm hand wheel; an M12 nut sits in the hex pocket (lock it with a
    second nut) so turning the wheel turns the threaded rod."""
    k = cq.Workplane("XY").circle(35).extrude(16)
    for i in range(10):
        a = 2 * math.pi * i / 10
        k = k.cut(cq.Workplane("XY").center(37 * math.cos(a), 37 * math.sin(a))
                  .circle(5).extrude(16))
    k = k.cut(hole_z(0, 0, 13.0))
    hexp = cq.Workplane("XY").workplane(offset=6).polygon(6, 19.6 / math.cos(math.pi / 6)) \
        .extrude(11)
    return k.cut(hexp)


def stand_cup():
    """Sits on top of the threaded rod; the carrier's bottom nut drops into the
    30 mm recess and the fender washer rests on the 64 mm face."""
    c = cq.Workplane("XY").circle(32).extrude(30)
    c = c.cut(cq.Workplane("XY").workplane(offset=12).circle(15.5).extrude(19))   # nut recess
    c = c.cut(cq.Workplane("XY").workplane(offset=-1).circle(6.4).extrude(11))    # rod socket
    return c


def load_yoke():
    """U-yoke that hangs from both ends of the M6 rod through a bar or twist
    load head (cradles open upward), with a 10.5 mm eye for the shackle."""
    y = (box(-19, -11, 0, 70, 0, 12).union(box(11, 19, 0, 70, 0, 12))   # arms
         .union(box(-19, 19, -16, 0, 0, 12)))                          # crossbar
    for x in (-15, 15):
        y = y.cut(box(x - 3.4, x + 3.4, 62, 71, -1, 13))
        y = y.cut(cq.Workplane("XY").center(x, 62).circle(3.4).extrude(12))
    y = y.cut(hole_z(0, -8, 10.5, -1, 13))
    return y


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
    "fixture_dogbone_grip_plate": (lambda: grip_plate(), "print"),
    "fixture_cring_frame":  (lambda: frame(), "print"),
    "fixture_anvil_bar":    (lambda: anvil(), "print"),
    "loader_carrier_tray":  (lambda: carrier_tray(), "print"),
    "loader_plate_sleeve_olympic": (lambda: plate_sleeve_olympic(), "print"),
    "loader_stand_base":    (lambda: stand_base(), "print"),
    "loader_stand_knob":    (lambda: stand_knob(), "print"),
    "loader_stand_cup":     (lambda: stand_cup(), "print"),
    "loader_load_yoke":     (lambda: load_yoke(), "print"),
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
