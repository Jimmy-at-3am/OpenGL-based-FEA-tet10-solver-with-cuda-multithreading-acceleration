"""Vector illustrations for the PolyFEA deck (no text inside; labels are HTML)."""
import math, random, os, sys

OUT = sys.argv[1]
os.makedirs(OUT, exist_ok=True)

BLUE = "#0078D4"; MAG = "#E3008C"; INK = "#1B1B1F"
OR_TOP, OR_SIDE, OR_FRONT = "#FBD8A6", "#F2B36B", "#E89E4F"
BL_TOP, BL_SIDE, BL_FRONT = "#CFE4F8", "#9CC6EE", "#7FB2E5"


def write(name, w, h, body, defs=""):
    s = (f'<svg xmlns="http://www.w3.org/2000/svg" width="{w}" height="{h}" viewBox="0 0 {w} {h}">'
         f'<defs>{defs}</defs>{body}</svg>')
    with open(os.path.join(OUT, name), "w") as f:
        f.write(s)
    print(name, len(s))


def fmt(pts):
    return " ".join(f"{x:.1f},{y:.1f}" for x, y in pts)

# ---------------------------------------------------------------- title band
def title_band():
    W, H, n = 1920, 104, 13
    lh = H / n
    defs = ('<linearGradient id="sweep" x1="0" y1="0" x2="1" y2="0">'
            '<stop offset="0" stop-color="#004E8C"/><stop offset="0.22" stop-color="#0063B1"/>'
            '<stop offset="0.5" stop-color="#0078D4"/><stop offset="0.8" stop-color="#1F86DA"/>'
            '<stop offset="1" stop-color="#5AAEEF"/></linearGradient>'
            '<linearGradient id="bead" x1="0" y1="0" x2="0" y2="1">'
            '<stop offset="0" stop-color="#FFFFFF" stop-opacity="0.34"/>'
            '<stop offset="0.38" stop-color="#FFFFFF" stop-opacity="0.04"/>'
            '<stop offset="0.7" stop-color="#000000" stop-opacity="0.04"/>'
            '<stop offset="1" stop-color="#001B33" stop-opacity="0.38"/></linearGradient>'
            '<linearGradient id="hot" x1="0" y1="0" x2="1" y2="0">'
            '<stop offset="0" stop-color="#E3008C" stop-opacity="0"/>'
            '<stop offset="1" stop-color="#E3008C" stop-opacity="1"/></linearGradient>')
    random.seed(7)
    body = []
    for i in range(n):
        y = i * lh
        # staircase right edge: printed layers end at slightly different x (the sweep front)
        end = W - (n - 1 - i) * 9 - random.choice([0, 6, 12, 18])
        if i == 0:
            end = W - 230  # top layer still being laid down
        body.append(f'<rect x="-6" y="{y:.2f}" width="{end + 6:.1f}" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#sweep)"/>')
        body.append(f'<rect x="-6" y="{y:.2f}" width="{end + 6:.1f}" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#bead)"/>')
        # faint seam ticks where the nozzle changed lines
        for sx in range(140 + 37 * i % 160, int(end) - 60, 260 + (i * 23) % 90):
            body.append(f'<rect x="{sx}" y="{y + 1.2:.2f}" width="1.4" height="{lh - 2.4:.2f}" fill="#FFFFFF" opacity="0.10"/>')
        if i == 0:
            body.append(f'<rect x="{end - 120:.1f}" y="{y:.2f}" width="126" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#hot)" opacity="0.9"/>')
    write("title_band.svg", W, H, "".join(body), defs)

# ---------------------------------------------------------------- cover texture
def cover_block():
    W, H, n = 1920, 360, 30
    lh = H / n
    defs = ('<linearGradient id="sw" x1="0" y1="0" x2="1" y2="0">'
            '<stop offset="0" stop-color="#004E8C"/><stop offset="0.55" stop-color="#0078D4"/>'
            '<stop offset="1" stop-color="#7CC0F4"/></linearGradient>'
            '<linearGradient id="bd" x1="0" y1="0" x2="0" y2="1">'
            '<stop offset="0" stop-color="#FFFFFF" stop-opacity="0.34"/>'
            '<stop offset="0.4" stop-color="#FFFFFF" stop-opacity="0.03"/>'
            '<stop offset="1" stop-color="#001B33" stop-opacity="0.36"/></linearGradient>'
            '<linearGradient id="hot" x1="0" y1="0" x2="1" y2="0">'
            '<stop offset="0" stop-color="#E3008C" stop-opacity="0"/>'
            '<stop offset="1" stop-color="#E3008C" stop-opacity="1"/></linearGradient>')
    body = []
    random.seed(3)
    for i in range(n):
        y = i * lh
        end = W - (n - 1 - i) * 14 - random.choice([0, 8, 16])
        if i == 0:
            end = W - 520
        body.append(f'<rect x="-8" y="{y:.2f}" width="{end + 8:.1f}" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#sw)"/>')
        body.append(f'<rect x="-8" y="{y:.2f}" width="{end + 8:.1f}" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#bd)"/>')
        if i == 0:
            body.append(f'<rect x="{end - 200:.1f}" y="{y:.2f}" width="206" height="{lh:.2f}" rx="{lh/2:.2f}" fill="url(#hot)"/>')
    write("cover_block.svg", W, H, "".join(body), defs)

# ---------------------------------------------------------------- isometric helpers
C30, S30 = math.cos(math.radians(30)), math.sin(math.radians(30))


def iso(x, y, z, ox, oy, s):
    return (ox + (x - y) * C30 * s, oy + (x + y) * S30 * s - z * s)


def poly(pts, fill, stroke=INK, sw=1.6, op=1.0, extra=""):
    return f'<polygon points="{fmt(pts)}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}" stroke-linejoin="round" fill-opacity="{op}" {extra}/>'


def line(a, b, stroke=INK, sw=1.6, dash=None, op=1.0):
    d = f' stroke-dasharray="{dash}"' if dash else ''
    return f'<line x1="{a[0]:.1f}" y1="{a[1]:.1f}" x2="{b[0]:.1f}" y2="{b[1]:.1f}" stroke="{stroke}" stroke-width="{sw}" stroke-linecap="round"{d} opacity="{op}"/>'


def circle(c, r, fill, stroke=INK, sw=1.5):
    return f'<circle cx="{c[0]:.1f}" cy="{c[1]:.1f}" r="{r}" fill="{fill}" stroke="{stroke}" stroke-width="{sw}"/>'


def slab(x0, y0, w, d, z0, t, ox, oy, s, top, side, front, sw=1.6, op=1.0):
    """box from (x0,y0,z0) size w x d x t; returns svg"""
    P = lambda x, y, z: iso(x, y, z, ox, oy, s)
    x1, y1, z1 = x0 + w, y0 + d, z0 + t
    out = []
    out.append(poly([P(x1, y0, z0), P(x1, y1, z0), P(x1, y1, z1), P(x1, y0, z1)], side, sw=sw, op=op))
    out.append(poly([P(x0, y1, z0), P(x1, y1, z0), P(x1, y1, z1), P(x0, y1, z1)], front, sw=sw, op=op))
    out.append(poly([P(x0, y0, z1), P(x1, y0, z1), P(x1, y1, z1), P(x0, y1, z1)], top, sw=sw, op=op))
    return "".join(out)


def arrow_down(x, y0, y1, sw=3):
    return (line((x, y0), (x, y1 - 10), sw=sw) +
            f'<polygon points="{x - 8},{y1 - 12} {x + 8},{y1 - 12} {x},{y1 + 2}" fill="{INK}"/>')

# ---------------------------------------------------------------- Delaunay (Bowyer-Watson)

def delaunay(pts):
    big = [(-1e4, -1e4), (1e4, -1e4), (0, 1e4)]
    P = pts + big
    tris = [(len(pts), len(pts) + 1, len(pts) + 2)]

    def circ(t):
        (ax, ay), (bx, by), (cx, cy) = P[t[0]], P[t[1]], P[t[2]]
        d = 2 * (ax * (by - cy) + bx * (cy - ay) + cx * (ay - by))
        ux = ((ax * ax + ay * ay) * (by - cy) + (bx * bx + by * by) * (cy - ay) + (cx * cx + cy * cy) * (ay - by)) / d
        uy = ((ax * ax + ay * ay) * (cx - bx) + (bx * bx + by * by) * (ax - cx) + (cx * cx + cy * cy) * (bx - ax)) / d
        return ux, uy, (ax - ux) ** 2 + (ay - uy) ** 2

    for i, p in enumerate(pts):
        bad = []
        for t in tris:
            ux, uy, r2 = circ(t)
            if (p[0] - ux) ** 2 + (p[1] - uy) ** 2 < r2 - 1e-9:
                bad.append(t)
        edges = {}
        for t in bad:
            for e in ((t[0], t[1]), (t[1], t[2]), (t[2], t[0])):
                k = tuple(sorted(e))
                edges[k] = edges.get(k, 0) + 1
        tris = [t for t in tris if t not in bad]
        for e, c in edges.items():
            if c == 1:
                tris.append((e[0], e[1], i))
    n = len(pts)
    return [t for t in tris if max(t) < n]


def slab_mesh_points(W, D, h):
    random.seed(11)
    pts = []
    nx, ny = int(round(W / h)), int(round(D / h))
    for i in range(nx + 1):
        pts.append((W * i / nx, 0)); pts.append((W * i / nx, D))
    for j in range(1, ny):
        pts.append((0, D * j / ny)); pts.append((W, D * j / ny))
    # interior lattice (triangle lattice, jittered)
    r = 1
    while r * h * 0.87 < D - 0.6 * h:
        yy = r * h * 0.87
        off = 0.5 * h if r % 2 else 0
        x = off + 0.6 * h
        while x < W - 0.6 * h:
            pts.append((x + random.uniform(-0.08, 0.08) * h, yy + random.uniform(-0.08, 0.08) * h))
            x += h
        r += 1
    return pts

# ---------------------------------------------------------------- meshing pipeline

def arrow_h(x0, x1, y, sw=3):
    return (line((x0, y), (x1 - 10, y), sw=sw) +
            f'<polygon points="{x1 - 12},{y - 8} {x1 - 12},{y + 8} {x1 + 2},{y}" fill="{INK}"/>')


def arrow_up(x, y0, y1, sw=3):
    return (line((x, y0), (x, y1 + 10), sw=sw) +
            f'<polygon points="{x - 8},{y1 + 12} {x + 8},{y1 + 12} {x},{y1 - 2}" fill="{INK}"/>')


def meshing():
    Wc, Hc = 860, 800
    s = 1.0
    body = []
    W, D = 200, 110
    ox = 130
    P = lambda x, y, z, oy, oxx=ox: iso(x, y, z, oxx, oy, s)
    # Stage 1: printed layers grouped into slabs (8 layers, 2 slabs of 4)
    oy1 = 110
    lt = 9
    for k in range(8):
        grp = k // 4
        top = OR_TOP if grp == 0 else "#FDE7C6"
        side = OR_SIDE if grp == 0 else "#F6C88F"
        front = OR_FRONT if grp == 0 else "#EFB271"
        body.append(slab(0, 0, W, D, k * (lt + 2), lt, ox, oy1, s, top, side, front, sw=1.2))
    body.append(arrow_down(ox + 40, oy1 + 150, oy1 + 205))
    # Stage 2: CDT of the slab shape
    oy2 = 345
    tri_pts = slab_mesh_points(W, D, 34)
    tris = delaunay(tri_pts)
    body.append(slab(0, 0, W, D, 0, 6, ox, oy2, s, OR_TOP, OR_SIDE, OR_FRONT, sw=1.4))
    for t in tris:
        body.append(poly([P(*tri_pts[i], 6, oy2) for i in t], "none", stroke="#8A4B12", sw=1.0))
    for p in tri_pts:
        body.append(circle(P(p[0], p[1], 6, oy2), 2.2, "#8A4B12", stroke="none", sw=0))
    body.append(arrow_down(ox + 40, oy2 + 168, oy2 + 215))
    # Stage 3: extrude every triangle into a prism one slab thick
    oy3 = 600
    th = 34
    body.append(slab(0, 0, W, D, 0, th, ox, oy3, s, OR_TOP, OR_SIDE, OR_FRONT, sw=1.4))
    for t in tris:
        body.append(poly([P(*tri_pts[i], th, oy3) for i in t], "none", stroke="#8A4B12", sw=1.0))
    xs = sorted(set(round(p[0], 3) for p in tri_pts if abs(p[1] - D) < 1e-6))
    for x in xs:
        body.append(line(P(x, D, 0, oy3), P(x, D, th, oy3), stroke="#8A4B12", sw=1.0))
    ys = sorted(set(round(p[1], 3) for p in tri_pts if abs(p[0] - W) < 1e-6))
    for y in ys:
        body.append(line(P(W, y, 0, oy3), P(W, y, th, oy3), stroke="#8A4B12", sw=1.0))
    body.append(arrow_h(330, 450, 640))
    # Stage 4: one prism split into 3 tetrahedra (global-index rule), exploded
    pr = [(0, 0, 0), (80, 0, 0), (20, 70, 0), (0, 0, 80), (80, 0, 80), (20, 70, 80)]
    tets = [(0, 1, 2, 3), (1, 2, 3, 4), (2, 3, 4, 5)]
    cols = [OR_SIDE, BL_SIDE, "#F2A7D3"]
    offs = [(-34, 0, -20), (0, 0, 0), (34, 0, 20)]
    oxp, oyp = 580, 705
    for t, c, o in zip(tets, cols, offs):
        V = [iso(pr[i][0] + o[0] * 2.5, pr[i][1], pr[i][2] + o[2] * 1.6, oxp, oyp, 1.0) for i in t]
        for f in [(0, 1, 2), (0, 1, 3), (0, 2, 3), (1, 2, 3)]:
            body.append(poly([V[i] for i in f], c, sw=1.4, op=0.55))
        for v in V:
            body.append(circle(v, 3.4, INK, stroke="none", sw=0))
    body.append(arrow_up(640, 560, 500))
    # Stage 5: slab on slab, joined by weld ties (top node -> point inside a triangle below)
    ox5, oy5 = 560, 330
    gap = 64
    body.append(slab(0, 0, W, D, 0, 24, ox5, oy5, s, BL_TOP, BL_SIDE, BL_FRONT, sw=1.4))
    Q = lambda x, y, z: iso(x, y, z, ox5, oy5, s)
    for (x, y) in [(28, 30), (85, 22), (145, 40), (185, 25), (55, 85), (120, 90), (170, 80)]:
        a = Q(x, y, 24 + gap)
        b = Q(x, y, 24)
        n = 6
        pts = []
        for k in range(n + 1):
            yy = a[1] + (b[1] - a[1]) * k / n
            xx = a[0] + ((6 if k % 2 else -6) if 0 < k < n else 0)
            pts.append((xx, yy))
        body.append(f'<polyline points="{fmt(pts)}" fill="none" stroke="{MAG}" stroke-width="2.2" stroke-linejoin="round"/>')
        body.append(circle(b, 4.2, MAG, stroke="none", sw=0))
        body.append(circle(a, 4.2, "#FFFFFF", stroke=MAG, sw=2))
    body.append(slab(0, 0, W, D, 24 + gap, 24, ox5, oy5, s, BL_TOP, BL_SIDE, BL_FRONT, sw=1.4, op=0.55))
    write("meshing_pipeline.svg", Wc, Hc, "".join(body))

# ---------------------------------------------------------------- tetrahedra

def tet(name, quadratic):
    W, H = 720, 560
    # A left-front, B right-front, C back (hidden), D apex -- 2D positions of an affine view
    P = [(70, 470), (600, 500), (430, 330), (300, 60)]
    defs = ""
    body = []
    if quadratic:
        defs = ('<linearGradient id="g1" x1="0" y1="1" x2="1" y2="0">'
                '<stop offset="0" stop-color="#9CC6EE"/><stop offset="1" stop-color="#F7B7DD"/></linearGradient>')
        f1 = "url(#g1)"
    else:
        f1 = BL_SIDE
    # hidden edges to the back vertex C
    for i in (0, 1, 3):
        body.append(line(P[i], P[2], stroke=INK, sw=2.2, dash="10 9", op=0.7))
    body.append(poly([P[0], P[1], P[3]], f1, sw=2.6, op=0.62))
    if quadratic:
        a = (5 + 3 * math.sqrt(5)) / 20; b = (5 - math.sqrt(5)) / 20
        for k in range(4):
            L = [b] * 4; L[k] = a
            q = (sum(L[i] * P[i][0] for i in range(4)), sum(L[i] * P[i][1] for i in range(4)))
            body.append(f'<path d="M{q[0]-8:.1f},{q[1]-8:.1f} L{q[0]+8:.1f},{q[1]+8:.1f} M{q[0]-8:.1f},{q[1]+8:.1f} L{q[0]+8:.1f},{q[1]-8:.1f}" stroke="#C25E00" stroke-width="3.5" stroke-linecap="round"/>')
        for i, j in [(0, 1), (1, 2), (0, 2), (0, 3), (1, 3), (2, 3)]:
            m = ((P[i][0] + P[j][0]) / 2, (P[i][1] + P[j][1]) / 2)
            body.append(circle(m, 12, MAG, stroke="#FFFFFF", sw=3))
    for p in P:
        body.append(circle(p, 15, BLUE, stroke="#FFFFFF", sw=3))
    write(name, W, H, "".join(body), defs)
    return P

meshing(); title_band(); cover_block()
P4 = tet("tet4.svg", False)
P10 = tet("tet10.svg", True)
print("tet4 nodes", [(round(x), round(y)) for x, y in P4])
