#pragma once
// =============================================================================
// IGeometryLoader.h
// =============================================================================
// Abstract interface for all geometry file format loaders.
//
// CONTRACT
// --------
// A concrete loader MUST:
//   - Parse the file at `path` into a flat triangle soup of (positions, indices).
//   - Guarantee indices are valid (< positions.size()).
//   - Leave positions in the original file coordinate system (mm or m as
//     authored).  FEAModel::processRawGeometry() handles centering / scaling.
//   - Set sourceLabel to the bare filename (no directory component).
//   - Return false and leave `out` unchanged on any parse failure.
//
// Adding a new format
// -------------------
//   1. Create ConcreteLoader.h / ConcreteLoader.cpp.
//   2. Implement IGeometryLoader::load().
//   3. Register the file extension in GeometryLoaderDispatch.h :: makeLoader().
//
// =============================================================================

#include <string>
#include <vector>
#include <glm/glm.hpp>

// ---------------------------------------------------------------------------
// LoadedGeometry
// ---------------------------------------------------------------------------
// Plain-data struct filled by every loader and consumed by
// FEAModel::processRawGeometry().
// ---------------------------------------------------------------------------
struct LoadedGeometry {
    std::vector<glm::vec3>    positions;   // one entry per unique welded vertex
    std::vector<unsigned int> indices;     // 3 entries per triangle

    // Human-readable label shown in the UI (e.g. "bracket.3mf").
    std::string sourceLabel;

    // Optional: number of logical "objects" found in the file.
    // STL always reports 1.  .3mf may report N (one per <object> element).
    int objectCount = 1;

    // new_TODO_04C: physical unit of the authored coordinates, as a factor to
    // millimetres (the canonical internal length unit). STL is unitless -> 1.0
    // (treated as mm; FEAModel applies FEAParams::stlUnitToMM as an override).
    // 3MF reads <model unit=...>; STEP reads xstep.cascade.unit. Used by
    // FEAModel::processRawGeometry to record the real physical size before the
    // 3-unit render normalisation.
    float fileUnitToMM = 1.0f;

    // Clear all fields (allows reuse without re-allocation overhead).
    void clear() {
        positions.clear();
        indices.clear();
        sourceLabel.clear();
        objectCount = 1;
        fileUnitToMM = 1.0f;
    }
};

// ---------------------------------------------------------------------------
// IGeometryLoader
// ---------------------------------------------------------------------------
class IGeometryLoader {
public:
    virtual ~IGeometryLoader() = default;

    // Load geometry from the file at `path` into `out`.
    // Returns true on success, false on any error.
    // On failure the content of `out` is undefined.
    virtual bool load(const std::string& path, LoadedGeometry& out) = 0;
};
