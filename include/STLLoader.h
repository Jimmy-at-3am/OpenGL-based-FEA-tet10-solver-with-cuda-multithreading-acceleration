#pragma once
// =============================================================================
// STLLoader.h
// =============================================================================
// Loads binary STL files into LoadedGeometry.
//
// This is a direct extraction of the triangle-reading + vertex-welding logic
// that previously lived inside FEAModel::loadSTL.  Only the geometry parsing
// is here; all post-processing (decimation, normal recomputation, bbox
// centering) remains in FEAModel::processRawGeometry.
//
// ASCII STL is not supported (returns false with a console message).
// The file size check (84 + nTris * 50 bytes) distinguishes binary from ASCII.
// =============================================================================

#include "IGeometryLoader.h"
#include <fstream>
#include <iostream>
#include <unordered_map>
#include <tuple>
#include <filesystem>

class STLLoader : public IGeometryLoader {
public:
    bool load(const std::string& path, LoadedGeometry& out) override;
};
