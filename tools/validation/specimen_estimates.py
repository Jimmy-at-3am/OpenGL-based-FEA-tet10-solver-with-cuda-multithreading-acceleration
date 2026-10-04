#!/usr/bin/env python3
# =============================================================================
# specimen_estimates.py  --  closed-form sizing / sanity estimates for the
# physical validation campaign in docs/validation/physical-test-plan.md.
#
# For every specimen in the plan this prints the closed-form failure load (and
# torque where relevant), the elastic stiffness, and the 40 % load used for the
# elastic loading cycles, all from one .mat file. Two uses:
#
#   1. Sizing: run with the literature pla.mat and your weight budget before
#      printing; anything flagged OVER needs a smaller section (load scales
#      with the section terms shown in the plan) or a lever.
#   2. Verification: run with your calibrated .mat and compare against the
#      PolyFEA predictions. A sim/closed-form gap larger than ~5 % in a
#      uniform-stress region is a solver/setup problem, not a material one.
#
# These are idealized textbook formulas (beam, Winkler curved beam, elastic
# torsion, Euler). They are NOT the predictions being validated -- PolyFEA's
# are. Geometry constants below must match the plan; edit them together.
#
# Run:  python tools/validation/specimen_estimates.py --mat assets/materials/pla.mat --budget-kg 25
# =============================================================================
import argparse
import math

G = 9.81  # m/s^2

# ---- geometry (mm), keep in sync with docs/validation/physical-test-plan.md --
R0 = dict(b=20.0, h=3.0, L=150.0, span3p=150.0, E_metal=69000.0)  # aluminium flat bar
C1 = dict(w=3.0, t=1.6)                     # flat dogbone gauge
C2 = dict(d=3.0)                            # standing bobbin gauge diameter
C3 = dict(d=6.0, L=40.0, drum_r=30.0)       # standing torsion bar
BAR = dict(b=10.0, h=5.0, L=80.0)           # V1/V2/V5 3-point-bend bar, span L
V3 = dict(R=15.0, h=4.0, b=6.0)             # C-ring: mean radius, radial thk, width
V4 = dict(d=8.0, Lg=15.0, a=80.0, z=25.0)   # lever-twist: shaft d, gauge, lever arm, load height
V5 = dict(notch=1.5)                        # notch depth on the BAR tension face
COLS = dict(b=6.0, t=3.0, lengths=(100.0, 140.0, 180.0))


def load_mat(path):
    """Parse a PolyFEA .mat file the same way ScenarioRunner does."""
    vals = {}
    with open(path, encoding="utf-8") as f:
        for line in f:
            if line.startswith("#") or "=" not in line:
                continue
            k, v = (s.strip() for s in line.split("=", 1))
            try:
                vals[k] = float(v)
            except ValueError:
                vals[k] = v
    return vals


def props(m):
    """MPa-based property set with explicit, printed fallbacks."""
    notes = []
    E = m["E"] / 1e6
    nu = m.get("nu", 0.35)
    Ez = m.get("E_z", 0.0) / 1e6
    if Ez <= 0:
        Ez = E
        notes.append("E_z missing -> using E")
    Gpz = m.get("G_pz", 0.0) / 1e6
    if Gpz <= 0:
        Gpz = E / (2 * (1 + nu))
        notes.append("G_pz missing -> using E/2(1+nu)")
    if "fractureStress" not in m:
        notes.append("fractureStress missing -> PolyFEA isotropic fracture "
                     "silently uses its 250 MPa default; add it to the .mat")
    s_iso = m.get("fractureStress", 0.0) / 1e6
    s_in = m.get("fractureStress_intralayer", 0.0) / 1e6 or s_iso
    s_z = m.get("fractureStress_interlayer", 0.0) / 1e6 or s_in
    t_z = m.get("fractureShear_interlayer", 0.0) / 1e6
    if t_z <= 0:
        t_z = s_z / math.sqrt(3)
        notes.append("fractureShear_interlayer missing -> using interlayer/sqrt(3)")
    return dict(E=E, Ez=Ez, G=Gpz, s_in=s_in, s_z=s_z, t_z=t_z), notes


def winkler(R, h, b):
    """Outer-fiber tension and inner-fiber compression (MPa per N of load) at
    the back of a C-ring whose critical section carries M = P*R and N = -P."""
    ri, ro, A = R - h / 2, R + h / 2, b * h
    rn = h / math.log(ro / ri)
    e = R - rn
    outer = R * (ro - rn) / (A * e * ro) - 1 / A
    inner = R * (rn - ri) / (A * e * ri) + 1 / A
    return outer, inner


def rows(p):
    E, Ez, Gp = p["E"], p["Ez"], p["G"]
    out = []

    # R0 -- rig check, metal bar (non-destructive)
    I = R0["b"] * R0["h"] ** 3 / 12
    k_cant = 3 * R0["E_metal"] * I / R0["L"] ** 3
    k_3p = 48 * R0["E_metal"] * I / R0["span3p"] ** 3
    out.append(("R0a", "metal cantilever (rig check)", None, k_cant, None,
                "%.2f mm per kg at the tip" % (G / k_cant)))
    out.append(("R0b", "metal 3-point bend (rig check)", None, k_3p, None,
                "%.2f mm per kg at mid-span" % (G / k_3p)))

    # C1/C2 -- tension coupons (calibration)
    A1 = C1["w"] * C1["t"]
    out.append(("C1", "flat dogbone, intralayer tension", p["s_in"] * A1, None,
                None, "F = sigma_in * w * t"))
    A2 = math.pi * C2["d"] ** 2 / 4
    out.append(("C2", "standing bobbin, interlayer tension", p["s_z"] * A2,
                None, None, "F = sigma_z * pi d^2/4"))

    # C3 -- pure torsion (calibration of G_pz and tau_z)
    d, L = C3["d"], C3["L"]
    J = math.pi * d ** 4 / 32
    T = p["t_z"] * math.pi * d ** 3 / 16
    twist40 = math.degrees(0.4 * T * L / (Gp * J))
    out.append(("C3", "standing torsion bar, interlayer shear",
                T / C3["drum_r"], None, T,
                "%.1f deg twist at 40%%; drum r=%g mm" % (twist40, C3["drum_r"])))

    # V1/V2 -- 3-point bend, same bar, two print orientations
    b, h, Lb = BAR["b"], BAR["h"], BAR["L"]
    Ib = b * h ** 3 / 12
    for vid, desc, EE, ss in (("V1", "3PB bar on edge, intralayer", E, p["s_in"]),
                              ("V2", "3PB bar standing, interlayer", Ez, p["s_z"])):
        F = 2 * ss * b * h ** 2 / (3 * Lb)
        out.append((vid, desc, F, 48 * EE * Ib / Lb ** 3, None,
                    "F = 2 sigma b h^2 / 3L; k excludes ~1-2% shear defl."))

    # V3 -- C-ring in compression (curved beam, statically determinate back)
    so, si = winkler(V3["R"], V3["h"], V3["b"])
    Ic = V3["b"] * V3["h"] ** 3 / 12
    F_t = p["s_in"] / so
    F_vm = p["s_in"] / si
    out.append(("V3", "C-ring compression, outer-fiber tension", F_t,
                2 * E * Ic / (math.pi * V3["R"] ** 3), None,
                "symmetric von Mises would flag inner fiber at %.0f N" % F_vm))

    # V4 -- lever-twist: torque F*a plus bending F*z at the shaft root
    d, a, z = V4["d"], V4["a"], V4["z"]
    tau_per = 16 * a / (math.pi * d ** 3)
    sig_per = 32 * z / (math.pi * d ** 3)
    F_tau = p["t_z"] / tau_per
    F_sig = p["s_z"] / sig_per
    F_int = 1 / math.sqrt((sig_per / p["s_z"]) ** 2 + (tau_per / p["t_z"]) ** 2)
    F4 = min(F_tau, F_sig)
    mode = "interlayer shear" if F_tau <= F_sig else "interlayer tension"
    J4 = math.pi * d ** 4 / 32
    k_twist = 1 / (a * a * V4["Lg"] / (Gp * J4))  # shaft twist only, lower bound on compliance
    out.append(("V4", "lever-twist shaft, " + mode, F4, k_twist, F4 * a,
                "k = shaft twist only (upper bound); with sigma-tau interaction F=%.1f N" % F_int))

    # V5 -- notched 3PB: bracket between full notch sensitivity and net section
    hn = h - V5["notch"]
    F_net = 2 * p["s_in"] * b * hn ** 2 / (3 * Lb)
    out.append(("V5", "notched 3PB, intralayer", F_net, None, None,
                "net-section bound; Kt~2 -> fully notch-sensitive %.0f N" % (F_net / 2)))

    # V7 -- slender pinned columns (buckling gap)
    Icol = COLS["b"] * COLS["t"] ** 3 / 12
    crush = p["s_in"] * COLS["b"] * COLS["t"]
    for Lc in COLS["lengths"]:
        Pcr = math.pi ** 2 * E * Icol / Lc ** 2
        out.append(("V7-%d" % Lc, "pinned column, Euler buckling", Pcr, None,
                    None, "linear solver sees no failure until ~%.0f N" % crush))
    return out


def main():
    ap = argparse.ArgumentParser(
        description="Closed-form sizing/verification estimates for the "
                    "physical validation campaign.")
    ap.add_argument("--mat", default="assets/materials/pla.mat")
    ap.add_argument("--budget-kg", type=float, default=None,
                    help="largest total mass you can hang; flags specimens "
                         "whose estimate exceeds 70%% of it")
    args = ap.parse_args()

    p, notes = props(load_mat(args.mat))
    print("material: %s" % args.mat)
    print("  E=%.0f  E_z=%.0f  G_pz=%.0f MPa | sigma_in=%.1f  sigma_z=%.1f  tau_z=%.1f MPa"
          % (p["E"], p["Ez"], p["G"], p["s_in"], p["s_z"], p["t_z"]))
    for n in notes:
        print("  note: " + n)
    print()
    hdr = "%-7s %-42s %9s %7s %9s %10s %9s  %s" % (
        "id", "specimen / governing mode", "F_fail N", "kg", "torque", "k N/mm",
        "40% N", "remark")
    print(hdr)
    print("-" * len(hdr))
    for rid, desc, F, k, T, remark in rows(p):
        Fs = "%9.1f" % F if F is not None else "%9s" % "-"
        kg = "%7.2f" % (F / G) if F is not None else "%7s" % "-"
        Ts = "%7.0f Nmm" % T if T is not None else "%9s" % "-"
        ks = "%10.2f" % k if k is not None else "%10s" % "-"
        f40 = "%9.1f" % (0.4 * F) if F is not None else "%9s" % "-"
        flag = ""
        if args.budget_kg and F is not None and F / G > 0.7 * args.budget_kg:
            flag = "  OVER 70% OF BUDGET"
        print("%-7s %-42s %s %s %s %s %s  %s%s" % (rid, desc, Fs, kg, Ts, ks, f40,
                                                   remark, flag))


if __name__ == "__main__":
    main()
