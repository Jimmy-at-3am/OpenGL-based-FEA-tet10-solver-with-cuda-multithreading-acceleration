#pragma once
// =============================================================================
// GeometryLoaderDispatch.h
// =============================================================================
// Factory that maps a file path's extension to the correct IGeometryLoader.
//
// USAGE
// -----
//   LoadedGeometry geo;
//   if (GeometryLoaderDispatch::load(filepath, geo)) {
//       model.processRawGeometry(geo);
//   }
//
// ADDING A NEW FORMAT
// -------------------
//   1. Implement a concrete IGeometryLoader in its own header/cpp.
//   2. Add an entry to makeLoader() below.
//   3. Add the file extension to main.cpp scanForModels().
//
// =============================================================================

#include "IGeometryLoader.h"
#include "STLLoader.h"
#include "ThreeMFLoader.h"
#include "StepLoader.h"

#include <memory>
#include <string>
#include <algorithm>
#include <filesystem>

namespace GeometryLoaderDispatch {

// Returns the appropriate loader for the file at `path`, keyed on extension.
// Returns nullptr if the extension is not recognised.
inline std::unique_ptr<IGeometryLoader> makeLoader(const std::string& path) {
    std::string ext = std::filesystem::path(path).extension().string();
    // Normalise to lower-case
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    if (ext == ".stl")  return std::make_unique<STLLoader>();
    if (ext == ".3mf")  return std::make_unique<ThreeMFLoader>();
    if (ext == ".step" || ext == ".stp") return std::make_unique<StepLoader>();

    return nullptr; // unsupported format
}

// Convenience: make loader and call load() in one call.
// Returns false if extension is unsupported or loading fails.
inline bool load(const std::string& path, LoadedGeometry& out) {
    auto loader = makeLoader(path);
    if (!loader) {
        std::cout << "GeometryLoaderDispatch: unsupported format for '"
                  << path << "'" << std::endl;
        return false;
    }
    return loader->load(path, out);
}

} // namespace GeometryLoaderDispatch
