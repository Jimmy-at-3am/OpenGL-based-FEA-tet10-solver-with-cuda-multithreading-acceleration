#!/usr/bin/env python3
"""validate.py -- companion tool for docs/validation/physical-test-plan.md

  estimate   closed-form breaking loads, stiffness and a loading recipe per test
  predict    PolyFEA prediction for one test (drives FEAPreProcessor --run)
  calibrate  results.csv -> calibrated .mat file
  verdict    results.csv + predictions.csv -> pass / fail table

Masses may carry units and be summed, e.g. "45lb+350g+1.2kg"; a bare number is
kilograms. Deflections are millimetres. The geometry constants below must match
section 4 of the plan.
"""
import argparse
import csv
import datetime
import json
import math
import os
import re
import subprocess
import sys

G = 9.81
LB_KG = 0.45359237

# ---- specimen geometry (mm): keep in sync with the plan, section 4 ----------
COUPON_W = 3.0                                    # dogbone gauge width
COUPON_T = {"tension_flat": 2.0, "tension_standing": 4.0}
BAR_B = BAR_H = 8.0                               # bar section
BAR_L = 80.0                                      # grip-block face to load-hole axis
TW_D = 8.0                                        # twist shaft diameter
TW_LG = 20.0                                      # block face to lever face
TW_A = 100.0                                      # shaft axis to load-hole axis (lever arm)
TW_Z = 38.0                                       # block face to load-hole axis
LEVER_W, LEVER_T = 16.0, 12.0                     # lever: in-plane width, thickness
CR_R, CR_H, CR_B = 15.0, 4.0, 10.0                # C-ring: mean radius, radial thk, width
COL_B, COL_T = 6.0, 3.0                           # column section, buckles across COL_T
COLUMNS = {"column60": 60.0, "column80": 80.0, "column100": 100.0}
RIG_B, RIG_H, RIG_L, RIG_E = 20.0, 3.0, 150.0, 69000.0  # aluminium rig check, E in MPa

# ---- simulation setup per test ----------------------------------------------
# preset/sign: scenario load preset and force direction. lmax: largest
# dimension (mm) of the simulation CAD, used to convert --edge-mm to the
# runner's normalized maxVolume. probe: (posMM from the bbox centre, axis).
SIM = {
    "tension_flat":     dict(preset="pullX", sign=1, lmax=170.0, probe=None),
    "tension_standing": dict(preset="pullZ", sign=1, lmax=170.0, probe=None),
    "bar_flat":         dict(preset="bendXZ", sign=-1, lmax=105.0,
                             probe=((52.5, 0.0, -4.0), 2)),
    "bar_standing":     dict(preset="bendZX", sign=-1, lmax=105.0,
                             probe=((-4.0, 0.0, 52.5), 0)),
    "twist_free":       dict(preset="bendZY", sign=-1, lmax=128.0,
                             probe=((56.0, 0.0, 29.0), 1)),
    "cring":            dict(preset="surfaceCompY", sign=1, lmax=38.0,
                             probe=((0.13, 19.0, 0.0), 1)),
}

# ---- acceptance (plan section 9) ---------------------------------------------
# tolF/tolK: tolerances on measured/predicted. role: what the comparison
# means. nom/expF/expK: nominal (dim1, dim2) and the exponents used to scale a
# prediction to each specimen's measured dimensions.
CHECKS = {
    "tension_flat":     dict(tolF=0.03, roleF="closure", nom=(COUPON_W, 2.0),
                             expF=(1, 1)),
    "tension_standing": dict(tolF=0.03, roleF="closure", nom=(COUPON_W, 4.0),
                             expF=(1, 1)),
    "bar_flat":         dict(tolF=0.15, roleF="validation", tolK=0.03,
                             roleK="closure", nom=(BAR_B, BAR_H),
                             expF=(1, 2), expK=(1, 3)),
    "bar_standing":     dict(tolF=0.20, roleF="validation", tolK=0.03,
                             roleK="closure", nom=(BAR_B, BAR_H),
                             expF=(1, 2), expK=(1, 3)),
    "twist_free":       dict(tolF=0.20, roleF="validation", tolK=0.10,
                             roleK="validation", nom=(TW_D, None),
                             expF=(3, 0), expK=(4, 0)),
    "cring":            dict(tolF=0.15, roleF="validation", tolK=0.10,
                             roleK="validation", nom=(CR_B, CR_H),
                             expF=(1, 2), expK=(1, 3)),
}

T95 = {1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365,
       8: 2.306, 9: 2.262, 10: 2.228, 12: 2.179, 15: 2.131, 20: 2.086,
       30: 2.042}


# =============================================================================
# small helpers
# =============================================================================
def parse_mass(text):
    """'45lb+350g+1.2kg' -> kilograms. A bare number is kilograms."""
    total, seen = 0.0, False
    for part in str(text).replace(" ", "").split("+"):
        if not part:
            continue
        m = re.fullmatch(r"([0-9]*\.?[0-9]+(?:[eE][-+]?[0-9]+)?)(kg|g|lbs?)?",
                         part, re.I)
        if not m:
            raise ValueError("cannot read mass '%s' (write e.g. 45lb+350g+1.2kg)"
                             % text)
        unit = (m.group(2) or "kg").lower()
        total += float(m.group(1)) * {"kg": 1.0, "g": 1e-3}.get(unit, LB_KG)
        seen = True
    if not seen:
        raise ValueError("empty mass")
    return total


def parse_list(text, conv):
    """Space- or ';'-separated values; '+' joins the parts of one value."""
    s = re.sub(r"\s*\+\s*", "+", str(text or "").strip())
    return [conv(t) for t in re.split(r"[;\s]+", s) if t]


def fit_line(x, y):
    """Least-squares slope of y on x, and R^2."""
    n = len(x)
    mx, my = sum(x) / n, sum(y) / n
    sxx = sum((a - mx) ** 2 for a in x)
    syy = sum((b - my) ** 2 for b in y)
    sxy = sum((a - mx) * (b - my) for a, b in zip(x, y))
    if sxx == 0:
        raise ValueError("all loads are equal")
    return sxy / sxx, (sxy * sxy / (sxx * syy) if syy > 0 else 1.0)


def t95(df):
    if df > 30:
        return 1.96
    return T95[max(k for k in T95 if k <= df)]


def summarize(vals):
    """n, mean, sample sd, CoV, 95 % half-width of the mean."""
    n = len(vals)
    mean = sum(vals) / n
    sd = math.sqrt(sum((v - mean) ** 2 for v in vals) / (n - 1)) if n > 1 else 0.0
    half = t95(n - 1) * sd / math.sqrt(n) if n > 1 else float("inf")
    return n, mean, sd, (sd / mean if mean else 0.0), half


def judge(mean, half, tol):
    """PASS / FAIL / INCONCLUSIVE for a mean measured/predicted ratio."""
    if abs(mean - 1.0) <= tol and half <= tol:
        return "PASS"
    if mean + half < 1.0 - tol or mean - half > 1.0 + tol:
        return "FAIL"
    return "INCONCLUSIVE"


def load_mat(path):
    vals = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = (s.strip() for s in line.split("=", 1))
            try:
                vals[k] = float(v)
            except ValueError:
                vals[k] = v
    return vals


def props(m):
    """Material constants in MPa, with explicit fallbacks."""
    notes = []
    E = m["E"] / 1e6
    nu = m.get("nu", 0.35)
    Ez = m.get("E_z", 0.0) / 1e6
    if Ez <= 0:
        Ez = E
        notes.append("E_z missing -> using E")
    Gp = m.get("G_pz", 0.0) / 1e6
    if Gp <= 0:
        Gp = E / (2 * (1 + nu))
        notes.append("G_pz missing -> using E/2(1+nu)")
    if "fractureStress" not in m:
        notes.append("fractureStress missing -> PolyFEA's isotropic fracture "
                     "silently uses 250 MPa; add it")
    s_iso = m.get("fractureStress", 0.0) / 1e6
    s_in = m.get("fractureStress_intralayer", 0.0) / 1e6 or s_iso
    s_z = m.get("fractureStress_interlayer", 0.0) / 1e6 or s_in
    t_z = m.get("fractureShear_interlayer", 0.0) / 1e6
    if t_z <= 0:
        t_z = s_z / math.sqrt(3)
        notes.append("fractureShear_interlayer missing -> using interlayer/sqrt(3)")
    return dict(E=E, Ez=Ez, G=Gp, nu=nu, s_in=s_in, s_z=s_z, t_z=t_z), notes


# =============================================================================
# results.csv access
# =============================================================================
def read_rows(path):
    """CSV rows as dicts with lower-case keys; '#' lines and blanks skipped."""
    with open(path, newline="", encoding="utf-8-sig") as f:
        lines = [ln for ln in f if ln.strip() and not ln.lstrip().startswith("#")]
    rows = []
    for raw in csv.DictReader(lines):
        rows.append({(k or "").strip().lower(): (v or "").strip()
                     for k, v in raw.items()})
    return rows


def group(rows):
    by = {}
    for r in rows:
        by.setdefault(r.get("test", ""), []).append(r)
    return by


def label(r):
    return "%s/%s" % (r.get("test", "?"), r.get("id", "?"))


def dims(r):
    out = []
    for key in ("dim1_mm", "dim2_mm"):
        v = r.get(key, "")
        out.append(float(v) if v else None)
    return tuple(out)


def fail_N(r):
    v = r.get("fail", "")
    return parse_mass(v) * G if v else None


def stiffness(r, warn):
    """(k in N/mm, R^2) from the loads/defl_mm columns, or None."""
    if not r.get("loads") or not r.get("defl_mm"):
        return None
    loads = [m * G for m in parse_list(r["loads"], parse_mass)]
    defl = parse_list(r["defl_mm"], float)
    if len(loads) != len(defl):
        warn("%s: %d loads but %d deflections" % (label(r), len(loads), len(defl)))
        return None
    if len(loads) < 3:
        warn("%s: need at least 3 load steps for a slope" % label(r))
        return None
    slope, r2 = fit_line(loads, defl)
    if slope == 0:
        warn("%s: deflection did not change with load" % label(r))
        return None
    if r2 < 0.995:
        warn("%s: R^2 = %.4f < 0.995 (slip or seating?)" % (label(r), r2))
    return 1.0 / abs(slope), r2


def fixture_break(r):
    return r.get("mode", "").lower().startswith("fixture")


def mode_class(text):
    t = (text or "").lower()
    if t.startswith("interlayer"):
        return "interlayer"
    if t.startswith("intralayer"):
        return "intralayer"
    return t or None


# =============================================================================
# estimate
# =============================================================================
def winkler(R, h, b):
    """Outer-fiber tension and inner-fiber compression magnitude (MPa per N)
    at the back of the C-ring, where M = P*R and N = -P."""
    ri, ro, A = R - h / 2, R + h / 2, b * h
    rn = h / math.log(ro / ri)
    e = R - rn
    outer = R * (ro - rn) / (A * e * ro) - 1 / A
    inner = R * (rn - ri) / (A * e * ri) + 1 / A
    return outer, inner


def twist_compliance(p, d=TW_D, propped=False):
    """Load-point compliance (mm/N): shaft twist + lever bending, plus shaft
    bending when the lever is not propped on the shaft axis."""
    J = math.pi * d ** 4 / 32
    c = TW_A ** 2 * TW_LG / (p["G"] * J)
    c += (TW_A - d / 2) ** 3 / (3 * p["E"] * LEVER_T * LEVER_W ** 3 / 12)
    if not propped:
        c += TW_Z ** 3 / (3 * p["Ez"] * math.pi * d ** 4 / 64)
    return c


def estimates(p):
    rows = []
    I_rig = RIG_B * RIG_H ** 3 / 12
    k_rig = 3 * RIG_E * I_rig / RIG_L ** 3
    rows.append(("rig", None, k_rig, "aluminium: %.2f mm per kg at 150 mm" % (G / k_rig)))
    for t in ("tension_flat", "tension_standing"):
        s = p["s_in"] if t == "tension_flat" else p["s_z"]
        rows.append((t, s * COUPON_W * COUPON_T[t], None, "strength x gauge area"))
    I = BAR_B * BAR_H ** 3 / 12
    for t, E, s in (("bar_flat", p["E"], p["s_in"]), ("bar_standing", p["Ez"], p["s_z"])):
        rows.append((t, s * BAR_B * BAR_H ** 2 / (6 * BAR_L), 3 * E * I / BAR_L ** 3,
                     "beam formula; the root fillet lowers it 10-20%"))
    F_tau = p["t_z"] * math.pi * TW_D ** 3 / (16 * TW_A)
    F_sig = p["s_z"] * math.pi * TW_D ** 3 / (32 * TW_Z)
    rows.append(("twist_propped", F_tau, 1 / twist_compliance(p, propped=True),
                 "pure torque; torque = load x %g mm" % TW_A))
    rows.append(("twist_free", min(F_tau, F_sig), 1 / twist_compliance(p),
                 "%s governs" % ("interlayer shear" if F_tau <= F_sig
                                 else "interlayer tension")))
    so, si = winkler(CR_R, CR_H, CR_B)
    Ic = CR_B * CR_H ** 3 / 12
    rows.append(("cring", p["s_in"] / so, 2 * p["E"] * Ic / (math.pi * CR_R ** 3),
                 "outer fiber; symmetric von Mises flags inner fiber at %.0f N"
                 % (p["s_in"] / si)))
    Icol = COL_B * COL_T ** 3 / 12
    for t, L in COLUMNS.items():
        rows.append((t, math.pi ** 2 * p["E"] * Icol / (4 * L ** 2), None,
                     "Euler, fixed base; split over two bottles"))
    return rows


def recipe(test, F):
    if test == "rig":
        return "250 g steps to 1 kg"
    if F is None:
        return "-"
    kg = F / G
    if test.startswith("tension"):
        lb = 5 * math.floor(0.7 * kg / LB_KG / 5)
        return "plates to ~%d lb, then water" % lb
    if test.startswith("column"):
        return "~%d g per bottle per step" % max(5, 5 * round(kg * 1000 / 2 / 10 / 5))
    step = max(10, 10 * round(kg * 1000 * 0.1 / 10))
    return "4 x %d g, then ~%.1f kg + pour" % (step, 0.7 * kg)


def cmd_estimate(args):
    p, notes = props(load_mat(args.mat))
    print("material: %s" % args.mat)
    print("  E=%.0f  E_z=%.0f  G_pz=%.0f MPa | in-plane=%.1f  interlayer=%.1f  "
          "interlayer shear=%.1f MPa" % (p["E"], p["Ez"], p["G"], p["s_in"],
                                         p["s_z"], p["t_z"]))
    for n in notes:
        print("  note: " + n)
    print()
    hdr = "%-17s %8s %7s %7s %8s  %-32s %s" % ("test", "break N", "kg", "lb",
                                               "k N/mm", "loading recipe", "remark")
    print(hdr)
    print("-" * len(hdr))
    for test, F, k, note in estimates(p):
        cols = ("%8.1f %7.2f %7.1f" % (F, F / G, F / G / LB_KG) if F is not None
                else "%8s %7s %7s" % ("-", "-", "-"))
        ks = "%8.2f" % k if k is not None else "%8s" % "-"
        print("%-17s %s %s  %-32s %s" % (test, cols, ks, recipe(test, F), note))


# =============================================================================
# predict
# =============================================================================
KILL_FDM = re.compile(r"\[FRACTURE-FDM\] iter (\d+): killed (\d+) \(interTens=(\d+) "
                      r"interShear=(\d+) intralayer=(\d+)\)")
KILL_ISO = re.compile(r"\[FRACTURE\] iter (\d+): (\d+) elements killed")
CONVERGED = re.compile(r"Converged \(no new failures\)")
HARD_FAIL = re.compile(r"\[FRACTURE\] Solver failed at iteration (\d+)")
SEVERED = "part SEVERED"


class Outcome:
    """Classification of one fracture run from its console log and report."""

    def __init__(self, mag, code, log, report):
        self.mag, self.code, self.report = mag, code, report or {}
        self.kills = []
        for m in KILL_FDM.finditer(log):
            it, n, a, b, c = map(int, m.groups())
            self.kills.append((it, n, {"interlayer-tension": a,
                                       "interlayer-shear": b, "intralayer": c}))
        for m in KILL_ISO.finditer(log):
            it, n = map(int, m.groups())
            self.kills.append((it, n, {"isotropic": n}))
        self.kills.sort(key=lambda kv: kv[0])
        total = sum(n for _, n, _ in self.kills)
        rep_total = (self.report.get("fracture") or {}).get("totalFailed")
        self.total = max(total, int(rep_total or 0))
        self.converged = bool(CONVERGED.search(log))
        hard = HARD_FAIL.search(log)
        self.hard_fail_iter = int(hard.group(1)) if hard else None
        self.severed = SEVERED in log
        # solveBrittleFracture only reports severance from the third iteration
        # on (iter > 1); a singular second iteration after kills is a break too.
        self.quirk = (not self.severed and self.hard_fail_iter is not None
                      and self.hard_fail_iter >= 1 and self.total > 0)
        # maxIter reached while still killing elements: treat as a break.
        self.unfinished = (not self.severed and not self.quirk and code == 0
                           and not self.converged and bool(self.kills)
                           and self.kills[-1][1] > 0)
        self.severed = self.severed or self.quirk or self.unfinished
        self.damaged = self.total > 0
        self.has_fracture_output = bool(self.kills) or self.severed or self.converged

    def problem(self):
        if self.quirk:
            return None
        if self.code != 0:
            return self.report.get("error") or "exit code %d" % self.code
        if not self.has_fracture_output:
            return "no fracture output in the log"
        return None

    def status(self):
        if self.unfinished:
            return "breaks (maxIter reached while still failing; raise --max-iter to confirm)"
        if self.quirk:
            return "breaks (separated on iteration 2)"
        if self.severed:
            return "breaks"
        if self.damaged:
            return "local damage only (%d elements)" % self.total
        return "intact"

    def first_mode(self):
        for _, n, modes in self.kills:
            if n > 0:
                return max(modes, key=modes.get)
        return ""

    def dominant_mode(self):
        acc = {}
        for _, _, modes in self.kills:
            for k, v in modes.items():
                acc[k] = acc.get(k, 0) + v
        return max(acc, key=acc.get) if acc and max(acc.values()) > 0 else ""


def absolutize(path, here):
    if os.path.isabs(path):
        return path
    cand = os.path.join(here, path)
    return os.path.abspath(cand) if os.path.exists(cand) else path


def base_scenario(args, spec):
    if args.scenario:
        with open(args.scenario, encoding="utf-8") as f:
            sc = json.load(f)
        here = os.path.dirname(os.path.abspath(args.scenario))
        geom = sc.get("geometry", {})
        for key in ("stl", "step", "gcode3mf"):
            if isinstance(geom.get(key), str):
                geom[key] = absolutize(geom[key], here)
        if isinstance(sc.get("material"), str):
            sc["material"] = absolutize(sc["material"], here)
        sign = -1.0 if sc["loads"][0].get("mag", 1.0) < 0 else 1.0
        if args.edge_mm:
            lmax = args.lmax_mm or spec["lmax"]
            sc.setdefault("mesh", {})["maxVolume"] = (args.edge_mm ** 3 / 8.5) * (3.0 / lmax) ** 3
        return sc, sign
    if not (args.geometry and args.mat):
        sys.exit("predict needs --geometry and --mat (or --scenario)")
    low = args.geometry.lower()
    if low.endswith(".gcode.3mf"):
        sys.exit("toolpath (.gcode.3mf) predictions need a --scenario file")
    if low.endswith((".step", ".stp")):
        key = "step"
    elif low.endswith((".stl", ".3mf")):
        key = "stl"            # loadFile dispatches on the extension
    else:
        sys.exit("unknown geometry type: %s" % args.geometry)
    edge = args.edge_mm or 1.5
    lmax = args.lmax_mm or spec["lmax"]
    sc = {"v": 1,
          "geometry": {key: os.path.abspath(args.geometry)},
          "material": os.path.abspath(args.mat),
          "mesh": {"maxVolume": args.max_volume or (edge ** 3 / 8.5) * (3.0 / lmax) ** 3,
                   "quality": 1.6},
          "loads": [{"type": "preset", "name": spec["preset"], "mag": 1.0}],
          "solve": {"gpu": args.gpu, "fdmAnisotropy": True, "buildAxis": 2}}
    return sc, float(spec["sign"])


def scenario_for(base, sign, mag, kind, args, probe=None):
    sc = json.loads(json.dumps(base))
    sc["loads"][0]["mag"] = sign * mag
    solve = sc.setdefault("solve", {})
    solve["kind"] = kind
    if kind == "fracture":
        solve["maxIter"] = args.max_iter
    else:
        solve.pop("maxIter", None)
    sc["asserts"] = []
    sc.pop("screenshots", None)
    if probe:
        sc["probes"] = [{"id": "p", "posMM": list(probe), "quantity": "displacement"}]
    else:
        sc.pop("probes", None)
    return sc


def run_case(args, sc, tag):
    os.makedirs(args.workdir, exist_ok=True)
    scen = os.path.abspath(os.path.join(args.workdir, tag + ".json"))
    rep = os.path.abspath(os.path.join(args.workdir, tag + ".report.json"))
    shots = os.path.abspath(os.path.join(args.workdir, "shots"))
    with open(scen, "w", encoding="utf-8") as f:
        json.dump(sc, f, indent=2)
    if os.path.exists(rep):
        os.remove(rep)
    cmd = [args.exe, "--run", scen, "--out", rep, "--shots", shots]
    try:
        proc = subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              timeout=args.timeout)
    except FileNotFoundError:
        sys.exit("cannot start %s" % args.exe)
    except subprocess.TimeoutExpired:
        sys.exit("%s took longer than --timeout %d s" % (tag, args.timeout))
    log = proc.stdout.decode("utf-8", errors="replace")
    with open(os.path.join(args.workdir, tag + ".log"), "w", encoding="utf-8") as f:
        f.write(log)
    report = {}
    if os.path.exists(rep):
        try:
            with open(rep, encoding="utf-8") as f:
                report = json.load(f)
        except ValueError:
            report = {}
    return proc.returncode, log, report


def find_loads(args, base, sign):
    """Bisection on the load magnitude for damage onset and for breaking."""
    cache = {}

    def ev(mag):
        key = round(mag, 6)
        if key not in cache:
            code, log, rep = run_case(args, scenario_for(base, sign, mag, "fracture", args),
                                      "fracture_%.5gN" % mag)
            o = Outcome(mag, code, log, rep)
            msg = o.problem()
            if msg:
                sys.exit("run at %.4g N failed: %s (logs in %s)" % (mag, msg, args.workdir))
            cache[key] = o
            print("  %10.3f N  %s" % (mag, o.status()))
        return cache[key]

    if args.lo and args.hi:
        lo, hi = args.lo, args.hi
    elif args.guess:
        lo, hi = 0.5 * args.guess, 2.0 * args.guess
    else:
        sys.exit("give --guess (e.g. from 'estimate') or --lo and --hi")
    for _ in range(6):
        if ev(hi).severed:
            break
        hi *= 2
    else:
        sys.exit("no break up to %.4g N; raise --hi" % hi)
    for _ in range(6):
        if not ev(lo).damaged:
            break
        lo /= 2
    else:
        sys.exit("damage even at %.4g N; lower --lo" % lo)

    def bracket(pred):
        b = min(o.mag for o in cache.values() if pred(o))
        a = max(o.mag for o in cache.values() if not pred(o) and o.mag < b)
        return a, b

    results = []
    for pred in (lambda o: o.damaged, lambda o: o.severed):
        a, b = bracket(pred)
        while (b - a) / b > args.tol:
            ev(0.5 * (a + b))
            a, b = bracket(pred)
        results.append((a, b))
    (fa, fb), (ca, cb) = results
    return cache[round(fb, 6)], cache[round(cb, 6)], (fa, fb), (ca, cb)


def cmd_predict(args):
    if args.test not in SIM:
        sys.exit("--test must be one of: " + ", ".join(SIM))
    spec = SIM[args.test]
    base, sign = base_scenario(args, spec)
    args.workdir = args.workdir or os.path.join(
        "polyfea_predict", "%s_e%s" % (args.test, args.edge_mm or "scn"))
    print("test %s, preset %s, maxVolume %.4g, runs in %s"
          % (args.test, base["loads"][0].get("name", "?"),
             base.get("mesh", {}).get("maxVolume", 0), args.workdir))

    first = coll = None
    n_tets = ""
    if not args.stiffness_only:
        print("fracture search:")
        first, coll, (fa, fb), (ca, cb) = find_loads(args, base, sign)
        n_tets = (coll.report.get("mesh") or {}).get("nTets", "")
        print("  damage onset F_first   between %.3f and %.3f N" % (fa, fb))
        print("  break        F_collapse between %.3f and %.3f N" % (ca, cb))
        print("  first elements fail as: %s; most failures at break: %s"
              % (first.first_mode() or "-", coll.dominant_mode() or "-"))

    k_pred = ""
    probe = spec["probe"]
    if args.probe_mm:
        pos = tuple(float(v) for v in args.probe_mm.split(","))
        axis = "xyz".index(args.probe_axis) if args.probe_axis else (probe[1] if probe else 2)
        probe = (pos, axis)
    if probe and not args.fracture_only:
        mag = args.k_load or (0.3 * coll.mag if coll else 10.0)
        code, log, rep = run_case(args, scenario_for(base, sign, mag, "linear", args, probe[0]),
                                  "linear_%.5gN" % mag)
        if code != 0:
            sys.exit("linear run failed: %s (logs in %s)" % (rep.get("error") or code,
                                                              args.workdir))
        p = (rep.get("probes") or {}).get("p") or {}
        vec = p.get("vector")
        if not vec:
            sys.exit("no probe result in the linear report (see %s)" % args.workdir)
        if p.get("unit") != "mm":
            print("  warning: probe unit is '%s', not mm" % p.get("unit"))
        if p.get("snapDist", 0) > 0.5:
            print("  warning: probe snapped %.2f mm away; check --probe-mm"
                  % p.get("snapDist", 0))
        k_pred = mag / abs(vec[probe[1]])
        n_tets = n_tets or (rep.get("mesh") or {}).get("nTets", "")
        print("stiffness: %.4g N/mm at the probe (load %.3g N, deflection %.4g mm)"
              % (k_pred, mag, abs(vec[probe[1]])))

    fields = [args.test, args.edge_mm or "", n_tets,
              "%.5g" % k_pred if k_pred != "" else "",
              "%.5g" % first.mag if first else "",
              "%.5g" % coll.mag if coll else "",
              first.first_mode() if first else "",
              coll.dominant_mode() if coll else ""]
    header = ("test,edge_mm,nTets,k_pred_N_per_mm,F_first_N,F_collapse_N,"
              "initiating_mode,dominant_mode")
    line = ",".join(str(f) for f in fields)
    print()
    print(header)
    print(line)
    if args.append:
        new = not os.path.exists(args.append) or os.path.getsize(args.append) == 0
        with open(args.append, "a", encoding="utf-8") as f:
            if new:
                f.write(header + "\n")
            f.write(line + "\n")
        print("appended to %s" % args.append)


# =============================================================================
# calibrate
# =============================================================================
def cmd_calibrate(args):
    warnings = []
    warn = warnings.append
    by = group(read_rows(args.results))
    base = load_mat(args.base)
    p0, _ = props(base)

    def strengths(test):
        vals = []
        for r in by.get(test, []):
            F = fail_N(r)
            d1, d2 = dims(r)
            if F is None:
                continue
            if fixture_break(r):
                warn("%s: broke at the fixture, excluded" % label(r))
                continue
            if not (d1 and d2):
                warn("%s: missing dimensions, excluded" % label(r))
                continue
            vals.append(F / (d1 * d2))
        return vals

    def moduli(test):
        vals = []
        for r in by.get(test, []):
            k = stiffness(r, warn)
            d1, d2 = dims(r)
            if k:
                b, h = d1 or BAR_B, d2 or BAR_H
                vals.append(k[0] * BAR_L ** 3 / (3 * b * h ** 3 / 12))
        return vals

    found = {
        "fractureStress_intralayer": strengths("tension_flat"),
        "fractureStress_interlayer": strengths("tension_standing"),
        "E": moduli("bar_flat"),
        "E_z": moduli("bar_standing"),
    }
    E_lever = (sum(found["E"]) / len(found["E"])) if found["E"] else p0["E"]
    G_vals, tau_vals = [], []
    for r in by.get("twist_propped", []):
        d = dims(r)[0] or TW_D
        k = stiffness(r, warn)
        if k:
            c_lev = (TW_A - d / 2) ** 3 / (3 * E_lever * LEVER_T * LEVER_W ** 3 / 12)
            c = 1.0 / k[0] - c_lev
            if c <= 0:
                warn("%s: stiffer than the lever alone; check the reading" % label(r))
            else:
                G_vals.append(TW_A ** 2 * TW_LG / (math.pi * d ** 4 / 32 * c))
        F = fail_N(r)
        if F is not None:
            if fixture_break(r):
                warn("%s: broke at the fixture, excluded" % label(r))
            else:
                tau_vals.append(16 * F * TW_A / (math.pi * d ** 3))
    found["G_pz"] = G_vals
    found["fractureShear_interlayer"] = tau_vals
    found["fractureStress"] = found["fractureStress_intralayer"]

    order = ["name", "E", "nu", "density", "fractureStress", "E_z", "nu_pz", "G_pz",
             "fractureStress_intralayer", "fractureStress_interlayer",
             "fractureShear_interlayer"]
    sources = {"E": "bar_flat", "E_z": "bar_standing", "G_pz": "twist_propped",
               "fractureStress": "tension_flat",
               "fractureStress_intralayer": "tension_flat",
               "fractureStress_interlayer": "tension_standing",
               "fractureShear_interlayer": "twist_propped"}
    out, comments = {}, []
    print("%-26s %12s %4s %7s  %s" % ("key", "value MPa", "n", "CoV", "source"))
    for key in order:
        if key == "name":
            out[key] = args.name or "%s (calibrated)" % base.get("name", "material")
            continue
        vals = found.get(key)
        if vals:
            n, mean, sd, cov, _ = summarize(vals)
            out[key] = mean * 1e6
            src = sources[key]
            comments.append("# %s = %.4g MPa  (n=%d, CoV %.1f%%, from %s)"
                            % (key, mean, n, 100 * cov, src))
            print("%-26s %12.4g %4d %6.1f%%  %s" % (key, mean, n, 100 * cov, src))
            if n < 3:
                warn("%s: only %d valid specimen(s)" % (key, n))
            if cov > 0.15:
                warn("%s: CoV %.0f%% > 15%%; the print process looks unstable"
                     % (key, 100 * cov))
        elif key in base:
            out[key] = base[key]
            src = sources.get(key)
            note = "kept from %s" % args.base
            if src:
                note += " (no %s data)" % src
            comments.append("# %s: %s" % (key, note))
            raw = key in ("nu", "nu_pz", "density")
            shown = "%.4g" % (base[key] if raw else base[key] / 1e6)
            print("%-26s %12s %4s %7s  %s" % (key, shown, "-", "-",
                                              note + (" [not MPa]" if raw else "")))
    with open(args.out, "w", encoding="utf-8") as f:
        f.write("# Calibrated by tools/validation/validate.py on %s from %s\n"
                % (datetime.date.today().isoformat(), args.results))
        for c in comments:
            f.write(c + "\n")
        for key in order:
            if key in out:
                v = out[key]
                f.write("%s=%s\n" % (key, ("%.6g" % v) if isinstance(v, float) else v))
    print("\nwrote %s" % args.out)
    for w in warnings:
        print("warning: " + w)


# =============================================================================
# verdict
# =============================================================================
def read_predictions(path):
    preds = {}
    for r in read_rows(path):
        def num(key):
            v = r.get(key, "")
            return float(v) if v else None
        preds.setdefault(r.get("test", ""), []).append(dict(
            nTets=num("ntets") or 0, edge=r.get("edge_mm", ""),
            k=num("k_pred_n_per_mm"), F=num("f_collapse_n"),
            first=num("f_first_n"), mode=r.get("initiating_mode", "")))
    return preds


def scale(r, nom, exps):
    f = 1.0
    for v, n, e in zip(dims(r), nom, exps):
        if e and v and n:
            f *= (v / n) ** e
    return f


def ratio_line(name, ratios, tol, role):
    """Closure compares the mean only (the prediction was built from these
    specimens); validation uses the confidence-interval rule in judge()."""
    if not ratios:
        return "  %-10s no usable data" % name
    n, mean, sd, cov, half = summarize(ratios)
    if role == "closure":
        verdict = "PASS" if abs(mean - 1.0) <= tol else "FAIL"
    else:
        verdict = judge(mean, half, tol) if n > 1 else "INCONCLUSIVE"
    return ("  %-10s measured/predicted %.3f +/- %s (95%%, n=%d, CoV %.1f%%)  "
            "tol +/-%d%%  %-12s [%s]" % (name, mean, "%.3f" % half if n > 1 else "n/a",
                                          n, 100 * cov, round(100 * tol), verdict, role))


def cmd_verdict(args):
    warnings = []
    warn = warnings.append
    by = group(read_rows(args.results))
    preds = read_predictions(args.predictions) if args.predictions else {}
    p = props(load_mat(args.mat))[0] if args.mat else None

    for r in by.get("rig", []):
        k = stiffness(r, warn)
        if not k:
            continue
        b, h = dims(r)
        E = k[0] * RIG_L ** 3 / (3 * (b or RIG_B) * (h or RIG_H) ** 3 / 12)
        ok = abs(E / RIG_E - 1) <= 0.03
        print("rig %s: E = %.1f GPa vs %.0f GPa expected -> %s"
              % (r.get("id", ""), E / 1000, RIG_E / 1000,
                 "PASS" if ok else "FAIL: fix the rig before anything else"))

    for test, chk in CHECKS.items():
        rows = by.get(test, [])
        if not rows or test not in preds:
            if rows:
                print("\n%s: no prediction in %s" % (test, args.predictions))
            continue
        levels = sorted(preds[test], key=lambda q: q["nTets"])
        fine = levels[-1]
        print("\n%s  (prediction from the finest mesh, %s tets)" % (test, int(fine["nTets"])))
        if len(levels) > 1:
            prev = levels[-2]
            for key, lim, what in (("F", 0.05, "F_collapse"), ("k", 0.03, "stiffness")):
                if fine[key] and prev[key]:
                    d = abs(fine[key] - prev[key]) / fine[key]
                    print("  mesh check %s: %.1f%% change between the two finest meshes %s"
                          % (what, 100 * d, "(ok)" if d <= lim else
                             "(NOT converged: refine further)"))
        if fine["F"]:
            ratios, fixture = [], 0
            for r in rows:
                F = fail_N(r)
                if F is None:
                    continue
                if fixture_break(r):
                    fixture += 1
                    continue
                ratios.append(F / (fine["F"] * scale(r, chk["nom"], chk["expF"])))
            print(ratio_line("break", ratios, chk["tolF"], chk["roleF"]))
            if fixture:
                print("  (%d fixture break(s) excluded)" % fixture)
        if fine["k"] and chk.get("tolK"):
            ratios = []
            for r in rows:
                k = stiffness(r, warn)
                if k:
                    ratios.append(k[0] / (fine["k"] * scale(r, chk["nom"], chk["expK"])))
            print(ratio_line("stiffness", ratios, chk["tolK"], chk["roleK"]))
            if chk["roleK"] == "closure" and ratios:
                mean = sum(ratios) / len(ratios)
                if abs(mean - 1) > chk["tolK"]:
                    key = "E" if test == "bar_flat" else "E_z"
                    print("  closure fix: multiply %s by %.3f in your .mat and redo the "
                          "predictions" % (key, mean))
        pred_class = mode_class(fine["mode"])
        seen = [mode_class(r.get("mode")) for r in rows
                if r.get("mode") and not fixture_break(r)]
        if pred_class and seen:
            hits = sum(1 for s in seen if s == pred_class)
            print("  failure type: predicted %s, matched %d of %d (need %d)"
                  % (pred_class, hits, len(seen), math.ceil(0.8 * len(seen))))

    for test, L in COLUMNS.items():
        for r in by.get(test, []):
            if not r.get("loads") or not r.get("defl_mm"):
                continue
            P = [m * G for m in parse_list(r["loads"], parse_mass)]
            d = parse_list(r["defl_mm"], float)
            pts = [(x, x / f) for f, x in zip(P, d) if f > 0 and x > 0]
            if len(pts) < 3:
                warn("%s: need 3+ loaded points with deflection > 0" % label(r))
                continue
            slope, r2 = fit_line([a for a, _ in pts], [b for _, b in pts])
            line = "\n%s %s: Southwell P_cr = %.1f N (R^2 %.3f)" % (test, r.get("id", ""),
                                                                    1 / slope, r2)
            if p:
                b, t = dims(r)
                b, t = b or COL_B, t or COL_T
                Pe = math.pi ** 2 * p["E"] * (b * t ** 3 / 12) / (4 * L ** 2)
                line += ("; Euler with your E = %.1f N (ratio %.2f); the solver has no "
                         "buckling and would carry ~%.0f N" % (Pe, (1 / slope) / Pe,
                                                              p["s_in"] * b * t))
            print(line)
    for w in warnings:
        print("warning: " + w)


# =============================================================================
def main():
    ap = argparse.ArgumentParser(
        description="Companion tool for docs/validation/physical-test-plan.md.")
    sub = ap.add_subparsers(dest="cmd", required=True)

    e = sub.add_parser("estimate", help="closed-form loads, stiffness and recipes")
    e.add_argument("--mat", default="assets/materials/pla.mat")

    pr = sub.add_parser("predict", help="PolyFEA prediction for one test")
    pr.add_argument("--test", required=True, help=", ".join(SIM))
    pr.add_argument("--exe", required=True, help="path to FEAPreProcessor(.exe)")
    pr.add_argument("--geometry", help="simulation CAD (.step/.stp/.stl), in mm")
    pr.add_argument("--mat", help="material file")
    pr.add_argument("--scenario", help="custom base scenario instead of --geometry/--mat")
    pr.add_argument("--edge-mm", type=float, help="target element edge (default 1.5)")
    pr.add_argument("--lmax-mm", type=float, help="largest part dimension, if not as planned")
    pr.add_argument("--max-volume", type=float, help="raw mesh.maxVolume override")
    pr.add_argument("--guess", type=float, help="expected breaking load, N")
    pr.add_argument("--lo", type=float, help="search bracket, N")
    pr.add_argument("--hi", type=float, help="search bracket, N")
    pr.add_argument("--tol", type=float, default=0.01, help="relative bracket width")
    pr.add_argument("--max-iter", type=int, default=40, help="fracture iterations per run")
    pr.add_argument("--k-load", type=float, help="load for the stiffness run, N")
    pr.add_argument("--probe-mm", help="x,y,z from the part's bbox centre")
    pr.add_argument("--probe-axis", choices=["x", "y", "z"])
    pr.add_argument("--stiffness-only", action="store_true")
    pr.add_argument("--fracture-only", action="store_true")
    pr.add_argument("--gpu", action="store_true")
    pr.add_argument("--timeout", type=int, default=3600, help="seconds per run")
    pr.add_argument("--workdir", help="where scenarios, logs and reports go")
    pr.add_argument("--append", help="append the result line to this predictions.csv")

    c = sub.add_parser("calibrate", help="results.csv -> calibrated .mat")
    c.add_argument("results")
    c.add_argument("--base", default="assets/materials/pla.mat",
                   help="values you don't measure (nu, nu_pz, density) come from here")
    c.add_argument("--out", required=True)
    c.add_argument("--name")

    v = sub.add_parser("verdict", help="compare results with predictions")
    v.add_argument("results")
    v.add_argument("predictions", nargs="?")
    v.add_argument("--mat", help="calibrated .mat (for the buckling comparison)")

    args = ap.parse_args()
    try:
        {"estimate": cmd_estimate, "predict": cmd_predict,
         "calibrate": cmd_calibrate, "verdict": cmd_verdict}[args.cmd](args)
    except ValueError as err:
        sys.exit("error: %s" % err)


if __name__ == "__main__":
    main()
