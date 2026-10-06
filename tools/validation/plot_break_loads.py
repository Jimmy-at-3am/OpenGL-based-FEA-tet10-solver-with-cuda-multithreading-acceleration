#!/usr/bin/env python3
"""plot_break_loads.py -- chart of the expected breaking load of every test piece.

Values come from validate.py's closed-form estimates for the given .mat, so the
chart can be redrawn after calibration:
  python tools/validation/plot_break_loads.py --mat pla_mine.mat
Default output: docs/validation/specimens/break_loads.png
"""
import argparse
import os
import sys

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import validate as v  # noqa: E402

BAR, SURFACE, INK, INK2, GRID = "#2a78d6", "#fcfcfb", "#0b0b0b", "#52514e", "#e4e3df"

LABELS = {
    "tension_flat":     ("Tension, flat dogbone", "dogbone_flat_t2", "snaps in the 3 mm neck"),
    "tension_standing": ("Tension, standing dogbone", "dogbone_standing_t4", "splits along a layer"),
    "cring":            ("Compression, C-ring", "cring", "cracks on the outer back"),
    "bar_flat":         ("Bending, flat bar", "bar_flat", "breaks at the root fillet"),
    "bar_standing":     ("Bending, standing bar", "bar_standing", "splits along a layer at the root"),
    "twist_free":       ("Twisting (propped or free)", "twist", "shears along a layer at the shaft root"),
    "column60":         ("Buckling, column L60", "column60", "bends sideways, does not shatter"),
    "column80":         ("Buckling, column L80", "column80", "bends sideways, does not shatter"),
    "column100":        ("Buckling, column L100", "column100", "bends sideways, does not shatter"),
}


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--mat", default="assets/materials/pla.mat")
    ap.add_argument("--out", default="docs/validation/specimens/break_loads.png")
    args = ap.parse_args()
    p, _ = v.props(v.load_mat(args.mat))
    est = {t: F for t, F, _, _ in v.estimates(p) if F is not None}
    rows = [(k, est[k]) for k in LABELS if k in est]
    rows.sort(key=lambda kv: kv[1])

    fig, ax = plt.subplots(figsize=(13, 7.2))
    fig.patch.set_facecolor(SURFACE)
    ax.set_facecolor(SURFACE)
    ys = range(len(rows))
    for y, (k, F) in zip(ys, rows):
        ax.barh(y, F, height=0.62, color=BAR, zorder=3)
        kg, lb = F / v.G, F / v.G / v.LB_KG
        text = "%.0f N  ·  %.1f kg  ·  %.1f lb" % (F, kg, lb)
        if k == "twist_free":
            text += "  →  torque %.2f N·m" % (F * v.TW_A / 1000)
        ax.text(F + 4, y + 0.08, text, va="center", fontsize=10.5, color=INK, fontweight="bold")
        sub = LABELS[k][2]
        if k.startswith("bar_"):          # root fillet lowers the beam estimate 10-20 %
            sub += "  (likely %.0f–%.0f N because of the fillet)" % (0.8 * F, 0.9 * F)
        ax.text(F + 4, y - 0.24, sub, va="center", fontsize=8.5, color=INK2)
    ax.set_yticks(list(ys))
    ax.set_yticklabels(["%s\n%s.stl" % (LABELS[k][0], LABELS[k][1]) for k, _ in rows],
                       fontsize=9.5, color=INK)
    ax.set_xlim(0, max(F for _, F in rows) * 1.55)
    ax.set_xlabel("Expected load at break (hanging weight, newtons)", color=INK2, fontsize=10)
    ax.grid(axis="x", color=GRID, lw=0.8, zorder=0)
    for s in ("top", "right", "left"):
        ax.spines[s].set_visible(False)
    ax.spines["bottom"].set_color(GRID)
    ax.tick_params(colors=INK2, length=0)
    mat_name = os.path.basename(args.mat)
    ax.set_title("Where each test piece is expected to break", loc="left", fontsize=15,
                 fontweight="bold", color=INK, pad=26)
    ax.text(0, 1.015, "Closed-form estimates from %s. Real values come from your prints; "
            "redraw with your calibrated .mat." % mat_name,
            transform=ax.transAxes, fontsize=9, color=INK2)
    fig.tight_layout()
    fig.savefig(args.out, dpi=110, facecolor=SURFACE)
    print("wrote", args.out)


if __name__ == "__main__":
    main()
