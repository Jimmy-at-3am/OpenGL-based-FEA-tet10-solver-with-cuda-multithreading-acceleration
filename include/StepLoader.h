#pragma once
// =============================================================================
// StepLoader.h
// =============================================================================
// Loads .step / .stp files into LoadedGeometry via OpenCASCADE.
//
// Two entry points:
//   load()          -- IGeometryLoader interface; fills geometry only.
//                      Used by GeometryLoaderDispatch for format-agnostic paths.
//   loadWithBRep()  -- extended API used by FEAModel::loadSTEP(); also fills
//                      an opaque BRepHandle that retains the analytic B-rep
//                      for exact nearest-point / normal / curvature queries.
//
// When compiled without OCCT (USE_OCCT=OFF / HAS_OCCT not defined):
//   load()        returns false with a console message.
//   loadWithBRep() returns false; brep_out is left empty.
// =============================================================================

#include "IGeometryLoader.h"
#include "FEAData.h"       // FEAParams (sizingChordError)
#include <memory>

class BRepHandle;           // forward — OCC types stay inside BRepHandle_impl.h

class StepLoader : public IGeometryLoader {
public:
    // IGeometryLoader interface — geometry only, no BRep retained.
    bool load(const std::string& path, LoadedGeometry& out) override;

    // Extended load: geometry + analytic B-rep handle.
    // brep_out is set to a valid BRepHandle on success and left unchanged on
    // failure.  params is used for tessellation (sizingChordError, ang_def).
    bool loadWithBRep(const std::string& path,
                      LoadedGeometry& out,
                      std::unique_ptr<BRepHandle>& brep_out,
                      const FEAParams& params);
};
