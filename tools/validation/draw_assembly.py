#!/usr/bin/env python3
"""draw_assembly.py -- 3D views of the fully assembled test setups.

Builds each scene from the real CAD of make_specimens.py (test pieces and the
all-PLA loading kit) plus simple stand-ins for the weights, cord, overhead bar,
table and vise, renders it with VTK and labels it with matplotlib.

Output: docs/validation/specimens/setup/6_assembly_tension.png
        docs/validation/specimens/setup/7_assembly_vise.png

Needs CadQuery and VTK. On a machine without a display:
    xvfb-run -a python tools/validation/draw_assembly.py
"""
import os
import sys

import numpy as np
import cadquery as cq
import vtk
from vtk.util.numpy_support import vtk_to_numpy
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_specimens as m  # noqa: E402

OUT = "docs/validation/specimens/setup"
C_PART = (0.42, 0.62, 0.88)      # test piece
C_KIT = (0.96, 0.66, 0.36)       # printed loading kit
C_KIT2 = (0.86, 0.52, 0.26)
C_KIT3 = (0.93, 0.80, 0.58)
C_PLATE = (0.25, 0.25, 0.27)
C_CORD = (0.55, 0.36, 0.24)
C_WOOD = (0.72, 0.58, 0.42)
C_STEEL = (0.55, 0.57, 0.60)
C_FLOOR = (0.93, 0.93, 0.92)
P = 40.0                         # bottom wedge pulled 40 mm -> platform 5 mm down
DROP = P / m.SLOPE
GAP = 4.0                        # plates hang this far above the platform


# ------------------------------------------------------------ geometry ---
def mesh(shape, tol=0.15):
    v, f = shape.val().tessellate(tol, 0.2)
    return np.array([(p.x, p.y, p.z) for p in v]), np.array(f, dtype=np.int64)


def frame(cols, t):
    """4x4 transform: columns = world images of the local x, y, z axes."""
    T = np.eye(4)
    T[:3, :3] = np.array(cols, dtype=float).T
    T[:3, 3] = t
    return T


def move(t):
    return frame([(1, 0, 0), (0, 1, 0), (0, 0, 1)], t)


def rot_z180(cx):
    """Half turn about the vertical line x = cx, y = 0."""
    return frame([(-1, 0, 0), (0, -1, 0), (0, 0, 1)], (2 * cx, 0, 0))


def apply(T, pts):
    pts = np.asarray(pts, dtype=float)
    return pts @ T[:3, :3].T + T[:3, 3]


def plate(d, t):
    return cq.Workplane("XY").circle(d / 2).circle(14).extrude(t)


def cyl_x(r, x0, x1):
    return cq.Workplane("YZ").workplane(offset=x0).circle(r).extrude(x1 - x0)


def cyl_y(r, y0, y1):
    return cq.Workplane("XZ").workplane(offset=-y1).circle(r).extrude(y1 - y0)


class Scene:
    def __init__(self):
        self.solids, self.tubes, self.anchors = [], [], {}

    def add(self, shape, T, color, opacity=1.0, edges=True):
        v, f = mesh(shape) if not isinstance(shape, tuple) else shape
        self.solids.append((apply(T, v), f, color, opacity, edges))

    def tube(self, pts, color=C_CORD, r=1.5):
        self.tubes.append((np.asarray(pts, float), color, r))


def loader(sc, T, plates, kit_masses=0, ghost=False):
    """Wedge lowerer with the plates hanging GAP above the platform, in the
    loader's own frame (toggle channel at x = CH_X), placed by T. Returns the
    world point of the top of the plate stack on the cord axis."""
    cx = m.CH_X
    sc.add(m.wedge_base(), T, C_KIT3)
    sc.add(m.wedge_lower(), T @ move((-P, 0, 4)), C_KIT2)
    sc.add(m.wedge_upper(), T @ move((0, 0, 4 - DROP)), C_KIT)
    zp = 4 + m.PLATFORM - DROP + GAP                       # bottom of the plates
    sc.add(m.toggle_bar(), T @ frame([(0, 1, 0), (-1, 0, 0), (0, 0, 1)], (cx, 0, zp - 16)), C_KIT2)
    z = zp
    for d, t in plates:
        sc.add(plate(d, t), T @ move((cx, 0, z)), C_PLATE, 0.16 if ghost else 1.0)
        z += t
    for i in range(kit_masses):                            # slotted kit masses on top
        sc.add(cq.Workplane("XY").circle(22).extrude(9), T @ move((cx + 55, 0, z + 9 * i)), C_STEEL)
    # pull cord tied through the lug's hand hole, running toward the operator
    sc.tube(apply(T, [(-P - 30, 0, 9), (-P - 70, 0, 14), (-P - 210, 0, 40)]))
    sc.anchors.update({
        "base": apply(T, [(147, -44, 20)])[0],
        "post": apply(T, [(-8, -44, 30)])[0],
        "lower": apply(T, [(30 - P, -25, 14)])[0],
        "tab": apply(T, [(m.MAX_PULL + 4 - P, -40, 8)])[0],
        "upper": apply(T, [(120, -44, 40 - DROP)])[0],
        "toggle": apply(T, [(cx, -55, zp - 8)])[0],
        "plates": apply(T, [(cx, 0, zp)])[0] + (0, -0.97 * plates[0][0] / 2, plates[0][1] / 2),
        "loader": apply(T, [(70, 0, 30)])[0] + (0, -44, 0),
        "pull": apply(T, [(-P - 150, 0, 31)])[0],
        "gap": apply(T, [(cx + 60, -40, zp - GAP / 2)])[0],
    })
    return apply(T, [(cx, 0, z)])[0], zp


def lower_cord(sc, T, zp, top, z_eye, eye_xy):
    """Doubled cord: around the toggle notch, up through the plate holes, through
    the eye of the part above (eye axis along world y)."""
    cx = m.CH_X
    lo = zp - 16 - 1.5
    strand = [(cx - 11.5, 0, top + 10), (cx - 11.5, 0, lo), (cx + 11.5, 0, lo), (cx + 11.5, 0, top + 10)]
    w = apply(T, strand)
    ex, ey = eye_xy
    pts = [(ex, 16, z_eye), (ex, -16, z_eye), tuple(w[0]), tuple(w[1]), tuple(w[2]), tuple(w[3]), (ex, 16, z_eye)]
    sc.tube(pts)


# ------------------------------------------------------------- scenes ---
def scene_tension(ghost=False):
    sc = Scene()
    cx = m.CH_X
    sc.add(m.box(-300, 420, -260, 260, -6, 0), move((0, 0, 0)), C_FLOOR, edges=False)
    top, zp = loader(sc, np.eye(4), [(305, 32), (305, 32), (230, 25)], ghost=ghost)
    z_eye_lo = top[2] + 55
    zo_lo = z_eye_lo + 47                                   # lower clevis open end
    sc.add(m.grip_clevis(2.4), frame([(0, 0, -1), (0, 1, 0), (1, 0, 0)], (cx - 20, 0, zo_lo)), C_KIT)
    sc.add(m.pin(8, 28, 14), frame([(0, 1, 0), (1, 0, 0), (0, 0, -1)], (cx, -12, zo_lo - 17)), C_KIT2)
    zb = zo_lo - 32                                         # dogbone bottom end at the slot bottom
    sc.add(m.dogbone(2.0), frame([(0, 0, 1), (1, 0, 0), (0, 1, 0)], (cx, -1, zb + 84.96)), C_PART)
    zo_up = zb + 169.92 - 32
    sc.add(m.grip_clevis(2.4), frame([(0, 0, 1), (0, 1, 0), (-1, 0, 0)], (cx + 20, 0, zo_up)), C_KIT)
    sc.add(m.pin(8, 28, 14), frame([(0, 1, 0), (1, 0, 0), (0, 0, -1)], (cx, -12, zo_up + 17)), C_KIT2)
    z_eye_up = zo_up + 47
    z_bar = z_eye_up + 52
    lower_cord(sc, np.eye(4), zp, top[2], z_eye_lo, (cx, 0))
    # overhead bar along X on two supports, upper cord loop around it
    sc.add(cyl_x(16, cx - 330, cx + 330), move((0, 0, z_bar)), C_STEEL)
    for x in (cx - 310, cx + 310):
        sc.add(m.box(x - 20, x + 20, -20, 20, 0, z_bar - 16), move((0, 0, 0)), C_WOOD)
        sc.add(m.box(x - 60, x + 60, -60, 60, 0, 12), move((0, 0, 0)), C_WOOD)
    ring = [(cx, 16 * np.cos(a), z_bar + 17.5 * np.sin(a)) for a in np.linspace(-0.2, np.pi + 0.2, 16)]
    sc.tube([(cx, -16, z_eye_up), (cx, 16, z_eye_up), (cx, 17.5, z_bar - 4)] + ring
            + [(cx, -17.5, z_bar - 4), (cx, -16, z_eye_up)])
    sc.anchors.update({
        "obar": (cx + 200, 0, z_bar), "upcord": (cx, -17, z_bar - 18),
        "upclevis": (cx - 20, -12, zo_up + 30), "dogbone": (cx, 0, zb + 85),
        "loclevis": (cx - 20, -12, zo_lo - 30), "locord": (cx - 11.5, 0, top[2] + 25),
        "support": (cx + 310, -20, 200),
    })
    return sc


def scene_vise(ghost=False):
    sc = Scene()
    sc.add(m.box(-420, 380, -340, 340, -6, 0), move((0, 0, 0)), C_FLOOR, edges=False)
    zt = 745.0                                              # table top
    sc.add(m.box(-380, 0, -300, 300, zt - 25, zt), move((0, 0, 0)), C_WOOD)
    for y in (-280, 240):
        sc.add(m.box(-60, -20, y, y + 40, 0, zt - 25), move((0, 0, 0)), C_WOOD)
    zv = zt + 30                                            # vise jaws and block base
    sc.add(m.box(-170, 4, -55, 55, zt, zv), move((0, 0, 0)), C_STEEL)
    for y0, y1 in ((20, 48), (-48, -20)):
        sc.add(m.box(-32, 4, y0, y1, zv, zv + 24), move((0, 0, 0)), C_STEEL)
    sc.add(cyl_y(6, 48, 120), move((-14, 0, zv + 12)), C_STEEL)
    sc.add(cyl_x(4, -60, 32), move((0, 120, zv + 12)), C_STEEL)
    sc.add(m.bar_flat(), move((0, 0, zv)), C_PART)
    zpin = zv + 8
    sc.add(m.pin(6, 44, 11), frame([(0, 1, 0), (1, 0, 0), (0, 0, -1)], (80, -22, zpin)), C_KIT2)
    sc.add(m.load_yoke(), frame([(0, 1, 0), (0, 0, 1), (1, 0, 0)], (80 - 6, 0, zpin - 62)), C_KIT)
    z_eye = zpin - 62 - 8
    T = move((80 - m.CH_X, 0, 0)) @ rot_z180(m.CH_X)       # loader turned so you pull away from the table
    top, zp = loader(sc, T, [(200, 22)], kit_masses=3, ghost=ghost)
    # cord: yoke eye (axis Z) down to the toggle; drawn as a doubled strand pair
    lo = zp - 16 - 1.5
    sc.tube([(80 - 11.5, 0, top[2] + 30), (80 - 11.5, 0, lo), (80 + 11.5, 0, lo), (80 + 11.5, 0, top[2] + 30),
             (81.5, 0, z_eye - 10), (80, 0, z_eye + 6), (78.5, 0, z_eye - 10), (80 - 11.5, 0, top[2] + 30)])
    sc.anchors.update({
        "vise": (-80, -55, zv - 10), "jaw": (-14, -48, zv + 12), "eye": (86, 0, z_eye), "bar": (40, -4, zv + 4), "pin": (80, -24, zpin),
        "yoke": (80, -18, zpin - 40), "cord": (80 - 11.5, 0, 400), "table": (-200, -300, zt - 12),
        "masses": apply(T, [(m.CH_X + 55, 0, top[2] + 15)])[0],
    })
    return sc


# ------------------------------------------------------------- render ---
def polydata(v, f):
    pts = vtk.vtkPoints()
    for p in v:
        pts.InsertNextPoint(*p)
    cells = vtk.vtkCellArray()
    for t in f:
        cells.InsertNextCell(3)
        for i in t:
            cells.InsertCellPoint(int(i))
    pd = vtk.vtkPolyData()
    pd.SetPoints(pts)
    pd.SetPolys(cells)
    return pd


def render(sc, cam, size, ss=2):
    """Render at ss x size and downsample (anti-aliasing). Returns the image
    and a function mapping world points to image pixels."""
    ren = vtk.vtkRenderer()
    ren.SetBackground(1, 1, 1)
    ren.SetUseDepthPeeling(1)
    ren.SetMaximumNumberOfPeels(8)
    for v, f, color, op, edges in sc.solids:
        clean = vtk.vtkCleanPolyData()
        clean.SetInputData(polydata(v, f))
        nrm = vtk.vtkPolyDataNormals()
        nrm.SetInputConnection(clean.GetOutputPort())
        nrm.SetFeatureAngle(35)
        nrm.SplittingOn()
        mp = vtk.vtkPolyDataMapper()
        mp.SetInputConnection(nrm.GetOutputPort())
        a = vtk.vtkActor()
        a.SetMapper(mp)
        pr = a.GetProperty()
        pr.SetColor(*color)
        pr.SetOpacity(op)
        pr.SetAmbient(0.35)
        pr.SetDiffuse(0.75)
        pr.SetSpecular(0.1)
        ren.AddActor(a)
        if edges:
            fe = vtk.vtkFeatureEdges()
            fe.SetInputConnection(clean.GetOutputPort())
            fe.BoundaryEdgesOn()
            fe.FeatureEdgesOn()
            fe.SetFeatureAngle(35)
            fe.ManifoldEdgesOff()
            fe.NonManifoldEdgesOff()
            fe.ColoringOff()
            em = vtk.vtkPolyDataMapper()
            em.SetInputConnection(fe.GetOutputPort())
            em.SetResolveCoincidentTopologyToPolygonOffset()
            ea = vtk.vtkActor()
            ea.SetMapper(em)
            ea.GetProperty().SetColor(0.12, 0.12, 0.12)
            ea.GetProperty().SetOpacity(max(op, 0.35))
            ea.GetProperty().SetLineWidth(1.4 * ss)
            ren.AddActor(ea)
    for pts, color, r in sc.tubes:
        line = vtk.vtkPolyLineSource()
        line.SetNumberOfPoints(len(pts))
        for i, p in enumerate(pts):
            line.SetPoint(i, *p)
        tb = vtk.vtkTubeFilter()
        tb.SetInputConnection(line.GetOutputPort())
        tb.SetRadius(r)
        tb.SetNumberOfSides(12)
        mp = vtk.vtkPolyDataMapper()
        mp.SetInputConnection(tb.GetOutputPort())
        a = vtk.vtkActor()
        a.SetMapper(mp)
        a.GetProperty().SetColor(*color)
        ren.AddActor(a)
    c = ren.GetActiveCamera()
    d = np.array(cam["direction"], float)
    d /= np.linalg.norm(d)
    f = np.array(cam["focal"], float)
    c.SetFocalPoint(*f)
    c.SetPosition(*(f + d * cam["dist"]))
    c.SetViewUp(0, 0, 1)
    c.SetViewAngle(cam["angle"])
    ren.ResetCameraClippingRange()
    vtk.vtkLightKit().AddLightsToRenderer(ren)
    W, H = size[0] * ss, size[1] * ss
    win = vtk.vtkRenderWindow()
    win.SetOffScreenRendering(1)
    win.SetAlphaBitPlanes(1)
    win.SetMultiSamples(0)
    win.AddRenderer(ren)
    win.SetSize(W, H)
    win.Render()
    grab = vtk.vtkWindowToImageFilter()
    grab.SetInput(win)
    grab.ReadFrontBufferOff()
    grab.Update()
    im = grab.GetOutput()
    w, h, _ = im.GetDimensions()
    img = vtk_to_numpy(im.GetPointData().GetScalars()).reshape(h, w, -1)[::-1, :, :3]
    from PIL import Image
    img = np.asarray(Image.fromarray(np.ascontiguousarray(img)).resize(size, Image.LANCZOS))
    Mv = c.GetCompositeProjectionTransformMatrix(W / H, -1, 1)
    M = np.array([[Mv.GetElement(i, j) for j in range(4)] for i in range(4)])

    def project(p):
        q = M @ np.array([*map(float, p), 1.0])
        q = q[:3] / q[3]
        return (q[0] + 1) / 2 * size[0], (1 - (q[1] + 1) / 2) * size[1]
    return img, project


def layout(items, lo, hi, gap):
    """Spread label y positions (sorted by anchor) so they keep `gap` apart."""
    items = sorted(items, key=lambda it: it[1])
    ys = [min(max(y, lo), hi) for _, y in items]
    for i in range(1, len(ys)):
        ys[i] = max(ys[i], ys[i - 1] + gap)
    over = ys[-1] - hi if ys else 0
    if over > 0:
        ys = [y - over for y in ys]
        for i in range(len(ys) - 2, -1, -1):
            ys[i] = min(ys[i], ys[i + 1] - gap)
    return [(it, y) for it, y in zip(items, ys)]


def panel(ax, sc, labels, cam, size, heading):
    """One rendered view with callouts. cam["sides"] = (room left, room right)
    as fractions of the image width; labels go to the nearer side that has room."""
    img, project = render(sc, cam, size)
    w, h = size
    lw, rw = cam.get("sides", (0.6, 0.6))
    ax.imshow(img, extent=(0, w, h, 0))
    ax.set_xlim(-lw * w, (1 + rw) * w)
    ax.set_ylim(h, -0.06 * h)
    ax.axis("off")
    ax.text(0, -0.03 * h, heading, fontsize=12, fontweight="bold", color="#333", va="bottom")
    pts = {k: project(sc.anchors[k]) for k, _ in labels}
    split = cam.get("split", 0.5) * w

    def side(x):
        if lw < 0.05:
            return "R"
        if rw < 0.05:
            return "L"
        return "L" if x < split else "R"
    for sd in ("L", "R"):
        group = [((k, t), pts[k][1]) for k, t in labels if side(pts[k][0]) == sd]
        if not group:
            continue
        for ((k, t), _), y in layout(group, 0.03 * h, 0.97 * h, cam.get("gap", 0.085) * h):
            x0, y0 = pts[k]
            tx = -0.03 * w if sd == "L" else 1.03 * w
            test = k in ("dogbone", "bar")
            ax.annotate(t, xy=(x0, y0), xytext=(tx, y), fontsize=10.5,
                        ha="right" if sd == "L" else "left", va="center",
                        color="#1f4e8c" if test else "#222", fontweight="bold" if test else "normal",
                        arrowprops=dict(arrowstyle="-", color="#666", lw=0.9, shrinkA=3, shrinkB=0),
                        bbox=dict(boxstyle="round,pad=0.35", fc="white", ec="#1f4e8c" if test else "#aaa", lw=0.8))
            ax.plot(x0, y0, "o", ms=4.5, color="#c62828", zorder=5)


def figure(title, sub, panels, out, figsize, ratios):
    from matplotlib.patches import Patch
    fig, axs = plt.subplots(1, len(panels), figsize=figsize, gridspec_kw=dict(width_ratios=ratios))
    for ax, args in zip(axs, panels):
        panel(ax, *args)
    fig.suptitle(title, fontsize=16, fontweight="bold", x=0.015, ha="left", y=0.985)
    fig.text(0.015, 0.012, sub, fontsize=10.5, color="#333", va="bottom")
    fig.legend(handles=[Patch(color=C_PART, label="test piece"), Patch(color=C_KIT, label="printed PLA loading kit"),
                        Patch(color=C_PLATE, label="your weights"), Patch(color=C_CORD, label="nylon cord, doubled"),
                        Patch(color=C_WOOD, label="your furniture / supports")],
               loc="upper right", ncol=5, fontsize=10, frameon=False, bbox_to_anchor=(0.99, 0.975))
    fig.subplots_adjust(left=0.005, right=0.995, top=0.93, bottom=0.07, wspace=0.02)
    fig.savefig(out, dpi=100, facecolor="white")
    plt.close(fig)
    print("wrote", out)


def main():
    os.makedirs(OUT, exist_ok=True)
    cx = m.CH_X
    chain = [
        ("obar", "Overhead bar, kept LOW (about 0.45 m up)\nso both cords are short: nylon stretches"),
        ("support", "Any sturdy supports\n(table frame, two heavy stools)"),
        ("upcord", "Short doubled cord loop"),
        ("upclevis", "PLA clevis grip + Ø8 PLA pin"),
        ("dogbone", "TEST PIECE: dogbone\n(breaks in the 3 mm neck)"),
        ("loclevis", "PLA clevis grip + Ø8 PLA pin"),
        ("locord", "Doubled cord, down through\nthe plate holes"),
        ("plates", "Plates hang from the specimen\n(here 25 + 25 + 10 lb)"),
        ("loader", "Wedge lowerer\n(see close-up)"),
    ]
    kit = [
        ("toggle", "Toggle bar in the platform\nchannel; cord loops under it"),
        ("gap", "3–5 mm gap: the specimen\nnow carries the plates"),
        ("upper", "Top wedge: platform"),
        ("lower", "Bottom wedge, 1:8 slope:\n8 mm pull = 1 mm drop"),
        ("tab", "Stop tabs: at most 64 mm\nof pull (8 mm drop)"),
        ("post", "Base posts hold the top\nwedge at the pull end"),
        ("base", "Base end wall: other end\nof the top wedge"),
        ("pull", "Pull cord: pull slowly\ntoward you"),
        ("plates", "Plates (drawn see-through)"),
    ]
    main_cam = dict(direction=(-0.62, -1.0, 0.36), focal=(cx, 0, 225), dist=2000, angle=22,
                    sides=(0.72, 0.02), gap=0.075)
    close_cam = dict(direction=(-0.55, -1.0, 0.5), focal=(10, 0, 45), dist=820, angle=22,
                     sides=(0.55, 0.55), split=0.53, gap=0.1)
    figure("Assembled setup — tension test (dogbone) on the all-PLA wedge lowerer",
           "Shown while the specimen holds a load step: bottom wedge pulled 40 mm, platform 5 mm down, plates hanging 4 mm above it.  "
           "If the dogbone breaks, the plates drop only that gap.\n"
           "Every part in the load path is PLA or cord, and the dogbone's 3 mm neck is the weakest link by 7× or more.  "
           "Bars and the twist piece use the same loader (7_assembly_vise.png).",
           [(scene_tension(), chain, main_cam, (900, 1150), "Full setup"),
            (scene_tension(ghost=True), kit, close_cam, (1000, 800), "Close-up of the wedge lowerer (plates see-through)")],
           os.path.join(OUT, "6_assembly_tension.png"), (21, 10.5), (1.55, 1.95))
    vise = [
        ("vise", "Vise at the table edge,\ngrips the 25 × 40 block"),
        ("bar", "TEST PIECE: bar_flat\n(breaks at the root fillet)"),
        ("yoke", "Load yoke on the Ø6 pin"),
        ("cord", "Doubled cord to the\ntoggle bar"),
        ("table", "Table"),
        ("masses", "Kit masses for fine steps"),
        ("plates", "5 lb plate"),
        ("loader", "Wedge lowerer (same kit),\nturned so you pull away\nfrom the table"),
        ("pull", "Pull cord"),
    ]
    head = [
        ("bar", "TEST PIECE: 8 × 8 bar"),
        ("pin", "Ø6 PLA pin through the load\nhead, 80 mm from the block"),
        ("yoke", "Yoke hangs on BOTH pin ends,\nso the pin is not a cantilever"),
        ("eye", "Cord ties through the eye"),
        ("jaw", "Vise jaw"),
    ]
    figure("Assembled setup — bending test (bar) in the vise; the twist piece hangs the same way",
           "Twist piece: clamp its 40 × 40 block in the vise and hang the yoke on the Ø6 pin through its load block (100 mm lever).\n"
           "The cord is long here, but bar and twist loads are only a few kg, so cord stretch does not matter.",
           [(scene_vise(), vise, dict(direction=(1.0, -1.25, 0.45), focal=(30, 0, 390), dist=2550, angle=22,
                                      sides=(0.7, 0.7), split=0.5, gap=0.08), (900, 1150), "Full setup"),
            (scene_vise(), head, dict(direction=(0.9, -1.3, 0.6), focal=(60, 0, 760), dist=620, angle=22,
                                      sides=(0.5, 0.6), split=0.45, gap=0.13), (900, 800), "Close-up of the load head")],
           os.path.join(OUT, "7_assembly_vise.png"), (21, 11.5), (1.9, 1.75))


if __name__ == "__main__":
    main()
