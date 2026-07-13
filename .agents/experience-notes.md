# Project experience notes

## Immediate-mode overlays and controls

- Draw order is not enough to make an overlay modal. `SimpleUI` widgets perform
  hit testing when they are called, so a control drawn after an overlay can
  still intercept clicks through that overlay.
- The README panel covers the same left-edge strip as the sectional-view
  slider. When `showReadme` is true, do not draw or call `vslider` for the
  section control. Preserve the current cut value and model clipping state;
  hiding the control must not reset an active section.
- When adding a new immediate-mode control, check both visual overlap and input
  overlap. Put the visibility/modal condition around the widget call itself,
  not only around its drawing primitives.

## Sectional-view state

- Keep the section value in persistent UI state keyed by the loaded model, and
  update `model.sectionEnabled`/`sectionZModel` only when the slider is active.
  A hidden slider should leave the last section unchanged.
- Convert the slider's physical Z value through the model's physical bounding
  box and `modelToMM`; shader clipping is in centered model coordinates, not
  raw file millimetres.

## Toolpath meshing and physical layers

- Never infer printed-layer count from FE slab count. Keep the source
  toolpath's layer count visible and report the grouping factor `k` used by the
  FE mesh.
- A low `maxSlabs` value creates stepped geometry and can erase small-object
  detail. The interactive default is deliberately high (128); explicit
  scenario caps remain authoritative for regression fixtures.

## Background computation

- A worker must not issue OpenGL calls. Set `deferGLUpload` while it runs and
  rebuild/upload buffers on the render thread after joining.
- Progress and cancellation must be checked at stage/layer/iteration
  boundaries. Third-party TetGen has no cooperative stop hook, so cancellation
  during that call can only discard its local result after it returns.
- Keep the right-side controls input-locked while work runs, but leave the
  bottom-left progress panel's cancel button interactive.

## Load visualization

- Force arrows should come from the assembled external vector and solved
  restraint reactions. For bending, showing only a vertical applied arrow is
  incomplete; supports/clamps exert real reaction forces and their spatial
  distribution represents the bending moment.

## CUDA PCG solver

- Do not change the iteration limit to `min(n, 10000)`. Project regression
  solves on the equivalent CPU CG path have required 9,887 and approximately
  13,000 iterations. A fixed 10,000-iteration ceiling therefore rejects valid
  finer-mesh solves.
- Keep `CG_MAX_ITER = max(n, 10000)` unless a separately validated,
  user-configurable stopping policy replaces it. The finite-residual,
  nonpositive-denominator, invalid-diagonal, and numerical-breakdown checks
  provide safety; an arbitrary low iteration ceiling does not.
- A GPU convergence failure invokes the CPU fallback. If the GPU performs
  10,000 useful iterations first, the fallback repeats the entire solve and
  can nearly double total time. Always evaluate iteration-limit changes as an
  end-to-end GPU-plus-fallback cost, not merely as protection against a long
  kernel run.
- GPU PCG may report success only after a finite residual satisfies the stated
  tolerance. Zero right-hand sides should return the zero solution directly,
  while non-SPD matrices and numerical breakdown must return failure so the
  existing CPU fallback remains available.
- Pass an explicitly compressed Eigen row-major CSR matrix to cuSPARSE. Do not
  reinterpret default column-major CSC arrays as CSR based only on expected
  symmetry; assembly and nonlinear tangent changes can violate that implicit
  storage assumption.
- `cusparseSpMV_preprocess()` is appropriate because PCG repeatedly multiplies
  by the same sparse matrix. First-use CUDA context/JIT initialization and
  repeated handle/allocation setup are separate sources of latency from the
  actual iterative progress.
- On the shared NVIDIA T1200, do not judge or test this solver by GPU usage and
  do not start competing heavy GPU workloads when another task is active.
  Judge progress by completed iterations, residual reduction, convergence,
  solution accuracy, timing, and whether CPU fallback was triggered.
