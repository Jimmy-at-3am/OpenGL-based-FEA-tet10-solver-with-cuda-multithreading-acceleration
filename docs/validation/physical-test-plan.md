# Physical Validation Test Plan — Milestone 1

- **Status:** Proposed — not yet executed
- **Validates:** milestone-1 PolyFEA. That covers small-strain linear-elastic
  statics, transversely isotropic FDM material, and brittle element-deletion
  fracture with three modes: interlayer tension, interlayer shear, and in-plane
  von Mises.
- **Equipment:** education-kit masses (5 g steps), exercise weights (5 lb
  steps), a printed weight carrier on a screw stand, a bench vise, an overhead bar
- **Tool:** `tools/validation/validate.py` (Python 3, standard library only),
  with four commands: `estimate`, `predict`, `calibrate`, `verdict`
- **Last reviewed:** 2026-10-04 (the earlier, exhaustive version is in git
  history at commit 41dd83c)

## Quick start

1. **Rig check** (15 min, §3).
2. **Print** the specimens in §4: four designs, about 50 prints incl. fixtures, one profile.
3. **Calibrate.** Break the tension dogbones and the propped twist specimens.
   For the bars, take elastic readings only — don't break them yet. Then run
   `validate.py calibrate`, which writes your `.mat`.
4. **Predict.** Run `validate.py predict` for every test at two mesh sizes, then
   commit `predictions.csv`. **Break nothing else before this commit.**
5. **Validate.** Break the bars, the free twist specimens, and the C-rings. Then
   run `validate.py verdict`.

## 1. What each test does

| Test | Specimen | Rig | Gives your material | Validates the solver on | Prints |
|---|---|---|---|---|---|
| Tension | Dogbone, flat and standing | Overhead bar | In-plane and interlayer strength | — (calibration) | 5 + 5 |
| Bending | Square bar on a grip block, flat and standing | Vise | E, E_z (elastic readings) | Breaking force, both orientations | 5 + 5 |
| Twisting | Shaft with a 100 mm lever | Vise | G_pz and interlayer shear strength, with the lever propped | Breaking torque and stiffness, with the lever free | 5 + 5 |
| Compression | C-ring | Anvil + frame on the overhead bar | — | Breaking force and stiffness | 5 |
| Buckling (optional) | T-column, 3 lengths | Vise | — | Records the gap (see below) | 3 |

**The logic.** Your own prints give the material constants. Everything else is
predicted from those constants *before* it is tested, so a match means
something. Strength is calibrated in tension because datasheets quote tensile
strength. The bending results therefore show how the solver behaves with
datasheet-style inputs.

**What milestone 1 cannot do:**

- **Buckling.** The solver has no geometric stiffness, so a slender column only
  "fails" when it crushes. That is 30–80× the real buckling load of these
  columns. The column test puts a number on the gap.
- **Twisting.** There is no torque load. The twist test uses a force on the
  lever, which the `bendZY` preset reproduces exactly.
- **Crushing solid blocks.** PLA yields rather than fractures in compression,
  and crushing a 10 mm cube needs about 600 kg. The C-ring is squeezed but
  breaks in tension, which the model covers.

## 2. Loading with your weights

- **Exercise weights (5 lb steps):** for the tension tests and base loads.
  Weigh each one once on a bathroom scale and write the true mass on it. Cast
  weights are often 2–5 % off, and that error goes straight into every result.
- **Education kit (5 g steps):** for elastic steps and fine control on the
  light tests.
- **Weight carrier on a screw stand (printed, `loader_*` files):** plates
  slide onto the carrier while it rests on the stand, so your hands never
  load the specimen. Turning the stand's hand wheel down lowers the carrier
  until the specimen picks up the weight gently. When the specimen breaks, the
  carrier drops only a few mm onto the stand. The load path is steel:
  specimen → shackle → M10 eye nut → M10 rod → washer and nut under the tray.
  Replace nylon strings with steel shackles or the printed load yoke.
- **Weigh once:** the carrier with its rod, nuts and eye nut, the shackle, and
  the yoke or frame. They are part of every load.
- **Write loads as sums with units**, for example `45lb+350g+1.2kg`. The tool
  converts them.

These estimates come from `validate.py estimate` with the literature
`pla.mat`. Rerun it with your calibrated `.mat`:

| Test | Breaks near | Elastic steps | Break steps (each held 30 s) |
|---|---|---|---|
| Tension, flat | 300 N (67 lb) | — | Start at 40 lb, then +5 lb |
| Tension, standing | 274 N (61 lb) | — | Start at 35 lb, then +5 lb |
| Bar, flat | 53 N (5.4 kg)\* | 4 × 540 g | Start at 3 kg, then +250 g |
| Bar, standing | 24 N (2.5 kg)\* | 4 × 250 g | Start at 1.3 kg, then +100 g |
| Twist (either) | 18 N (1.8 kg) | 4 × 180 g | Start at 1 kg, then +100 g |
| C-ring | 102 N (10.4 kg) | 4 × 1 kg | Start at 10 lb (4.5 kg), then +5 lb, kit masses near the end |
| Columns 60 / 80 / 100 | 32 / 18 / 12 N, split across two kit hangers | 165 / 95 / 60 g per hanger | — |

The starting load includes the carrier and hardware. Near the expected value,
use smaller steps (kit masses on top of the plates), because the step size is
your measurement resolution.

\*Beam formula. The root fillet makes the real value 10–20 % lower.

## 3. Rig check — do this first

Clamp an aluminium flat bar (20 × 3 mm, ≥ 200 mm long, from a hardware store)
in the vise with 150 mm free. Hang 250, 500, 750, then 1000 g at 150 mm and read
the tip deflection; expect about 3.55 mm per kg. Measure the bar's thickness at
5 points, because stiffness goes with thickness cubed. Record it as a `rig` row
(§8); `verdict` checks that the measured E is within 3 % of 69 GPa. If it
isn't, fix the rig before anything else. The usual culprits are the indicator
stand flexing, the vise clamp rotating, or a mislabeled weight.

## 4. Prints

**Profile — the same for every specimen.**

- 0.4 mm nozzle, 0.2 mm layers, 2 walls, 100 % infill.
- Set the sparse infill, internal solid infill, and top/bottom surface patterns
  all to **rectilinear**, so the inside of every part is the same ±45°
  crosshatch. Thin coupons are otherwise mostly skin and won't represent thick
  parts.
- One spool, dried. Keep each plate's `.gcode.3mf`.
- **Standing prints:** at least 5 at once, spaced ≥ 20 mm, with a minimum layer
  time of ≥ 8 s and a brim. A lone thin part doesn't cool between layers, so
  its layer bonding won't match your real parts.
- Condition every part 48 h at room temperature. Before testing, measure the
  critical dimensions (3 readings each).

**Ready-made files:** `docs/validation/specimens/` has every part as print-ready STL and STEP, plus the simulation copies; see its README for how to print them. Clamp blocks and load points are oversized on purpose so standard hardware grips them; only the measured sections are small.

**Geometry.** Coordinates are in mm. The simulation depends on the origin and
axes, so model the parts exactly like this and export STEP in mm.

| Part | Geometry | Print |
|---|---|---|
| **Dogbone** | Outline in XY, length along X: 30 × 40 tabs, each with a Ø8.5 hole (M8) 15 mm from its end; a 3.0-wide × 30-long gauge; R40 arcs from gauge to tab; 170 overall. Extrude 2.0 (flat print) or 4.0 (standing print). Each end is clamped between two printed grip plates (66 × 54 × 10, M8 through the tab hole, four M5 beside the tab, Ø14 carabiner hole). | 5 flat + 5 standing on a tab end; 4 grip plates |
| **Bar** | Flat CAD: block x −25..0, y −20..20, z 0..24 (vise grip); 8 × 8 bar x 0..64, y −4..4, z 0..8 (flush with the block bottom), R3 fillets at the block; 45° taper x 64..72 into a 16 × 16 load head x 72..88 (z 0..16) with a Ø6.5 cross hole along Y at x = 80, z = 8. Standing CAD: the same part with the bar along +Z, block on the bed (x 0..24, z 0..25), flush side facing −X, load hole at z = 105. | 5 as the flat CAD, 5 as the standing CAD |
| **Twist** | Block x −20..20, y −20..20, z 0..20 (vise grip); Ø8 shaft on the Z axis, z 20..40, R3 fillets at both ends; lever x −10..108, y −8..8, z 40..52; load block x 92..108, y −8..8, z 52..64 with a Ø6.5 hole along X at z = 58 (100 mm from the axis). A notch on the lever's y = −8 face marks the shaft axis; that face points down in the test. | 10 as modeled; supports under the lever only, none touching the shaft |
| **C-ring** | Ring in XY centred on the origin: inner R13, outer R17, z 0..10; a 6-wide gap centred on +X; 16-wide flat load pads at y = ±19. | 5 flat |
| **Column** (optional) | Flat "T": 25 × 30 × 10 clamp block, then a 6 × 3 stem whose free length from the block to the crossbar centreline is 60, 80, or 100; a 100 × 14 × 3 crossbar with Ø6 holes at ±44. | 1 of each length, flat |
| **Frame** (fixture) | Rectangle with a 60 × 80 interior and 16 × 16 bars; 200 × 25 × 20 anvil bar with Ø8 cord holes. | 1 each |

Print one spare per design to shake down the rigs.

**Simulation copies.** The solver loads the end face of a part, so that face
must sit on the axis of the load hole. For the simulation STEP files:

- cut the bar's load head at the hole axis, x = 80 (z = 105 standing), without
  the hole;
- cut the twist load block at the hole axis, z = 58, without the hole.

The dogbone and C-ring are simulated as printed.

## 5. Rigs

```
TENSION                  VISE (bars, twist, columns)        C-RING
  ═══╤═══ overhead bar                                      ═══╤═════════════╤═══ overhead bar
     │ cord              ▐█ vise █▌▬▬▬▬▬▬▬▬ part               │ cord ┌───┐   │ cord   frame top bar
   (grip)                  block clamped   │ hanger            │      │ C │   │        on the ring
  ▐dogbone▌                (table edge)    │                ═══╧══════╪═══╪═══╧═══ anvil, passes
   (grip)                                [load]                       └─┬─┘           through the frame
     │ carabiner                                                        │
  [carrier on screw stand]                                              [load]
```

- **Tension:** clamp each tab between two grip plates (an M8 bolt through the
  tab hole, four M5 bolts beside the tab), and hang each grip from a carabiner
  through its Ø14 hole so the pull self-centres. The lower grip connects
  by shackle to the weight carrier on its screw stand; the stand catches the
  carrier when the coupon snaps.
- **Vise:** mount it at the table edge so the load hangs clear.
  - Bars: horizontal, flush side down. Put an M6 bolt or rod through the load
    head and loop the hanger over both ends so the load hangs centred.
  - Twist: the shaft and lever both horizontal; an M6 bolt through the load
    block, with the hanger looped over both ends.
    For the **propped** specimens, set a round rod (parallel to the shaft)
    under the lever exactly at the axis mark, just touching before you load.
    That makes the load a pure torque. The **free** specimens get no rod.
- **C-ring:** hang the anvil bar (printed, or any stiff 200 mm bar) from the
  overhead bar by two cords through its end holes, after sliding the printed
  frame onto it. Stand the ring on the anvil with its flats up and down and its
  gap to the side, inside the frame. The frame's top bar rests on the ring and
  the load hangs from the frame's bottom. Gravity centres everything.
- **Deflection:**
  - Best: a dial indicator (0.01 mm, about $20) clamped to the vise or table.
  - Without one: put a phone on a stand and photograph the part against a
    steel ruler fixed behind it, then zoom in later (about ±0.1 mm).
  - C-ring: measure the gap between the anvil and the frame beside the ring
    with calipers.

## 6. Procedure (every specimen)

1. **Measure and log** the specimen's dimensions and ID, plus the room
   temperature.
2. **Seat it.** Mount it, hang about 5 % of the expected load, and zero the
   indicator.
3. **Elastic readings** (not for the dogbones). Take 4 steps up to about 40 %,
   reading 10 s after each step; PLA creeps, so keep that timing identical.
   Unload, repeat, and record the second pass.
4. **Break it, in steps** (see `setup/0_loading_method.png`). Remove the
   indicator.
   - Raise the stand so it carries the carrier.
   - Load the starting weight, then turn the wheel down until a 3–5 mm gap
     opens under the carrier.
   - Hold 30 s, the same for every step and specimen; PLA creeps, so timing
     matters.
   - If it survives, raise the stand back up, add one step, and lower again.
   - Never add or remove weight while the specimen holds it.
5. **Record:**
   - the load it broke at (as a sum with units), and in `notes` the last load
     it survived;
   - where it broke;
   - how it broke: *interlayer* is a flat break along one layer line;
     *intralayer* is rough and tears through the beads.

   Photograph the break, and video it if you can.

- **Valid tension breaks** are in the gauge or the arcs. Log a break at a hole
  as `fixture`; the tool excludes it.
- **Columns:** hang one kit slotted-mass hanger from each crossbar hole and
  add equal masses to both in small steps. Photograph
  the top's sideways deflection against a ruler each time. Stop when it keeps
  bending without more load.

## 7. Simulation, calibration, verdict

All four commands run from the repository root.

```bash
# material constants from your calibration tests
python tools/validation/validate.py calibrate results.csv --out pla_mine.mat

# one prediction; repeat per test and mesh (--edge-mm 1.5, then 1.0)
python tools/validation/validate.py predict --exe C:/path/FEAPreProcessor.exe \
    --test bar_flat --geometry bar_flat_sim.step --mat pla_mine.mat \
    --guess 45 --edge-mm 1.5 --append predictions.csv

# compare (also checks the rig, closure, mesh convergence and the columns)
python tools/validation/validate.py verdict results.csv predictions.csv --mat pla_mine.mat
```

**What `predict` does.** For `--test`, choose from `tension_flat`,
`tension_standing`, `bar_flat`, `bar_standing`, `twist_free`, and `cring`. For
`--guess`, use the breaking load that `estimate` gives with your `.mat`.

- It builds the scenario: preset, sign, mesh, and probe for the documented
  geometry.
- It repeats fracture runs at different loads to find two numbers: the load
  that first damages an element, and the load that breaks the part.
- It runs one linear solve for stiffness.
- Its line goes into `predictions.csv`.

It also takes care of the harness traps:

- A break on the second fracture iteration is reported as a solver failure
  (`iter > 1` in `solveBrittleFracture`); the tool counts it as a break.
- Scenario paths are made absolute.
- Runs go to `polyfea_predict/`, never to `scenarios/`.

**Order of work.**

1. Calibrate.
2. Predict `tension_*` and `bar_*`. `verdict` must show **closure**: the
   simulation reproduces your coupon strengths and bar stiffness within 3 %. If
   the bar stiffness is off, `verdict` prints the factor to apply to E or E_z;
   rerun those predictions.
3. Predict `twist_free` and `cring` at two mesh sizes.
4. Commit, then break.

## 8. Recording results

Copy `docs/validation/results_template.csv`. Write one row per specimen; lines
starting with `#` are ignored.

| Column | Meaning |
|---|---|
| `test` | `rig`, `tension_flat`, `tension_standing`, `bar_flat`, `bar_standing`, `twist_propped`, `twist_free`, `cring`, `column60` / `column80` / `column100` |
| `dim1_mm`, `dim2_mm` | Measured critical dimensions. Dogbone: gauge width, thickness. Bar: width, height (along the load). Twist: shaft diameter. C-ring: width, radial thickness. Rig and column: width, thickness. |
| `loads`, `defl_mm` | Elastic steps, space-separated: total hanging load and reading at each step. For columns, loads are the total of both bottles. |
| `fail` | Total hanging load at the break, for example `40lb+2.35kg` |
| `mode` | `interlayer`, `intralayer`, `mixed`, or `fixture` |

## 9. Acceptance

`verdict` applies these. Change them before testing, never after.

| Check | Pass when |
|---|---|
| Rig | E within ±3 % of 69 GPa |
| Closure (coupons, bar stiffness) | Mean within ±3 % |
| Mesh | Two finest meshes within 5 % (break load) and 3 % (stiffness) |
| Stiffness: twist, C-ring | ±10 % |
| Breaking load: flat bar, C-ring | ±15 % |
| Breaking load: standing bar, twist (torque = load × 100 mm) | ±20 % |
| Failure type | Predicted type matches in ≥ 4 of 5 |

**How each verdict is decided:**

- **PASS:** the mean of measured/predicted is within the tolerance, *and* the
  data are precise enough — the 95 % interval is no wider than the tolerance.
- **FAIL:** the whole interval lies outside the tolerance.
- **INCONCLUSIVE:** anything else. Print 5 more specimens.

Interlayer series often need 10 specimens.

Report each row separately. Claim "predicts X within Y %" only for rows that
pass, and only for your printer, material, and settings.

**When something misses:**

- **Rig or closure fails:** setup problem (units, preset, a missing `.mat` key).
  Fix it first.
- **Mesh not converged:** element deletion is mesh-sensitive; refine
  (`--edge-mm 0.7`) and report the spread.
- **All stiffnesses off in the same direction:** check the 10 s reading timing
  and the density of your prints.
- **Bending breaks above prediction (flat bar, C-ring):** printed PLA is
  stronger in bending than in tension. This is a material-model limit, not a
  solver bug.
- **The simulation's crack starts on the squeezed side but the real one on the
  stretched side:** the in-plane criterion treats tension and compression
  alike.
- **Coefficient of variation above 15 %:** the printing is unstable. Fix that
  before judging the solver.

## 10. Expected results (written before testing)

- **Stiffness:** within ±10 % once E, E_z and G_pz come from your prints.
- **Standing bar and twist:** flat interlayer breaks at the root, predicted
  within ±20 %. The twist may land about 10 % low if interlayer tension and
  shear interact, because the solver checks them separately.
- **Flat bar and C-ring:** breaking loads under-predicted; measured is
  1.2–2× predicted. In the C-ring the solver should also flag the inner,
  compressed fiber about 20–25 % early. The real crack starts on the outer
  back.
- **Twist stiffness:** the propped calibration differs from the free test
  mainly by shaft bending (about 3 %). The C-ring is therefore the stronger
  independent stiffness check.
- **Columns:** measured within about 10 % of Euler (vise compliance lowers it).
  The solver would carry 30–80× more, because it has no buckling.

## 11. Safety

- Wear glasses for every break; PLA shatters.
- Keep the screw stand under every hanging load (it is the catch), and your feet out from under it.
- Check the overhead bar before every tension session; those tests hang about
  60 lb.
