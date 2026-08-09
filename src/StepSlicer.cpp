// =============================================================================
//  StepSlicer.cpp  --  B-rep section seam (LayerSlicer::sliceBRepPlane).
//
//  This is the ONLY translation unit permitted to include OCC headers. For
//  now the body is a compiling STUB that returns false, so every caller
//  (LayerSlicer::computeSlices) transparently falls back to the triangle-mesh
//  section path. The analytic OCC section path (BRepAlgoAPI_Section against an
//  XY/XZ/YZ plane, edge -> ordered-loop assembly, units handling) is not yet
//  implemented; it will live entirely behind the HAS_OCCT guard below.
//
//  Keeping the seam in its own TU means the triangle path in LayerSlicer.cpp
//  never drags in OCC, and the rest of the codebase compiles identically whether
//  or not OCCT is present.
// =============================================================================
#include "LayerSlicer.h"
#include "BRepHandle.h"   // OCC-free interface (forward-declared in LayerSlicer.h)

namespace LayerSlicer {

bool sliceBRepPlane(const BRepHandle& brep, int axis, float coord, double Ldiag,
                    Section& out) {
    // TODO: replace this stub with the analytic OCC section path. Until
    // then we report "no B-rep section available" so computeSlices() uses the
    // always-present triangle-mesh path. Touch params to silence -Wunused.
    (void)brep; (void)axis; (void)coord; (void)Ldiag; (void)out;
    return false;
}

} // namespace LayerSlicer
