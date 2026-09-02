# Physics-Safe Load Setup

- **Status:** First safety foundation implemented; automated verification complete
- **Audience:** PolyFEA developers, reviewers, and instructors
- **Last reviewed:** 2026-08-30

## Decision summary

PolyFEA will keep its direct, novice-friendly viewport interaction, but friendly
labels will resolve to explicit engineering semantics. The frontend may simplify
language; it must not silently change a load footprint, reference frame,
distribution, transfer mechanism, constraint, or analysis assumption.

The existing `docs/design/load-input-system.md` remains the detailed interaction
and selection reference. This document supersedes its six-tool taxonomy and its
proposal to make a precomputed nodal `LoadSet` authoritative.

The first implemented slice is deliberately small:

- a GL-free semantic description of load/support intent;
- an analysis-capability gate shared by UI and tests;
- correct pressure-resultant auditing over planar facets that tessellate a
  curved surface;
- a compact Physics Receipt for the existing preset workflow;
- no new contact, connector, follower-load, or manufacturing-strain solver.

## Context and goals

PolyFEA is an educational C++17/OpenGL finite-element application for printed
parts. Its users should be able to say “push here” or “hold this with a pin”
without learning a traditional solver interface. That interaction is successful
only when the generated mathematical problem remains inspectable and precise.

Goals:

1. Make every committed condition state what quantity is known and how it enters
   the part.
2. Prevent the UI from claiming physics the selected solver path cannot perform.
3. Preserve load and support meaning across camera motion and remeshing.
4. Show requested and resolved force/moment before solving, then reactions and
   imbalance afterward.
5. Add fidelity in independently testable stages without replacing the renderer,
   camera, mesh pipeline, or legacy solver presets.

Non-goals for the first slice:

- general mesh-region painting and gizmos;
- arbitrary external load assembly;
- contact, friction, bolts, preload, springs, or remote kinematic coupling;
- follower pressure or load-stiffness assembly;
- thermal, moisture, residual-strain, or print-process simulation;
- claiming mesh-objective fracture prediction.

## Current solver envelope

The current solver builds preset force vectors and zero-displacement constraints
internally. Its nonlinear path scales one precomputed external vector through the
load steps. The honest supported envelope is therefore:

- single-body structural analysis;
- static loads;
- deformation-independent (dead) force direction and distribution;
- zero Cartesian displacement constraints;
- penalty enforcement of those constraints;
- no contact or connector state.

The current brittle-fracture path is an element-deletion model. Its crack path and
failure load remain mesh-sensitive unless a later regularized formulation and
convergence evidence establish otherwise.

The preset audit also found a mode-specific dispatch defect: `CantileverBendingZ`
has a dedicated linear setup, but `solveNonlinearStatic` does not branch on that
preset. It falls through to the Y-min clamp/Y-max surface-compression setup. The
first safety foundation therefore blocks that one preset/mode pair instead of
letting a familiar button launch a different physical problem.

The tension presets contain a separate support defect. Their comments call the
automatic restraint a 3-2-1 gauge, but the implementation fixes three DOFs at
one node and one DOF at each of two other nodes: five scalar constraints, not
six. At least one rigid mode therefore remains. This slice keeps the legacy
tension workflow runnable to avoid changing solver behavior, marks every tension
mode approximate, and exposes `5-DOF; >=1 RIGID MODE FREE` in the receipt.
Completing and rank-testing the gauge is the first solver-side follow-up.

### Existing preset truth table

`Exact` below means exact input semantics, not verified mesh convergence or
material validity.

| Existing preset | Entered magnitude | Spatial distribution | Automatic support | Linear | Nonlinear | Brittle fracture |
|---|---|---|---|---|---|---|
| Cantilever Z | Total force at one tip node | Concentrated node | X-min full Cartesian clamp | Approximate: singular local stress | Unsupported: solver changes to Y compression | Approximate: point load plus element deletion |
| Point Z | Total force at one top node | Concentrated node | Z-min full Cartesian clamp | Approximate | Approximate | Approximate |
| Surface compression Y | Total force on one face | Linear corner-triangle tributary load; equal-node fallback. Tet10 midside nodes receive no direct face load | Y-min full Cartesian clamp | Approximate: bbox footprint, quadratic-face linearization, and fallback | Approximate | Approximate: same plus element deletion |
| Tension X/Y/Z | Force magnitude on each of two opposite faces | Equal force per selected node | Five scalar constraints (3+1+1); at least one rigid mode remains | Approximate: mesh-dependent load and underconstrained gauge | Approximate | Approximate |

## Reliability findings

| Finding | Failure if hidden | Required rule |
|---|---|---|
| Preset/mode dispatch mismatch | Selecting nonlinear analysis silently changes cantilever bending into Y compression | Test the concrete preset adapter as well as the abstract feature; block mismatched mode paths before solver construction |
| Misnamed automatic support | A five-constraint tension gauge is presented as 3-2-1, hiding at least one remaining rigid mode and a possible singular/drifting solve | Show the resolved support in the receipt, mark the preset approximate, and require runtime `rank(CR)` plus a canonical six-mode test before relabeling it 3-2-1 |
| Quadratic-face distribution mismatch | A corner-triangle tributary vector is called consistent area loading on a Tet10 face, hiding that midside nodes receive no direct load | Name the current path `LinearFacetTributary`, show `CORNER-TRI`, and reserve `ConsistentArea` for an adapter that integrates all quadratic face functions |
| Conditional load fallback | “Area weighted” silently becomes equal force per node when the selected slab has no complete boundary triangles | Mark the preset approximate until resolved distribution is returned to the receipt; emit the fallback in solver diagnostics |
| Patch force footprint | Remeshing changes area, traction, peak stress, and failure | Store an accepted physical radius or exact geometry; never regenerate it from element size |
| Pressure semantics | A fixed nodal vector is presented as follower pressure | Mark current pressure as reference-configuration/small-deformation only; block unsupported combinations |
| Curved pressure result | `pressure × area` is mislabeled as net force | Integrate the vector area and report scalar load integral, vector resultant, and moment separately |
| Fixed-only support | Holes, sliders, platens, and flexible mounts are overconstrained | Ask how the part is attached; fixed is only a rigid clamp/bond |
| Missing displacement control | Compression, snap-fit, bend, and post-peak tests use the wrong control variable | Add prescribed displacement before calling the general workflow classroom-ready |
| Generic torque | A numerical force distribution is mistaken for a physical fixture | Model a remote resultant and explicit flexible/rigid transfer; label equivalent distributions |
| Camera-plane drag | View orientation becomes an accidental physical frame | Treat dragging as provisional input; commit a named frame and components |
| Multi-target magnitude | Adding a target silently multiplies or redistributes load | Store `TotalAcrossSelection` versus `PerRegion` explicitly |
| Target remap | A nearby but physically different region is selected after remeshing | Compare area, centroid, normal distribution, topology, and resolved resultants; require acceptance |
| Penalty constraints | “Fixed” leaks or reactions are inaccurate without notice | Report maximum constraint violation, conditioning warning, and equilibrium residual |
| Fracture orphan nodes | A stale force remains on a node with no active load path | Reject stale active targets and eliminate inactive degrees of freedom rather than grounding them silently |

## Industry interaction patterns retained

The design borrows reliability patterns, not enterprise visual density:

- ANSYS Mechanical exposes load/support objects, geometry scoping, remote transfer
  behavior, and distinct support types.
- Abaqus/CAE keeps region, distribution, amplitude, follower behavior, coupling,
  and step activation explicit.
- COMSOL keeps pressure, traction, total force, coordinate frame, and
  rigid/flexible attachment as semantic physics features.
- SOLIDWORKS and Fusion combine viewport manipulators with explicit total/per-item
  scope, bearing loads, remote loads, and pin/frictionless/prescribed supports.
- Altair tools add load-case and connection review so unresolved or false load
  paths remain visible.

The common lesson is that guidance may choose or explain a precise definition;
it must not substitute a different beginner-only formulation.

## Interaction design

The top-level workflow asks physical questions instead of presenting six similar
mathematical cards.

### How does the load reach the part?

| Friendly label | Engineering contract |
|---|---|
| Presses on a surface | Pressure or total force over a physical patch |
| A pin or screw pushes in a hole | Bearing load |
| An attached object pulls from here | Remote force and moment |
| The fixture moves this area | Prescribed displacement |
| The whole part accelerates | Gravity or body acceleration |

### How is the part held?

| Friendly label | Engineering contract |
|---|---|
| Clamped or bonded rigidly | All translations fixed |
| Resting or sliding | Normal motion fixed; tangential motion free |
| Held by a pin | Cylindrical support with explicit free directions |
| Mounted flexibly | Elastic foundation or bushing |
| Touching another part | Contact, when the solver supports it |

`Distributed force` is not a separate beginner card. It is the behavior of a
total Push/Pull applied over the selected physical region. `Torque` appears under
an attached fixture/remote resultant so the transfer assumption cannot disappear.

## Physics Receipt

Every draft and solve displays a compact receipt sourced from resolved solver
semantics, not from decorative gizmos:

```text
Push/Pull — total patch force
120 N across 84.2 mm²; average traction 1.43 MPa
Direction: global -Z, fixed in space
Resultant: F=(0, 0, -120) N
Moment about model origin: M=(...) N·m
38 surface nodes; footprint adequately resolved
Local peak stress inside the patch is load-introduction-sensitive
```

Required receipt fields:

- exact quantity, magnitude, and unit;
- total/per-region scope;
- target area, centroid, and connected-component count;
- spatial distribution and transfer behavior;
- reference frame and dead/follower behavior;
- resultant force and moment about a visible point;
- analysis compatibility and approximation status;
- material/build frame;
- unreliable local-result regions and other idealizations.

## Semantic architecture

```text
Student intent
    -> typed semantic feature
    -> geometry resolution and remap audit
    -> analysis capability gate
    -> frozen study definition
        -> cached linear dead-load contributions
        -> constraint equations C u = d
        -> state-dependent residual/tangent operators
        -> per-feature provenance
    -> solver
    -> reaction, equilibrium, and validity audit
```

The authoritative definition remains semantic until assembly. A nodal force
vector is a derived cache only for a load confirmed to be linear and
state-independent.

The minimal common fields are:

```cpp
enum class AnalysisMode { LinearStatic, NonlinearStatic, BrittleFracture };
enum class FeatureKind {
    PointForce, PatchResultant, BoundaryTraction, Pressure, BearingLoad,
    RemoteResultant, BodyAcceleration, FixedConstraint,
    PrescribedDisplacement, NormalSupport, CylindricalSupport, ElasticSupport,
    Contact
};
enum class MagnitudeScope { TotalAcrossSelection, PerRegion };
enum class DistributionKind {
    Unspecified, ConcentratedNode, ConsistentArea, LinearFacetTributary,
    EqualNode
};
enum class FrameKind { Global, BuildMaterial, ReferenceGeometry, CurrentBoundary };
enum class EvolutionKind { Dead, Follower };
enum class Capability { Exact, Approximate, Unsupported };
```

Specific feature types own their valid parameters. Do not use one permissive
parameter bag whose invalid field combinations must be discovered later.

## Solver contracts

### Dead-load cache

Linear, state-independent loads may compile to immutable nodal contributions.
Each contribution retains its feature ID until final global assembly so previews,
audits, and errors can point back to the originating condition.

### Reference-pressure audit

`SurfaceFacet` represents one planar facet with constant outward normal, oriented
area vector, and area centroid. A curved boundary must be supplied as its
constituent planar facets; one area vector plus centroid is not sufficient to
represent an aggregated curved patch. For uniform reference pressure, the audit
sums `p |A_i|`, `sign p A_i`, and
`(centroid_i - referencePoint) x (sign p A_i)` over those facets. This separates
scalar normal load from vector force and moment without claiming follower-load
assembly.

### Constraints

General supports and prescribed motion use `C u = d`. Existing fixed Cartesian
DOFs are an optimized special case. The rigid-mode preflight evaluates
`rank(C R)` per connected component. A numerical null-mode check of the assembled
initial tangent remains the final stability gate.

### State-dependent features

Follower pressure, contact, springs, and remote kinematic coupling contribute
residual and tangent terms during nonlinear iteration. They cannot be represented
truthfully by one precomputed external vector.

### Capability registry

One pure registry answers whether a feature is exact, approximate, or unsupported
for an analysis mode. The UI, tests, and future solver adapter consume the same
answer. An unavailable feature remains visible only as a clearly disabled future
capability; it cannot create a solver object.

## Prioritized capability matrix

| Capability | First implementation | Later solver work |
|---|---|---|
| Existing patch-resultant presets | Typed contract, corner-triangle/equal-node disclosure, and mode gate | Custom persistent mesh-region adapter with true Tet10 face integration |
| Full Cartesian clamp | Existing preset support and receipt disclosure | External constraint adapter |
| Tension numerical gauge | Disclose 3+1+1 five-DOF restraint and keep legacy workflow approximate | Add an independent sixth constraint, then prove six rigid modes are removed in a canonical test |
| Reference pressure | Correct scalar/vector/moment audit; solver capability blocked | Boundary integration for custom regions |
| Gravity/body acceleration | Semantic type blocked | Mass-consistent body-force assembly and resultant audit |
| Prescribed displacement | Semantic type and blocked capability | Nonzero Dirichlet assembly and reactions |
| Normal/cylindrical support | Semantic type and blocked capability | General constraint equations and fitted geometry |
| Bearing load | Semantic type and blocked capability | Cylindrical-region distribution and integration |
| Remote resultant | Semantic type and blocked capability | Explicit distributing/rigid coupling |
| Follower pressure | Blocked | External residual plus load tangent |
| Contact/compression-only/elastic support | Blocked | Stateful nonlinear operators |
| Bolt/preload/thermal/residual strain | Out of first slice | Separate assembly/manufacturing milestones |

## Validation and acceptance

The complete system must eventually verify:

- unit, camera, rigid-transform, and remesh invariance;
- total/per-region semantics on disconnected targets;
- planar and closed triangulated-surface pressure resultants;
- actual Tet4/Tet10 nodal face-load vectors, including quadratic midside weights;
- force, moment, reaction, and virtual-work balance;
- fixed, normal, cylindrical, and elastic canonical responses;
- prescribed motion and reaction recovery;
- constraint conflict and rigid-mode identification, including runtime
  `rank(CR)` and a six-mode canonical tension-gauge test;
- case isolation;
- inactive-fracture-target rejection;
- finite-difference verification of follower-load tangents;
- stress convergence outside load/support introduction zones.

The first slice is accepted when its standalone unit test proves the capability
matrix, support disclosures, and faceted curved-surface pressure audit; the main
executable builds; and the existing regression suite remains unchanged.

### Verification record — 2026-08-30

- The strict-warning focused target reports seven passing test groups; CTest reports
  `1/1` passing.
- The full application rebuild links and stages `FEAPreProcessor.exe`.
- The existing `--regress all` suite reports aggregate `PASS`; later edits were
  confined to the semantic contract, receipt, tests, and documentation rather
  than solver assembly.
- The final executable opens its expected main window and closes cleanly.
- Receipt and slider copy are checked against `SimpleUI`'s exact per-character
  advance and the existing 275 px column.
- Windows screenshot capture could not inspect the OpenGL surface because the
  automation helper returned `SetIsBorderRequired ... 0x80004002` twice. This is
  recorded as a visual-QA limitation, not as evidence of layout correctness.

## Deliberately deferred work

The following are separate solver milestones rather than hidden shortcuts:

- viewport region painting and persistent project serialization;
- general constraint enforcement and prescribed displacement;
- bearing/pin fitting and remote coupling;
- follower pressure and contact;
- connector/preload and manufacturing strain;
- fracture regularization and mesh-objectivity evidence.

Add one of these only when a real classroom workflow needs it and its mathematical
contract has an executable verification case.

## References

- Existing interaction design: `docs/design/load-input-system.md`
- Solver presets and analysis modes: `include/FEASolver.h`
- Solver assembly: `src/FEASolver.cpp`
- Current preset UI: `src/main.cpp`
- Headless regression harness: `src/ScenarioRunner.cpp`
- ANSYS Mechanical support types: <https://ansyshelp.ansys.com/public/Views/Secured/corp/v252/en/wb_sim/ds_Support_Types.html>
- Abaqus coupling constraints: <https://docs.software.vt.edu/abaqusv2025/English/SIMACAECAERefMap/simacae-t-itnhelptopiccoupling.htm>
- COMSOL Boundary Load: <https://doc.comsol.com/6.4/doc/com.comsol.help.sme/sme_ug_solid.07.076.html>
- SOLIDWORKS remote loads: <https://help.solidworks.com/2025/english/solidworks/cworks/IDC_HELP_REMOTE_LOAD.htm>
- Fusion structural constraints: <https://help.autodesk.com/cloudhelp/ENU/Fusion-Simulate/files/SIM-STRUCTURAL-CONSTRAINTS-CONCEPT.htm>
- Altair SimSolid boundary conditions: <https://help.altair.com/ss/en_us/topics/simsolid/chapter_heads/boundary_conditions_ss_r.htm>
