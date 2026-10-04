# Physical Validation Test Plan — Milestone 1

- **Status:** Proposed — not yet executed
- **Scope:** The milestone-1 solver envelope: small-strain linear-elastic statics
  (Tet4/Tet10), transversely isotropic FDM material, and brittle element-deletion
  fracture with the three FDM failure modes (interlayer tension, interlayer shear,
  in-plane von Mises)
- **Equipment class:** Dead weights, a scale, a dial indicator, a 3D printer
- **Companion tool:** `tools/validation/specimen_estimates.py` (closed-form sizing
  and verification numbers for every specimen below)
- **Last reviewed:** 2026-10-04

## 1. Summary

This plan answers two questions with physical evidence:

1. **Deformation:** does PolyFEA predict how stiff a printed part is?
2. **Fracture:** does it predict the force or torque at which the part breaks,
   where it breaks, and whether the break is between layers or through them?

A mismatch between simulation and test is only useful if you can tell *why* it
happened. The plan therefore keeps three error sources apart:

| Error source | What goes wrong | How the plan isolates it |
|---|---|---|
| **Test rig** | Clamp compliance, slipping grips, mislabeled weights | Metal control bar of known modulus (R0) before any plastic is tested |
| **Solver** | Units, boundary conditions, mesh, element formulation | Closed-form cross-checks, calibration closure, mesh convergence (§8) |
| **Material model** | Linear-elastic, brittle, max-stress, tension/compression-symmetric in-plane criterion | Blind predictions on new geometries and stress states (V-series) |

Material constants come from coupons printed on **your** printer with **your**
spool (C-series), never from the literature values in `assets/materials/pla.mat`.
Comparing against literature constants would mostly measure the difference
between your printer and the one in the cited paper.

### Test matrix

| ID | Specimen | Load | Role | Validates | n |
|---|---|---|---|---|---|
| R0 | Aluminium flat bar | Cantilever + 3-point bend | Rig check | Your rig and the solver on a known material | 1 bar |
| C1 | Flat dogbone | Tension | Calibration | In-plane strength `fractureStress_intralayer` | 5 |
| C2 | Standing bobbin | Tension | Calibration | Interlayer strength `fractureStress_interlayer` | 5 |
| C3 | Standing round bar | Pure torsion | Calibration | `G_pz`, interlayer shear strength `fractureShear_interlayer` | 5 |
| V1 | Bar printed on edge | 3-point bend | Calibration (E) + validation | In-plane bending fracture | 5 |
| V2 | Same bar, printed standing | 3-point bend | Calibration (E_z) + validation | Interlayer bending fracture, standing-vs-lying ratio | 5 |
| V3 | C-ring | Compression | Validation | Curved-beam stiffness and fracture, tension-vs-compression criterion | 5 |
| V4 | Shaft with lever arm | Torque + bending | Validation | Fracture torque, interlayer shear mode, torsional stiffness | 5 |
| V5 | Notched V1 bar | 3-point bend | Validation | Stress concentration and mesh sensitivity of element deletion | 5 |
| V6 | Sparse-infill bar | 3-point bend | Validation | G-code toolpath meshing on a realistic print | 5 |
| V7 | Slender columns, 3 lengths | Compression | Gap characterization | Buckling — the solver cannot predict it yet | 3×3 |

Run the tiers in order:

- **Tier A:** R0, C1, C2, V1, V2, V3. About 30 prints including spares, and
  roughly a weekend of testing. Answers tension, bending, and compression for
  both stiffness and fracture.
- **Tier B:** C3, V4, V5. Adds twisting and stress concentrations.
- **Tier C:** V6, V7. Adds the sparse-infill pipeline and documents the
  buckling gap.

## 2. What milestone 1 can and cannot be validated on

The test types you asked for map onto the solver like this:

| You asked for | Physical form here | PolyFEA path | Comparable today? |
|---|---|---|---|
| Tension | Dogbones (C1, C2) | `pullX` / `pullZ` (FacePull) | Yes |
| Bending | 3-point bend bars (V1, V2, V5, V6) | `bend3p<axis><dir>` | Yes |
| Compression ("pressure") | C-ring squeezed between platens (V3) | `surfaceCompY` | Yes — the ring breaks in tension, which the model covers |
| Twisting | Lever-loaded shaft (V4); pure-torsion bar (C3) | `bendZY` (FaceBend on the lever pad); no pure-torque preset | Partly — V4 is simulated; C3 is analyzed by formula |
| Buckling | Pinned slender columns (V7) | `surfaceCompY` | **No** — documented as a gap |

**Why not crush a solid block?** Solid PLA yields and barrels in compression
rather than fracturing, and the milestone-1 model is brittle and linear. Crushing
even a 10×10 mm block needs roughly 600 kg. V3 gives a compression-loaded test that
fails by fracture, at loads you can hang.

**Why buckling cannot be predicted yet.** `Tet10Element::ComputeTangentStiffness`
ignores the displacement argument. There is no geometric (initial-stress)
stiffness and no eigenvalue buckling, so Newton-Raphson reduces to the linear
solve. A slender column in PolyFEA only fails when its compressive stress
reaches the fracture limit — about 900 N for the V7 columns, which really buckle
at 14–47 N. Test it anyway: the measured gap becomes the acceptance target for
whichever milestone adds buckling.

**Why twisting uses a lever.** No preset applies a torque or remote moment
(`docs/design/physics-safe-load-setup.md` lists those as later work). In V4, a
single force on a pad at the end of an 80 mm lever arm produces the torque. In
the solver, `bendZY` applies that same force to the same pad, so the bench and
the simulation see an identical load. C3's pure-torsion data stays usable for
validation once a torque load exists.

## 3. Validation logic and ground rules

```
R0 rig check ──► C-series + elastic part of V1/V2 ──► calibrated .mat
     │                                                    │
     │                     closure check (sim reproduces C-series) ◄┘
     │                                  │
     │                mesh convergence + closed-form cross-check
     │                                  │
     │               predictions committed to git  ◄── no V-specimen broken before this
     │                                  │
     └──────────────────────────► V-series tests ──► comparison & verdict (§9)
```

1. **Never tune on validation data.** Material constants come only from the
   C-series and from the *elastic* part of V1/V2. Freeze `tieAlpha`, mesh settings,
   and every solver option before predicting.
2. **Pre-register.** Before breaking any V-specimen, commit the predictions
   file, the calibrated `.mat`, the scenario files, and the PolyFEA git hash
   (§10).
3. **One print campaign.** Use one printer, nozzle, spool lot, and slicer profile
   for every specimen, printed in the same week. Archive each plate's
   `.gcode.3mf`; it is also the input for the toolpath-mesh simulations.
4. **Classify every mismatch** with the diagnosis table (§9.4) before changing
   anything.

## 4. Equipment

**Minimum**

- Your weights, each weighed and labeled with its *measured* mass. Don't trust
  the nominal value.
- A hanger and a bucket. Filling the bucket with sand, water, or steel shot gives
  a smooth final ramp to failure.
- Your scale, used to weigh weights and the bucket after each break. Check it
  against a reference first; for example, 1.000 L of water at 20 °C weighs 998 g.
- A dial indicator (0.01 mm resolution, at least 10 mm travel) with a magnetic
  base or clamp mount.
- Digital calipers.
- A rigid hanging point rated for at least 3× your largest load, such as a beam,
  shelf bracket, or pull-up bar with an eye bolt. Also a bench vise and C-clamps.
- Steel rods, Ø6–10 mm, for 3-point-bend rollers and the loading nose. Dowel pins,
  drill blanks, or smooth bolt shanks all work.
- One 608 skateboard bearing for the torsion drum (C3).
- A phone to video every fracture and photograph every fracture surface.
- Safety glasses, and a foam pad or folded towel under every hanging load.

**Strongly recommended upgrade.** An HX711 board with a 5 kg or 20 kg load cell
(about $10, Arduino) logs force continuously and catches the true peak at
fracture. Use it in series with the hanger, or under the specimen in the lever
press.

**Aluminium bar for R0.** Get a 20×3 mm flat bar, about 300 mm long, from a
hardware store. Aluminium alloys have E = 68–70 GPa. Steel works too, at
200–210 GPa. Measure the thickness at 5 points: stiffness scales with h³, so a
1 % thickness error becomes a 3 % stiffness error.

## 5. Rig check R0 — do this first

R0 tests your weights, indicator mounting, and clamp on a material whose modulus
is known, before any plastic is involved.

| | R0a cantilever | R0b 3-point bend |
|---|---|---|
| Setup | Bar clamped in the vise, 150 mm free length measured from the jaw edge | Same bar on the 3-point-bend fixture (§7.2), 150 mm span |
| Load | 0.25 / 0.5 / 0.75 / 1.0 kg, hung from a string in a notch at 150 mm | Up to 4 kg on the stirrup |
| Measure | Tip deflection, plus a second indicator 10 mm from the jaw to detect clamp rotation | Mid-span deflection |
| Expected (Al, E = 69 GPa) | 3.55 mm/kg | 0.22 mm/kg |
| Pass | Measured stiffness within ±3 % of FL³/3EI, using measured b, h, L | Within ±3 % of FL³/48EI |

Simulate R0 too. This exercises the full solver path on an isotropic material
whose answer is known exactly:

```json
{
  "v": 1,
  "geometry": { "preset": "box", "size": [150.0, 20.0, 3.0] },
  "material": "materials/aluminum.mat",
  "mesh": { "maxVolume": 0.2, "quality": 1.6 },
  "loads": [ { "type": "preset", "name": "bendXZ", "mag": -9.81 } ],
  "solve": { "kind": "linear", "gpu": false },
  "probes": [ { "id": "tip", "posMM": [75.0, 0.0, 1.5], "quantity": "displacement" } ],
  "asserts": [ { "path": "solver.ok", "op": "==", "value": 1 } ]
}
```

Read `probes.tip.vector[2]` (mm) from `report.json` and compare it to FL³/3EI
with E = 70 GPa from `aluminum.mat`, which gives 3.50 mm. The result should be
within 2 %; refine `maxVolume` until it is. The runner's asserts can't index
arrays, so make this comparison by hand or with a script. `bendXZ` clamps the
X-min face and spreads the force evenly over the X-max face, which matches a tip
load hung at 150 mm.

If R0 fails, fix the rig before going further. Common causes are clamp rotation
(seen on the root indicator), the indicator stand flexing, the indicator's spring
force (§7.1), or a mislabeled weight.

## 6. Specimens

### 6.1 Print protocol (all specimens)

- One printer, one nozzle, one spool lot, dried filament. Record and archive the
  full slicer profile and every plate's `.gcode.3mf`.
- Use 0.2 mm layers, 2 walls, and 100 % rectilinear infill at ±45° for all
  specimens except V6. Alternating ±45° keeps the solid core roughly in-plane
  isotropic, which matches the transversely isotropic model.
- **Standing prints:** print at least 5 at once, spaced 20 mm or more apart, and
  set a minimum layer time of 8 s or more. Use a brim. A tiny cross-section
  printed alone does not cool between layers, and its interlayer strength won't
  match your real parts.
- Spread each series across plates and plate positions. Write IDs on a grip or
  head, never on the gauge section.
- Condition at room temperature for at least 48 h. Test everything within one
  2-week window and log temperature and humidity for every test.
- Before testing, measure each specimen's critical dimensions (3 readings) and
  mass. Mass divided by volume flags under-extrusion: a solid PLA part well below
  about 1.20 g/cm³ has voids. Reject visibly defective specimens before testing
  and log the rejects.
- Print one extra per series. Use it to shake down the fixture; it doesn't count
  toward the results.

### 6.2 Specimen definitions

"Estimate" is the closed-form value from `specimen_estimates.py` with the
**literature** `pla.mat`. These values are for sizing only. After calibration,
rerun the script with your `.mat` and treat PolyFEA's output as the prediction.

| ID | Geometry (mm) | Print orientation | Estimate |
|---|---|---|---|
| C1 | Dogbone: gauge 3.0 w × 1.6 t × 30 long, R25 transitions to 15-wide × 25-long tabs, about 112 overall | Flat | 240 N (24.5 kg) |
| C2 | Bobbin: gauge Ø3.0 × 12 long, R6 fillets to Ø12 × 20 heads, Ø4 cross-hole 10 mm from each end | Standing | 161 N (16.4 kg) |
| C3 | Round bar: gauge Ø6 × 40 long, R3 fillets to 14×14×20 square heads | Standing | 0.76 N·m, which is 2.6 kg on a 30 mm drum; 6.9° twist at 40 % |
| V1 | Bar 100 long, 5 deep (Y) × 10 wide (Z), so it stands 10 mm tall on the bed | On edge | 104 N (10.6 kg); 34 N/mm |
| V2 | Same bar as V1, printed standing (100 mm tall): 5 deep along X, 10 wide along Y | Standing | 48 N (4.8 kg); 23 N/mm |
| V3 | C-ring: mean radius 15, radial thickness 4, width 6 (print height); 6 mm gap at +X; 0.8 mm flats at ±Y | Flat | 61 N (6.2 kg) if it cracks on the outer tension fiber; k ≈ 21 N/mm |
| V4 | Ø8 shaft, 15 gauge, R2 fillets; 30×30×12 base block; lever arm 12 (Y) × 8 (Z) reaching 80 mm from the shaft axis; 6×6×2 pad on the lever tip with a Ø2 hole along X 1 mm below the pad top | Standing, supports under the lever only; keep supports off the gauge section | 22.6 N (2.3 kg), which is 1.81 N·m; interlayer-shear mode |
| V5 | V1 bar with a mid-span U-notch on the tension face: 1.5 deep, 0.75 root radius, running through the 10 mm width so its root is printed as perimeters | On edge | Between 26 N (fully notch-sensitive, K_t ≈ 2) and 51 N (net section) |
| V6 | Bar 120 × 15 (Y) × 6 (Z); 2 walls; 3 top and 3 bottom layers; 15 % gyroid | Flat | About 110 N by skins-and-walls hand calculation; size it with the simulation |
| V7 | Columns 6 × 3 section; 100 / 140 / 180 pin-to-pin, axis along Y on the bed, ends ground or printed to a 60° knife edge parallel to the 6 mm side | Flat | Euler 47 / 24 / 14 N |

**Sizing rule.** If your weights can't reach about 1.4× an estimate, shrink the
section. Tension load scales with w·t, bending load with b·h², torsion with d³,
and C-ring load with b·h². Then rerun the script with your geometry; the
constants at the top of the script must match the plan.

**Geometry rationale:**

- **C1 and C2** use dogbone and bobbin shapes so the break happens in the gauge,
  away from the grips.
- **C3** uses a 40 mm gauge (L/d ≈ 7) so end effects stay small in the twist
  reading.
- **V1 and V2** are the same bar in two orientations. Their failure-load ratio
  directly tests PolyFEA's standing-versus-lying fracture ordering, and the
  ratio cancels much of the rig error.
- **V3:** the moment at the ring's back section is exactly P·R by statics, no
  matter how the platens or the clamp band are modeled. The critical stress is
  therefore insensitive to load-introduction modeling. A curved beam's inner
  fiber also carries more stress than its outer fiber, so a tension/compression-
  symmetric criterion predicts the wrong crack location (see H4 in §11).
- **V4:** the lever is long relative to the load height (a/2z = 1.6), so
  interlayer shear governs with a 2× margin over interlayer tension.
- **V5** is V1 plus a notch, so the notched-to-plain strength ratio comes from
  the same fixture.

### 6.3 Calibration reductions

These turn the C-series and the elastic part of V1/V2 into `.mat` values. Use
each specimen's measured dimensions, then average over the series and record
the standard deviation with the mean. A constant with CoV above 15 % points to
an unstable print process (§9.2). `.mat` values are in SI units (Pa).

| `.mat` key | Source | Formula (N, mm, MPa) |
|---|---|---|
| `fractureStress_intralayer`, and `fractureStress` set to the same value | C1 | σ = F_fail / (w·t) |
| `fractureStress_interlayer` | C2 | σ = 4·F_fail / (π·d²) |
| `fractureShear_interlayer` | C3 | τ = 16·T_fail / (π·d³) |
| `G_pz` | C3 elastic slope dT/dθ (θ in rad) | G = (dT/dθ)·L_g / J, with J = π·d⁴/32 |
| `E` | V1 elastic slope k | E = (L³/48I) / (1/k − L/(4κ·G·A)), with I = b·h³/12, A = b·h, κ = 5/6, G ≈ E/2.7 (iterate once) |
| `E_z` | V2 elastic slope k | Same formula, with G = `G_pz` from C3, or from the literature file if C3 (Tier B) isn't done yet. The shear term is about 1–2 % |

Keep `nu` and `nu_pz` from the literature file.

## 7. Test procedures

### 7.1 Common protocol (every specimen)

1. **Log the specimen:** ID, dimensions (3 readings at the critical section), mass,
   photo, room temperature and humidity.
2. **Mount and align.** Check the specimen is square and centered. Apply about
   5 % of the expected failure load to seat it, then zero the indicator.
3. **Elastic cycles** (every test that measures deflection or twist, so not C1
   or C2). Load in 4 steps up to about 40 % of the expected failure
   load (the committed prediction for V-specimens, the estimate for C-specimens).
   Set weights down gently; never drop them on. Read the indicator exactly 10 s
   after each step, because PLA creeps and consistent timing makes calibration
   and validation comparable. Unload and read the residual. Repeat the whole
   cycle once and keep only the second cycle.
   - Stiffness k is the least-squares slope of force against deflection over the
     2nd-cycle points.
   - The data are only usable if R² ≥ 0.995 and the residual after unloading is
     at most 2 % of the peak deflection. Otherwise the specimen is slipping or
     seating — fix the mounting before going on.
   - **Correct for the indicator's spring force.** Press the plunger onto your
     scale at mid-travel to measure it; it is typically 0.5–1.5 N. It acts on
     the specimen like an extra load: subtract it when the plunger pushes
     against the load direction (indicator underneath in 3-point bending), and
     add it when it pushes along the load. This matters most for V2 and V4.
4. **Retract the indicator** before going to fracture.
5. **Fracture ramp.**
   - Add weights quickly up to about 70 % of the expected failure load.
   - Then pour sand or water into the bucket at roughly 0.3 % of the expected
     failure load per second. Failure should come 60–180 s into the ramp.
   - Never hold a load near failure: PLA creep-ruptures.
   - Video the specimen, and the scale display wherever a scale reads the force.
6. **After the break.** Weigh everything that was hanging: bucket and contents,
   hanger, stirrup, and weights. F_fail = m·g, times the lever factor where a
   lever is used. Log the time to failure. If you run out of weight, log a
   run-out of "≥ F" and unload.
7. **Fracture surface.** Photograph it, locate the crack origin (in mm from a
   stated reference), and classify the mode:
   - **interlayer:** flat and smooth, follows one layer line, perpendicular to
     the build axis
   - **intralayer:** rough, beads torn through, crosses layer lines
   - **mixed**
   - **fixture-induced:** at a grip, roller, or nose. This makes a C-specimen
     invalid; for a V-specimen it is a result to record.
   Slow-motion video usually shows where the crack started.

### 7.2 Fixtures

**3-point bend by dead weight (R0b, V1, V2, V5, V6).** The specimen rests on two
rollers sitting on separate blocks, leaving the middle open below. A stirrup
carries the loading nose on top of the specimen, and its legs pass outside the
specimen to a hook underneath.

```
             Ø10 loading nose (part of the stirrup)
        ┌────────────●────────────┐
        │  ══════════════════════ │ specimen
        │   ○                  ○  │ Ø6–10 rollers, span L ±0.2 mm
        │  ███                ███ │ blocks, open in the middle
        └───────────┬─────────────┘
                    │  hook        dial indicator under mid-span,
                   [W] weights      beside the stirrup legs
```

- Measure the span between roller centers. Center the nose to within ±0.5 mm.
- Include the stirrup's own mass in the load.

**Tension (C1, C2).** Hang the specimen vertically and connect it through
shackles or pins at both ends so it self-aligns.

- C1 grips: each tab is clamped between two plates (aluminium or plywood) faced
  with 120-grit sandpaper. Two M4 bolts per grip pass *beside* the tab, not
  through it, and a hanging pin goes through the plates beyond the tab end.
- C2: pins go through the head cross-holes.
- No extensometer is needed. Moduli come from bending (V1/V2), and the
  C-series measures strength only.
- **Valid only if** the break is in the gauge or the transition.

**Pure torsion drum (C3).**

```
 vise ║▐■▌══════ Ø6 gauge ══════▐■▌ drum r=30 ─┤608 bearing on a stand
                                       │ string, tangent to the drum
                                      [W]
```

- One square head is held in the vise; the other sits in a printed drum with a
  square socket.
- The drum runs on an 8 mm stub inside the 608 bearing. The bearing takes the
  string's pull, so the bar sees torque only.
- Shim the bearing stand until it is coaxial with the vise-held head, to within
  0.5 mm.
- Twist θ = δ/r_m, where δ is the dial reading on the drum rim at radius r_m.
- Torque T = m·g·(drum radius + string radius).

**Lever press (V3, V7).**

```
 pivot ●━━━━━━━━━━━━━━━━━━━━━━━━━━━━━ bucket
        ← a →↓ steel ball on a flat platen
             [ specimen ]
        ═══════════════════ base (or the scale)
```

- Calibrate the lever once: put the scale under the specimen position and hang
  3–4 known masses. Fit F_specimen = α·m_hung + β; the β term absorbs the
  lever's own weight.
- Above the scale's capacity, use the fitted line. The lever is geometric, so
  the fit stays linear.
- Put a stop under the lever so it drops no more than about 10 mm at fracture.

**V4 lever-twist.**

- Hold the base block in the vise with the shaft horizontal and the lever
  horizontal; check both with a phone level.
- Hang the weight from a string through the pad's Ø2 hole.
- Read the vertical deflection on the pad.
- Failure torque = F·a, where a is the measured horizontal distance from the
  shaft axis to the string.

**V7 columns.**

- Seat the knife-edge ends in 90° V-grooves, cut in metal or printed. The edges
  must lie along the 6 mm side so the column pivots freely about its weak axis.
- Measure lateral mid-height deflection **without contact**: photograph the
  column against a mm grid at each load step. An indicator's 1 N spring force is
  a large lateral push on a 14 N column.
- Load in small steps until deflection grows with no further load increase; that
  plateau is P_max.
- Southwell plot: deflection/P against deflection is a straight line with slope
  1/P_cr. It gives P_cr without forcing the column to collapse.

## 8. Simulation protocol

### 8.1 Inputs

- **Geometry:**
  - A STEP file in mm of the as-designed specimen. This is the solid mesh, Tet10
    for linear solves. Orient it in the **print frame** (Z = build axis), because
    `buildAxis` and the toolpath mesh live in that frame.
  - For V6, also the printed `.gcode.3mf`, meshed with `mesh.method: "toolpath"`.
- **Material:** create `pla_<spool>_cal.mat` with `E`, `E_z`, `G_pz`,
  `fractureStress_intralayer`, `fractureStress_interlayer`, and
  `fractureShear_interlayer` from the C-series and the elastic part of V1/V2.
  - **Always include `fractureStress`.** Isotropic fracture otherwise silently
    falls back to the runner's 250 MPa steel default.
  - `nu` and `nu_pz` stay at literature values; these tests are insensitive to
    them.
- **Frozen settings:** `tieAlpha` = 100 (the regression value). For V6, set
  `maxSlabs` ≥ the printed layer count so each FE slab is one layer (k = 1); a
  coarser grouping smears the 3 skin layers into the infill. Fix all mesh levels
  before predicting.

### 8.2 Preset mapping

Every validation scenario uses `"solve": { "fdmAnisotropy": true, "buildAxis": 2 }`.
The support condition comes with the load preset.

| Test | Geometry frame | `loads[0].name` | Notes | Probe (posMM is from the part's bbox center) |
|---|---|---|---|---|
| C1 closure | Dogbone along X | `pullX` | Clamps the X-min tab end | — (compare F_collapse to C1) |
| C2 closure | Bobbin along Z | `pullZ` | | — |
| V1, V5 | Bar along X, depth along Y | `bend3pXY`, `spanMM: 80` | Supports on Y-min | Bottom mid-span, 2 mm beside the notch for V5; use the Y component |
| V2 | Bar along Z, depth along X | `bend3pZX`, `spanMM: 80` | | Bottom mid-span; use the X component |
| V3 | Ring in XY plane, gap at +X | `surfaceCompY` | Clamps the Y-min 5 % band and loads the Y-max band in −Y. Each band covers its flat plus a few mm of curved outer surface, which slightly changes local stiffness but not the moment at the critical back section | Top flat center; use the Y component |
| V4 | Shaft along Z, lever along +X, pad top = Z-max face | `bendZY` | Clamps the base bottom, loads only the pad; the pad stands 2 mm proud so the 0.5 % face band selects it alone | Pad center; use the Y component |
| V6 | Toolpath mesh, bar along X, depth along Z | `bend3pXZ`, `spanMM: 100` | Needs `geometry.gcode3mf`, `"gcodeSections": {}`, `mesh.method: "toolpath"`, and `"quadratic": false` (the toolpath lane is Tet4). Also run the solid STEP model as a contrast | Bottom mid-span; use the Z component |
| V7 | Column along Y | `surfaceCompY` | Expect no failure until about 50 MPa compressive stress | — |

Example (V1, stiffness run at the 40 % load). Use **absolute paths**: the runner
switches the working directory to the executable's data directory. Keep
validation scenarios **out of `scenarios/`**, because `--regress all` runs every
file there.

```json
{
  "v": 1,
  "geometry": { "step": "C:/polyfea-validation/print/v1_bar_on_edge.step" },
  "material": "C:/polyfea-validation/materials/pla_spoolA_cal.mat",
  "mesh": { "maxVolume": 0.0000055, "quality": 1.6 },
  "loads": [ { "type": "preset", "name": "bend3pXY", "mag": 40.0, "spanMM": 80.0 } ],
  "solve": { "kind": "linear", "gpu": false, "fdmAnisotropy": true, "buildAxis": 2 },
  "probes": [ { "id": "midBottom", "posMM": [0.0, -2.5, 0.0], "quantity": "displacement" } ],
  "asserts": [ { "path": "solver.ok", "op": "==", "value": 1 } ]
}
```

`maxVolume` units depend on the geometry source, with a target edge length e
in mm:

- **Box presets:** mm³, so `maxVolume ≈ e³/8.5`.
- **STEP/STL:** normalized model units, scaled so the largest bbox dimension
  L_max is 3 units. Use `maxVolume ≈ (e³/8.5)·(3/L_max)³`. The example above is
  e ≈ 1.2 mm on the 100 mm bar.

Check `mesh.nTets` in the report and aim for at least 4 Tet10 elements through
the critical depth.

### 8.3 Stiffness prediction

k_pred = mag / u, where u is the probe's vector component along the load. Check
`snapDist` < 0.5 mm. If it isn't, the probe snapped to the wrong place; adjust
`posMM`.

### 8.4 Fracture-load prediction

The fracture solver holds a fixed load and deletes elements until nothing more
fails or the part separates. This is load control, the same as a dead-weight
test. Extract two numbers per test by bisection on `loads[0].mag`:

- **F_first** (damage onset): the smallest load at which `fracture.totalFailed`
  ≥ 1.
- **F_collapse** (the predicted breaking load, compared with the tests): the
  smallest load at which the run ends with the part severed. Detect severance
  from the console log: `[FRACTURE] ... part SEVERED`.
  - Use `maxIter` ≥ 40 for prediction runs; the regression scenarios use 8–14
    for speed. If a run hits `maxIter` while still killing elements, rerun it
    with double the limit before classifying it.
  - **Known harness quirk:** the severance check in `solveBrittleFracture` is
    `iter > 1` on a 0-based counter. A part that separates on the *second*
    iteration is therefore reported as `solver 'fracture' returned failure`
    (exit 2) instead of as severed. At a load above F_first, treat exit 2 with
    prior element deaths in the log as severed.

Bisection method:

- Bracket between 0.5× and 2× the closed-form estimate.
- Seven bisection steps give about 1 % resolution.
- Record `fracture.byMode` and a `deadView: "colored"` screenshot at F_collapse.
  These are the predicted mode and crack location.

**Element order.** A fresh headless `kind: "fracture"` run uses Tet4: the
runner sets `useQuadraticElements = hasQuadraticMesh`, which is false for a new
TetGen mesh, and `solve.quadratic` is ignored on this path. A `kind: "linear"`
run defaults to Tet10. Stiffness predictions are therefore Tet10 and fracture
predictions Tet4, so refine fracture meshes further. Alternatively, run fracture
from the UI after a Tet10 linear solve, which makes fracture inherit Tet10.

### 8.5 Mesh convergence (required for every V prediction)

- Run three mesh levels, each with edge length about 1.5× finer than the last
  (`maxVolume` ÷ 3.4 per level), and record `mesh.nTets`.
- A quantity has converged when the two finest meshes agree within **3 %** for
  stiffness and **5 %** for F_collapse.
- If it has not converged, report all three values and use their spread as the
  numerical uncertainty.

Element deletion without regularization is expected to drift for V5 (the notch
root) and possibly under the V1/V5 loading nose. That drift is itself a finding:
`physics-safe-load-setup.md` already lists "fracture regularization and
mesh-objectivity evidence" as deferred work.

### 8.6 Closed-form cross-check (solver error)

Compare each converged simulation with `specimen_estimates.py`, run on the
calibrated `.mat` with the same geometry:

- Stiffness in V1/V2/V3 should agree within about **5 %**. Beam and Winkler
  theory carry a few percent of idealization error of their own.
- A larger gap is a units, preset, or mesh problem. Resolve it before comparing
  with experiments.
- For fracture, the clean uniform-stress check is the C1/C2 closure (§8.7). In
  V1/V2, F_first matches 2σbh²/3L only if the first element dies at the
  mid-span bottom fiber. If it dies under the loading nose first, record that as
  a load-introduction/criterion artifact (H3, H4, and §9.4).

Two quick extra checks:

- Scaling a box by 10× must scale stiffness by exactly 10× and failure load by
  100×. This exercises the SI-scaling path.
- CPU and GPU runs must agree on F_collapse within 0.5 %.

### 8.7 Closure

Simulate C1 and C2 with the calibrated `.mat`. F_collapse must reproduce the
measured mean strength × area within 3 %. This is an end-to-end check of mode
mapping, `buildAxis`, units, and FacePull.

## 9. Analysis and acceptance

### 9.1 Per-specimen normalization

- Scale each prediction to the specimen's **measured** dimensions:

  | Test | Strength scales with | Stiffness scales with |
  |---|---|---|
  | Tension | A | — |
  | 3-point bend | b·h² | b·h³ |
  | Torsion | d³ | d⁴ |
  | C-ring | b·h² | b·h³ |

- Then compute the ratio r_i = measured_i / predicted_i.

### 9.2 Statistics and verdict

Let r̄ be the mean ratio and s its standard deviation. The 95 % confidence
interval of the mean is r̄ ± t·s/√n, with t = 2.776 for n = 5 and 2.262 for
n = 10.

- **PASS:** |r̄ − 1| ≤ tol, and the CI half-width ≤ tol (the data are precise
  enough to say so).
- **FAIL:** the whole CI lies outside [1 − tol, 1 + tol].
- **INCONCLUSIVE:** anything else. Print 5 more and retest. With a CoV of 10 %
  and n = 5, the CI half-width alone is ±12 %. Interlayer series often need
  n = 10.

Report the coefficient of variation (CoV) for every series. If CoV > 15 %, the
print process is unstable. Fix the printing before drawing any conclusion about
the solver.

### 9.3 Acceptance criteria

These are proposals. Change them **before** testing, never after.

| # | Check | Quantity | Tolerance |
|---|---|---|---|
| A1 | Rig | R0 measured vs closed form | ±3 % |
| A2 | Solver, known material | R0 simulation vs closed form | ±2 % |
| A3 | Closure | C1/C2 F_collapse vs measured strength × area; V1/V2 simulated stiffness vs measured | ±3 % |
| A4 | Solver cross-check | V-series simulation vs §8.6 closed forms | ±5 % |
| A5 | Mesh convergence | Two finest meshes | ±3 % stiffness, ±5 % F_collapse |
| A6 | Deformation | V3, V4, V5 stiffness | ±10 % |
| A7 | Deformation | V6 stiffness (sparse, toolpath mesh) | ±20 % |
| A8 | Fracture | V1, V3 F_collapse (intralayer) | ±15 % |
| A9 | Fracture | V2 (interlayer tension), V4 (interlayer shear; torque = F·a) | ±20 % |
| A10 | Fracture | V2/V1 measured ratio vs predicted ratio | ±20 % |
| A11 | Fracture | V5 notched | Conservative (r̄ ≥ 1) and r̄ ≤ 1.35 |
| A12 | Fracture | V6 sparse | ±25 % |
| A13 | Mode and location | Every V test | Predicted mode matches in ≥ 4 of 5 specimens; predicted crack at the same feature, within 3 mm |
| A14 | Buckling gap | V7 | Not pass/fail. Record P_cr measured, Euler with calibrated E, and the simulation's failure load; that ratio becomes the target for a buckling milestone |

**Verdict.** Report every row separately and never average across tests. Claim
"PolyFEA predicts X within Y %" only for rows that PASS. State the printer,
material, and print settings the claim covers.

### 9.4 Diagnosis table

| Symptom | Most likely cause | Category |
|---|---|---|
| A1 fails | Clamp rotation, indicator stand flexing, mislabeled weight | Rig |
| A2/A3/A4 fails | Units, wrong preset or axis, `buildAxis`, missing `.mat` keys, unconverged mesh | Solver/setup |
| All stiffnesses off in the same direction by a similar amount | Calibration timing differs from validation timing (creep), or porosity (check density) | Calibration |
| Bending stiffness too high only on toolpath meshes | Tet4 shear locking; refine `targetEdgeMM` | Solver (element) |
| V1 measured far above predicted (1.2–2×), V2 closer | FDM flexural strength exceeds tensile strength: outer perimeters run along the stress, the stress gradient matters, PLA is nonlinear near failure. Fix: calibrate in-plane strength in flexure or add a gradient/critical-distance criterion | Material model |
| Simulated first damage under the loading nose or on the compressed fiber, but the real crack starts on the tension side | The in-plane criterion is von Mises, which treats tension and compression alike; real PLA is stronger in compression. Fix: a tension-sensitive in-plane criterion | Material model |
| V4 measured about 10 % below predicted, with an interlayer break | Interlayer σ–τ interaction: the code checks tension and shear independently. Fix: a quadratic interaction criterion | Material model |
| V5 far more conservative than expected; F_first keeps falling with refinement | Local element-death criterion with no length scale (notch sensitivity < 1). Fix: regularization or a critical-distance method | Material model / known gap |
| CoV > 15 % in a series | Print instability: layer time, cooling, moisture | Print process |

## 10. Records and repository layout

Keep campaign data outside `scenarios/`, as noted in §8.2:

```
validation/milestone1/
  README.md          printer, nozzle, profile, spool lot, dates, deviations from this plan
  print/             *.step, *.gcode.3mf (exactly what was printed)
  materials/         pla_<spool>_cal.mat
  scenarios/         one JSON per test × mesh level × load
  predictions.csv    committed BEFORE any V-specimen is broken
  specimens.csv
  results.csv
```

Keep photos and video outside git (or in LFS) and reference them by filename.

**`predictions.csv`:** `test_id, polyfea_git_hash, mat_file, mesh_level,
nTets, element_order, k_pred_N_per_mm, F_first_N, F_collapse_N,
predicted_mode, predicted_location, closed_form_value, closed_form_gap_pct`

**`specimens.csv`:** `specimen_id, series, orientation, plate, plate_position,
print_date, mass_g, dim1_mm, dim2_mm, dim3_mm, density_g_cm3, rejected,
reject_reason`

**`results.csv`:** `specimen_id, test_date, test_order, temp_C, rh_pct,
k_meas_N_per_mm, r2, residual_pct, F_fail_N, time_to_failure_s, failure_mode,
crack_location_mm, valid, notes, media_ref`

Randomize test order across series, for example by drawing IDs from a shuffled
list. This keeps drift in room conditions or technique from looking like a
difference between series.

## 11. Pre-registered expectations

Writing these down before testing means a passing result can't be explained
away afterward, and a failing one already points at a likely cause.

- **H1.** Solid-part stiffness (V3, V4, V5) lands within ±10 % once E and E_z are
  calibrated with the same 10 s reading protocol.
- **H2.** V2 breaks flat on a layer line at the bottom fiber, and the interlayer
  failure load is predicted within ±20 %.
- **H3.** V1 is *under*-predicted: the measured failure load is 1.2–2× the
  prediction, because FDM PLA's flexural strength exceeds its tensile strength.
  If so, the failure is in the material model (A8 fails, A4 passes), not the
  solver.
- **H4.** In V3, the simulation's first damage appears at the inner, compressed
  fiber, about 20–25 % below the load at which the real ring cracks on its
  outer, tension fiber.
- **H5.** V4 is predicted to fail in interlayer shear. The measured load may be
  about 10 % lower if σ–τ interaction is real.
- **H6.** V5's F_first is conservative and keeps falling under mesh refinement;
  F_collapse moves less.
- **H7.** For V7, the simulation overpredicts load capacity by 20–60×.

## 12. Safety

- Wear safety glasses for every break. PLA fails in sharp shards.
- Limit the drop height to about 20 mm with a strap, chain, or stop under the
  hanger, and keep foam underneath.
- Keep hands and feet out from under hanging loads. Check the hanging point and
  eye bolt before every session.
- Buckling columns and the C-ring can spring sideways when they let go. Keep a
  guard or box around them.

## 13. Optional harness additions

None of these is required — the bisection protocol in §8.4 works with today's
runner — but each one removes a manual step or a trap:

1. Add `fracture.severed`, `fracture.iterations`, and `fracture.hitMaxIter` to
   `report.json`, so the log doesn't have to be grepped.
2. Add `fracture.peakStressRatio`: the maximum over elements and modes of
   stress/limit at the first iteration, plus that element's position and mode.
   Because the model is linear, F_first = mag / ratio comes from a single run.
3. Honor `solve.quadratic: true` for `kind: "fracture"`, so fracture and
   stiffness predictions use the same element order.
4. Change the severance condition from `iter > 1` to `iter > 0`, so a
   second-iteration separation reports as severed rather than as a solver
   failure. This matches the adjacent comment.
