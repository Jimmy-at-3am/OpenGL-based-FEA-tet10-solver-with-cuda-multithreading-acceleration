#!/usr/bin/env python3
"""draw_setups.py -- test-setup figures for docs/validation/physical-test-plan.md

Draws one scaled schematic per test (dimensions in mm, from make_specimens.py):
the test piece (blue), supports and fixtures (grey, green labels) and loads
(red). Output: docs/validation/specimens/setup/*.png

Run: python tools/validation/draw_setups.py
"""
import math
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.patches import Circle, FancyArrowPatch, Polygon, Rectangle

OUT = "docs/validation/specimens/setup"
PART = dict(facecolor="#8fb8e8", edgecolor="#1f4e8c", lw=1.4, zorder=3)
FIX = dict(facecolor="#c9c9c9", edgecolor="#444444", lw=1.2, zorder=2)
HW = dict(facecolor="#555555", edgecolor="#222222", lw=1.0, zorder=4)
LOAD_C, SUP_C = "#c62828", "#2e7d32"


def new(title, w=11, h=8):
    fig, ax = plt.subplots(figsize=(w, h))
    ax.set_aspect("equal")
    ax.axis("off")
    ax.set_title(title, fontsize=14, fontweight="bold", loc="left")
    return fig, ax


def rect(ax, x, y, w, h, style, **kw):
    ax.add_patch(Rectangle((x, y), w, h, **{**style, **kw}))


def arrow(ax, p0, p1, color=LOAD_C, lw=2.5):
    ax.add_patch(FancyArrowPatch(p0, p1, arrowstyle="-|>", mutation_scale=22,
                                 color=color, lw=lw, zorder=6))


def note(ax, xy, text, xytext, color="black", size=9.5):
    ax.annotate(text, xy=xy, xytext=xytext, fontsize=size, color=color, zorder=7,
                arrowprops=dict(arrowstyle="-", color=color, lw=0.8),
                bbox=dict(boxstyle="round,pad=0.3", fc="white", ec=color, lw=0.6))


def dim(ax, p0, p1, text, off=(0, 0)):
    ax.annotate("", xy=p0, xytext=p1, arrowprops=dict(arrowstyle="<->", lw=0.8))
    ax.text((p0[0] + p1[0]) / 2 + off[0], (p0[1] + p1[1]) / 2 + off[1], text,
            fontsize=8.5, ha="center", va="center",
            bbox=dict(fc="white", ec="none", pad=0.5))


def weights(ax, cx, ytop, label):
    """Bucket with weights hanging from (cx, ytop)."""
    ax.plot([cx, cx], [ytop, ytop - 25], color="#333", lw=1.5, zorder=4)
    ax.add_patch(Polygon([(cx - 22, ytop - 25), (cx + 22, ytop - 25),
                          (cx + 18, ytop - 75), (cx - 18, ytop - 75)],
                         facecolor="#e0e0e0", edgecolor="#333", lw=1.2, zorder=4))
    ax.text(cx, ytop - 50, "carrier\non screw\nstand", ha="center", va="center", fontsize=8)
    arrow(ax, (cx + 32, ytop - 20), (cx + 32, ytop - 70))
    ax.text(cx + 38, ytop - 45, label, color=LOAD_C, fontsize=10, va="center",
            fontweight="bold")


def catch_box(ax, cx, y):
    rect(ax, cx - 35, y - 30, 70, 30, FIX, hatch="//")
    ax.text(cx, y - 15, "catch box\n1–2 cm below", ha="center", va="center", fontsize=7.5,
            zorder=5, bbox=dict(fc="white", ec="none", pad=0.3))


def dogbone_outline(L_total=169.9):
    hg, ht, R, g = 1.5, 15.0, 40.0, 15.0
    cos_t = (R - (ht - hg)) / R
    xa = g + R * math.sqrt(1 - cos_t ** 2)
    xe = xa + 40.0
    pts = [(-xe, -ht), (xe, -ht), (xe, ht), (xa, ht)]
    th = math.acos(cos_t)
    for i in range(1, 20):
        a = th * (1 - i / 20)
        pts.append((g + R * math.sin(a), hg + R - R * math.cos(a)))
    pts += [(g, hg), (-g, hg)]
    for i in range(1, 20):
        a = th * i / 20
        pts.append((-g - R * math.sin(a), hg + R - R * math.cos(a)))
    pts += [(-xa, ht), (-xe, ht)]
    upper = pts
    lower = [(x, -y) for x, y in upper]
    # outline = upper edge left->right, lower edge right->left
    top = [p for p in upper if p[1] > 0]
    top = sorted(top, key=lambda p: p[0])
    bot = sorted([(x, -y) for x, y in top], key=lambda p: -p[0])
    return top + bot, xe


# ------------------------------------------------------------------ tension
def fig_tension():
    fig, ax = new("Test 1 — Tension (dogbone_flat_t2 / dogbone_standing_t4)", 10, 11)
    outline, xe = dogbone_outline()
    pts = [(y, x) for x, y in outline]          # rotate: length vertical
    ax.add_patch(Polygon(pts, **PART))
    # grips (two plates seen face-on, slightly wider than the tab)
    for s in (1, -1):
        y0 = s * xe - (40 if s > 0 else -40) - (26 if s > 0 else 0)
        y_lo = xe - 40 if s > 0 else -xe - 26
        rect(ax, -27, y_lo, 54, 66, FIX, alpha=0.55)
        hole_y = s * (xe - 15)
        ax.add_patch(Circle((0, hole_y), 4.25, **HW))
        for dy in (-15, 9) if s > 0 else (-9, 15):
            for dx in (-21, 21):
                ax.add_patch(Circle((dx, hole_y + dy), 2.75, **HW))
        cy = s * (xe + 14)
        ax.add_patch(Circle((0, cy), 7, facecolor="white", edgecolor="#444", lw=1.2, zorder=4))
    # overhead bar + carabiners
    top = xe + 14
    ax.add_patch(Circle((0, top + 30), 9, **HW))
    ax.plot([-30, 30], [top + 30, top + 30], color="#222", lw=6, zorder=1)
    ax.add_patch(matplotlib.patches.Ellipse((0, top + 14), 12, 30, fill=False, ec="#222", lw=2.2, zorder=5))
    ax.add_patch(matplotlib.patches.Ellipse((0, -top - 14), 12, 30, fill=False, ec="#222", lw=2.2, zorder=5))
    weights(ax, 0, -top - 29, "F = m·g\n(pulls along axis)")
    catch_box(ax, 0, -top - 110)
    note(ax, (0, top + 30), "FIXED SUPPORT: overhead bar\n(pull-up bar / beam), carabiner =\nfree pin → self-aligning",
         (45, top + 10), SUP_C)
    note(ax, (-27, xe - 20), "Grip: 2 printed plates per end\n(fixture_dogbone_grip_plate)\nM8 bolt through tab hole +\n4 × M5 clamp bolts beside tab",
         (-140, xe - 10))
    note(ax, (1.5, 0), "Measured section:\n3 × 2 mm (flat print)\n3 × 4 mm (standing print)\nmust break here", (40, 5), "#1f4e8c")
    note(ax, (0, -top - 14), "shackle → weight carrier (see 0_loading_method)", (40, -top - 5))
    dim(ax, (-15, xe - 50), (15, xe - 50), "30", (0, -5))
    dim(ax, (-60, -xe), (-60, xe), "170", (-9, 0))
    ax.text(-140, -top - 175, "Load in steps with the carrier + screw stand: start ≈40 lb, +5 lb per step, hold 30 s.\n"
            "Record total hanging mass. Break must be in the narrow section.\n"
            "No deflection reading (stiffness comes from the bars).", fontsize=9.5)
    ax.set_xlim(-150, 165); ax.set_ylim(-top - 190, top + 50)
    return fig


# ------------------------------------------------------------------ bending
def bar_side(ax, x0, y0, layers):
    """Side view of the bar (block left, load head right), bottom at y0."""
    rect(ax, x0 - 25, y0, 25, 24, PART)
    ax.add_patch(Polygon([(x0, y0), (x0 + 64, y0), (x0 + 72, y0), (x0 + 72, y0 + 16),
                          (x0 + 64, y0 + 8), (x0 + 3, y0 + 8),
                          (x0, y0 + 11)], **PART))
    rect(ax, x0 + 72, y0, 16, 16, PART)
    ax.add_patch(Circle((x0 + 80, y0 + 8), 3.25, facecolor="white", edgecolor="#1f4e8c", zorder=4))
    ax.add_patch(Circle((x0 + 80, y0 + 8), 2.6, **HW))
    # layer lines
    if layers == "flat":
        for z in range(1, 8):
            ax.plot([x0 + 3, x0 + 64], [y0 + z, y0 + z], color="#1f4e8c", lw=0.3, zorder=4)
    else:
        for x in range(4, 64, 3):
            ax.plot([x0 + x, x0 + x], [y0, y0 + 8], color="#1f4e8c", lw=0.3, zorder=4)


def fig_bending():
    fig, axs = plt.subplots(2, 1, figsize=(11, 11))
    fig.suptitle("Test 2 — Bending, cantilever (bar_flat / bar_standing)", fontsize=14,
                 fontweight="bold", x=0.02, ha="left")
    for ax, layers, lab, load in ((axs[0], "flat", "bar_flat — printed flat: layers run along the bar (in-plane)", "≈ 45–53 N"),
                                  (axs[1], "standing", "bar_standing — printed standing: layers cross the bar (between-layer)", "≈ 20–24 N")):
        ax.set_aspect("equal"); ax.axis("off")
        ax.set_title(lab, fontsize=11, loc="left")
        # table + vise
        rect(ax, -110, -40, 105, 30, FIX)
        ax.text(-80, -25, "table", ha="center", va="center", fontsize=8, zorder=5)
        rect(ax, -45, -10, 42, 12, FIX)                     # vise body on the table
        ax.text(-24, -4, "vise", ha="center", va="center", fontsize=7.5, zorder=5)
        rect(ax, -32, 2, 34, 30, FIX, alpha=0.7)            # jaw behind the block
        bar_side(ax, 0, 2, layers)
        ax.add_patch(Rectangle((-27, 0), 29, 30, fill=False, ec=SUP_C, lw=2, ls="--", zorder=5))
        note(ax, (-12, 30), "FIXED SUPPORT: whole 25×40×24 block\nbetween the vise jaws\n(vise at the table edge, bar sticks out)", (-110, 52), SUP_C)
        note(ax, (40, 2), "flat side (no step) DOWN", (20, -35))
        note(ax, (30, 10), "measured: 8 × 8 mm bar", (5, 30), "#1f4e8c")
        # hanger + load
        ax.plot([80, 80], [6, -20], color="#333", lw=1.5, zorder=5)
        note(ax, (80, 10), "M6 bolt through load head; printed\nload yoke hangs on BOTH ends", (105, 45))
        weights(ax, 80, -20, "F = m·g ↓\n" + load)
        # indicator
        rect(ax, 76, 26, 8, 20, HW); ax.plot([80, 80], [18, 26], color="#222", lw=1.5)
        note(ax, (84, 36), "dial indicator on the head\n(or phone photo vs. ruler)", (110, 20))
        dim(ax, (0, -8), (80, -8), "80 mm (block face → load line)", (0, -5))
        ax.set_xlim(-115, 200); ax.set_ylim(-100, 80)
    fig.text(0.02, 0.01, "Elastic readings first (4 steps: ~500 g flat / ~250 g standing, read 10 s after each). "
             "Break only after predictions are committed:\nload in steps with the carrier (start ~3 kg / ~1.3 kg). Expected break: at the root fillet next to the block.",
             fontsize=9.5)
    return fig


# -------------------------------------------------------------------- twist
def fig_twist():
    fig, axs = plt.subplots(1, 2, figsize=(15, 8), gridspec_kw=dict(width_ratios=[1.25, 1]))
    fig.suptitle("Test 3 — Twisting (twist.stl): 5 PROPPED (calibration) + 5 FREE (validation)",
                 fontsize=14, fontweight="bold", x=0.02, ha="left")
    # A: top view
    ax = axs[0]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("A. Top view (looking down)", loc="left", fontsize=11)
    rect(ax, -60, -45, 40, 90, FIX); ax.text(-40, 0, "vise\njaws", ha="center", va="center", fontsize=8, zorder=5)
    rect(ax, -20, -20, 20, 40, PART)                      # block (z 0..20 horizontal)
    rect(ax, 0, -4, 20, 8, PART)                          # shaft
    rect(ax, 20, -8, 12, 118, PART)                       # lever (z 40..52 along x here)
    rect(ax, 32, 92, 12, 16, PART)                        # load block
    ax.plot([0, 60], [0, 0], "k-.", lw=0.8)
    ax.text(62, 1, "shaft axis", fontsize=8)
    ax.add_patch(Circle((38, 100), 3, facecolor="white", edgecolor="#222", lw=1.5, zorder=6))
    ax.plot([36, 40], [98, 102], color=LOAD_C, lw=2, zorder=7); ax.plot([36, 40], [102, 98], color=LOAD_C, lw=2, zorder=7)
    note(ax, (38, 100), "LOAD: M6 bolt through load block,\nload yoke on both ends, F pulls DOWN\n(into the page)", (60, 110), LOAD_C)
    note(ax, (-10, -20), "FIXED SUPPORT: 40×40×20 block\nclamped in the vise", (-60, -70), SUP_C)
    note(ax, (10, 4), "measured: Ø8 shaft\n(20 mm long)", (55, 30), "#1f4e8c")
    dim(ax, (80, 0), (80, 100), "100 mm\nlever arm", (16, 0))
    ax.add_patch(Rectangle((19, -1.5), 14, 3, facecolor=SUP_C, edgecolor="none", zorder=6, alpha=0.8))
    note(ax, (26, 0), "propped tests only:\nround rod UNDER the lever,\non the axis notch", (-10, 75), SUP_C)
    ax.set_xlim(-75, 120); ax.set_ylim(-85, 130)
    # B: end view along shaft
    ax = axs[1]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("B. End view (looking along the shaft)", loc="left", fontsize=11)
    rect(ax, -20, -20, 40, 40, PART, alpha=0.45)
    ax.text(0, -26, "block in vise (behind)", ha="center", fontsize=8)
    ax.add_patch(Circle((0, 0), 4, **PART))
    rect(ax, -10, -8, 118, 16, PART)                      # lever
    rect(ax, 92, -8, 16, 16, PART)
    ax.add_patch(Circle((100, 0), 3.25, **HW))
    ax.plot([100, 100], [-8, -40], color="#333", lw=1.5)
    weights(ax, 100, -40, "F = m·g ↓")
    ax.add_patch(Circle((0, -13), 5, facecolor=SUP_C, edgecolor="#1b5e20", lw=1.2, zorder=5))
    note(ax, (0, -13), "PROPPED: rod (Ø10) under lever at\nthe axis notch → reacts F, so the\nshaft gets PURE TORQUE T = F·100 mm", (-60, -75), SUP_C)
    note(ax, (50, 8), "FREE: no rod → torque + bending\n(this is what the solver predicts)", (20, 40))
    ax.add_patch(matplotlib.patches.Arc((0, 0), 70, 70, theta1=200, theta2=290, color=LOAD_C, lw=2.5, zorder=6))
    arrow(ax, (-31, -18), (-34, -10))      # left side moves UP -> clockwise
    ax.text(-74, 26, "T = F × 100 mm\n(clockwise: the right\nside goes down)", color=LOAD_C,
            fontsize=9.5, fontweight="bold")
    dim(ax, (0, 22), (100, 22), "100 mm", (0, 4))
    ax.set_xlim(-75, 160); ax.set_ylim(-125, 60)
    fig.text(0.02, 0.01, "Elastic: 4 steps of ~180 g, dial indicator on the load block. Break: carrier steps from ~1 kg, +100 g. "
             "Expected ≈18 N (1.8 N·m), flat break across a layer at the shaft root.", fontsize=9.5)
    return fig


# -------------------------------------------------------------------- C-ring
def ring_poly(cx, cy):
    """C-ring outline (end view), 13/17 radii, 6 mm gap on +x, pads at +-19."""
    from matplotlib.path import Path
    import numpy as np
    th = np.linspace(math.asin(3 / 17), 2 * math.pi - math.asin(3 / 17), 200)
    outer = [(cx + 17 * math.cos(t), cy + 17 * math.sin(t)) for t in th]
    ti = np.linspace(2 * math.pi - math.asin(3 / 13), math.asin(3 / 13), 200)
    inner = [(cx + 13 * math.cos(t), cy + 13 * math.sin(t)) for t in ti]
    return outer + inner


def fig_cring():
    fig, axs = plt.subplots(1, 2, figsize=(15, 9), gridspec_kw=dict(width_ratios=[1.3, 1]))
    fig.suptitle("Test 4 — Compression (cring.stl + fixture_cring_frame + fixture_anvil_bar)",
                 fontsize=14, fontweight="bold", x=0.02, ha="left")
    # A: side view along anvil
    ax = axs[0]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("A. Side view", loc="left", fontsize=11)
    ax.plot([-20, 220], [150, 150], color="#222", lw=7)
    ax.text(225, 150, "overhead bar", va="center", fontsize=8.5)
    for x in (10, 190):
        ax.plot([x, x], [150, 10], color="#8d6e63", lw=1.6)
    rect(ax, 0, 0, 200, 20, FIX)                                 # anvil
    rect(ax, 92, -38, 16, 112, FIX, alpha=0.9)                    # frame edge-on (16 thick)
    rect(ax, 95, 20, 10, 38, PART)                                # ring edge-on (10 wide) on anvil
    ax.plot([100, 100], [-38, -40], color="#333", lw=1.5)
    weights(ax, 100, -40, "F = m·g ↓")
    note(ax, (10, 80), "cords through the anvil's\nØ8 end holes", (-20, 110))
    note(ax, (100, 10), "SUPPORT: anvil bar (fixed by cords)", (120, -60), SUP_C)
    note(ax, (100, 66), "frame hangs around the anvil;\nits top bar sits on the ring", (130, 100))
    ax.set_xlim(-30, 270); ax.set_ylim(-130, 170)
    # B: view along anvil
    ax = axs[1]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("B. View along the anvil", loc="left", fontsize=11)
    rect(ax, -46, -38, 92, 112, FIX, alpha=0.6)                   # frame outer
    rect(ax, -30, -22, 60, 80, dict(facecolor="white", edgecolor="#444", lw=1.2, zorder=2))
    rect(ax, -12.5, 0, 25, 20, FIX)                               # anvil section (hung by cords)
    ax.text(0, 10, "anvil", ha="center", va="center", fontsize=7.5, zorder=5)
    cy = 20 + 19
    ax.add_patch(Polygon(ring_poly(0, cy), **PART))
    rect(ax, -8, cy + 14, 16, 5, PART); rect(ax, -8, cy - 19, 16, 5, PART)
    ax.text(0, cy + 25, "frame top bar rests on the top pad", ha="center", fontsize=8, zorder=6,
            bbox=dict(fc="white", ec="none", pad=0.3))
    note(ax, (-12, 10), "SUPPORT: anvil bar\n(bottom pad sits on it;\ncords hold it from above)", (-125, -20), SUP_C)
    note(ax, (17, cy), "6 mm gap\nto the side", (60, cy + 25))
    note(ax, (-17, cy), "expected crack:\nouter back", (-115, cy + 25), "#1f4e8c")
    ax.plot([0, 0], [-38, -70], color="#333", lw=1.5)
    weights(ax, 0, -70, "F ↓ (frame pulls\ntop pad down)")
    note(ax, (-2, -30), "strap around the frame's\nbottom bar", (-125, -75))
    ax.set_xlim(-130, 115); ax.set_ylim(-160, 95)
    fig.text(0.02, 0.01, "Elastic: 4 steps of ~1 kg; measure the anvil-to-frame gap beside the ring with calipers. "
             "Break: carrier steps from ~4.5 kg, +5 lb. Expected ≈ 10 kg (102 N).\n"
             "The frame's own mass is part of the load.", fontsize=9.5)
    return fig


# ----------------------------------------------------------------- buckling
def fig_buckling():
    fig, axs = plt.subplots(1, 2, figsize=(13, 9), gridspec_kw=dict(width_ratios=[1.3, 1]))
    fig.suptitle("Test 5 (optional) — Buckling (column60 / column80 / column100)",
                 fontsize=14, fontweight="bold", x=0.02, ha="left")
    L = 80
    ax = axs[0]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("A. Front view (L = 80 shown)", loc="left", fontsize=11)
    rect(ax, -40, -30, 80, 25, FIX); ax.text(28, -18, "vise", ha="center", fontsize=8, zorder=5)
    rect(ax, -15, -25, 30, 25, PART)
    rect(ax, -3, 0, 6, L, PART)
    rect(ax, -50, L - 7, 100, 14, PART)
    for x in (-44, 44):
        ax.add_patch(Circle((x, L), 3, facecolor="white", edgecolor="#1f4e8c", zorder=4))
        ax.plot([x, x], [L, L - 120], color="#333", lw=1.4)
        ax.add_patch(Rectangle((x - 9, L - 155), 18, 35, facecolor="#bbdefb", edgecolor="#333", zorder=4))
        arrow(ax, (x + (14 if x > 0 else -14), L - 125), (x + (14 if x > 0 else -14), L - 150))
    ax.text(0, L - 172, "two kit slotted-mass hangers (add equal masses)", ha="center", fontsize=8.5)
    ax.text(62, L - 138, "P/2 each", color=LOAD_C, fontsize=10, fontweight="bold")
    note(ax, (-10, -12), "FIXED SUPPORT: 25×30×10 block\nclamped in the vise, stem UP", (55, -45), SUP_C)
    note(ax, (3, L / 2), "measured: 6 × 3 mm stem", (20, 45), "#1f4e8c")
    dim(ax, (-20, 0), (-20, L), "L = 60 / 80 / 100", (-24, 0))
    ax.set_xlim(-135, 115); ax.set_ylim(-100, 110)
    ax = axs[1]; ax.set_aspect("equal"); ax.axis("off")
    ax.set_title("B. Side view — it buckles THIS way (out of plane)", loc="left", fontsize=11)
    rect(ax, -10, -25, 10, 25, PART)
    import numpy as np
    y = np.linspace(0, L, 50); x = 12 * (1 - np.cos(np.pi * y / (2 * L)))
    ax.fill_betweenx(y, x - 1.5, x + 1.5, color="#8fb8e8", ec="#1f4e8c", zorder=3)
    ax.plot([0, 0], [0, L], "k--", lw=0.8)
    rect(ax, 12 - 1.5, L - 7, 3, 14, PART)
    ax.plot([12, 12], [L, L - 40], color="#333", lw=1.4)
    arrow(ax, (12, L - 40), (12, L - 65))
    ax.text(16, L - 55, "P", color=LOAD_C, fontsize=12, fontweight="bold")
    dim(ax, (0, L + 12), (12, L + 12), "δ", (0, 5))
    rect(ax, 30, 0, 4, L + 10, FIX); ax.text(38, L / 2, "ruler fixed\nbehind;\nphoto each\nstep", fontsize=8.5)
    ax.set_xlim(-40, 90); ax.set_ylim(-85, 110)
    fig.text(0.02, 0.01, "Add equal kit masses in small steps (~165 / 95 / 60 g per hanger for L = 60 / 80 / 100). Photograph δ each step;\n"
             "stop when it keeps bending without more mass. verdict computes the buckling load (Southwell).", fontsize=9.5)
    return fig


# ------------------------------------------------------ shock-free loading
def _loader(ax, x0, gap, broken=False, plates=3):
    """Carrier + screw stand at x0. gap = carrier lift-off from the cup (mm)."""
    floor = 0
    rect(ax, x0 - 70, floor, 140, 30, FIX)                       # stand base
    rod_top = 120 - (gap if not broken else 0)
    cup_top = rod_top
    rect(ax, x0 - 6, 30, 12, rod_top - 30 - 30, dict(facecolor="#9e9e9e", edgecolor="#555", lw=1, zorder=2))
    rect(ax, x0 - 35, 70, 70, 16, HW)                             # knob
    rect(ax, x0 - 32, cup_top - 30, 64, 30, FIX)                  # cup
    car_bottom = cup_top + (gap if not broken else 0)
    # carrier: nut+washer, tray, boss, plates, rod, eye nut
    rect(ax, x0 - 25, car_bottom, 50, 3, HW)
    rect(ax, x0 - 115, car_bottom + 3, 230, 14, FIX, alpha=0.95)
    for i in range(plates):
        rect(ax, x0 - 100, car_bottom + 17 + i * 16, 200, 14, dict(facecolor="#424242", edgecolor="#111", lw=1, zorder=3))
    top = car_bottom + 17 + plates * 16 + 30
    rect(ax, x0 - 5, car_bottom, 10, top - car_bottom, dict(facecolor="#bdbdbd", edgecolor="#555", lw=1, zorder=4))
    ax.add_patch(Circle((x0, top + 8), 8, fill=False, ec="#222", lw=3, zorder=5))   # eye nut
    return car_bottom, top + 16


def fig_loading():
    fig, axs = plt.subplots(1, 3, figsize=(16, 9))
    fig.suptitle("Shock-free loading — the specimen only ever feels the weight through the screw stand, never your hands",
                 fontsize=13.5, fontweight="bold", x=0.02, ha="left")
    steps = [("1. Stand UP: carrier rests on the cup.\nAdd plates by hand — the stand carries them,\nthe specimen carries nothing.", 0, False),
             ("2. Turn the knob DOWN slowly (≈1.75 mm/turn)\nuntil a 3–5 mm gap shows under the carrier.\nNow the specimen holds the full load. Hold 30 s.", 5, False),
             ("3a. Survived → turn the stand back UP until it\ncarries the load, add the next plate, repeat 2.\n3b. Broke → carrier drops only the 3–5 mm gap\nonto the cup. Record that load.", 5, True)]
    for ax, (txt, gap, broken) in zip(axs, steps):
        ax.set_aspect("equal"); ax.axis("off")
        cb, eye_top = _loader(ax, 0, gap, broken)
        # shackle + specimen above
        spec_bot = eye_top + 14
        ax.add_patch(matplotlib.patches.Ellipse((0, eye_top + 7), 14, 22, fill=False, ec="#222", lw=2.4, zorder=5))
        if broken:
            ax.add_patch(Polygon([(-6, spec_bot), (6, spec_bot), (3, spec_bot + 40), (-3, spec_bot + 40)], **PART))
            ax.add_patch(Polygon([(-3, spec_bot + 55), (3, spec_bot + 55), (6, spec_bot + 95), (-6, spec_bot + 95)], **PART))
            ax.text(14, spec_bot + 46, "break", color=LOAD_C, fontsize=10, fontweight="bold")
        else:
            ax.add_patch(Polygon([(-6, spec_bot), (6, spec_bot), (3, spec_bot + 40), (3, spec_bot + 55),
                                  (6, spec_bot + 95), (-6, spec_bot + 95), (-3, spec_bot + 55), (-3, spec_bot + 40)], **PART))
        ax.plot([-40, 40], [spec_bot + 100, spec_bot + 100], color="#222", lw=6)
        ax.text(0, spec_bot + 108, "fixed support", ha="center", color=SUP_C, fontsize=9)
        ax.text(-120, -55, txt, fontsize=9.5, va="top")
        if gap and not broken:
            dim(ax, (60, cb - gap), (60, cb), "gap", (14, 0))
            arrow(ax, (95, cb + 60), (95, cb + 10))
            ax.text(100, cb + 40, "W", color=LOAD_C, fontsize=12, fontweight="bold")
        ax.set_xlim(-125, 125); ax.set_ylim(-140, spec_bot + 125)
    a = axs[0]
    note(a, (35, 78), "hand wheel (turn to\nraise / lower)", (40, 20))
    note(a, (0, 105), "cup: catches the carrier", (-120, 150))
    note(a, (100, 140), "plates slide over the\nboss onto the tray", (20, 215))
    note(a, (0, 250), "steel shackle + M10 eye nut\n(no nylon string)", (-120, 290))
    fig.text(0.02, 0.01, "Load path is steel: specimen → shackle → M10 eye nut → M10 rod → fender washer + nut under the tray. "
             "Printed parts only carry, centre and catch the plates.", fontsize=9.5)
    return fig


def main():
    os.makedirs(OUT, exist_ok=True)
    figs = {"0_loading_method": fig_loading, "1_tension": fig_tension, "2_bending": fig_bending, "3_twist": fig_twist,
            "4_compression_cring": fig_cring, "5_buckling": fig_buckling}
    for name, fn in figs.items():
        fig = fn()
        fig.savefig(os.path.join(OUT, name + ".png"), dpi=110, bbox_inches="tight")
        plt.close(fig)
        print("wrote", os.path.join(OUT, name + ".png"))


if __name__ == "__main__":
    main()
