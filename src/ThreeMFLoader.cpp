#include "ThreeMFLoader.h"
#include <iostream>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <cctype>
#include <algorithm>

// miniz: single-file ZIP library pulled by CMake FetchContent
#include "miniz.h"

namespace fs = std::filesystem;

// =============================================================================
// Internal XML scanner helpers
// =============================================================================
//
// We do NOT use a full XML parser.  The .3mf spec guarantees well-formed XML,
// so we use a fast hand-written scanner that:
//   • Advances a char* cursor through the document.
//   • Recognises element start-tags by name.
//   • Reads named attributes as string/float/int tokens.
// =============================================================================

namespace {

inline void skipWS(const char* doc, size_t& cur, size_t len) {
    while (cur < len && std::isspace(static_cast<unsigned char>(doc[cur])))
        ++cur;
}

bool skipTo(const char* doc, size_t& cur, size_t len, char ch) {
    while (cur < len && doc[cur] != ch) ++cur;
    return cur < len;
}

bool skipTag(const char* doc, size_t& cur, size_t len) {
    bool inQuote = false;
    char quoteChar = 0;
    while (cur < len) {
        char c = doc[cur];
        if (inQuote) {
            if (c == quoteChar) inQuote = false;
        } else {
            if (c == '"' || c == '\'') { inQuote = true; quoteChar = c; }
            else if (c == '>') { ++cur; return true; }
        }
        ++cur;
    }
    return false;
}

// Case-insensitive tag name match.  The match succeeds if the tag is followed
// by whitespace, '/', or '>' (i.e. it is a complete name, not a prefix).
bool tagMatch(const char* doc, size_t cur, size_t len, const char* tag) {
    size_t tlen = std::strlen(tag);
    if (cur + tlen > len) return false;
    for (size_t i = 0; i < tlen; ++i) {
        if (std::tolower(static_cast<unsigned char>(doc[cur + i])) !=
            std::tolower(static_cast<unsigned char>(tag[i])))
            return false;
    }
    char next = (cur + tlen < len) ? doc[cur + tlen] : 0;
    return (std::isspace(static_cast<unsigned char>(next)) || next == '/' || next == '>');
}

// Find the end index of the current tag (the position of '>').
size_t findTagEnd(const char* doc, size_t start, size_t len) {
    bool inQuote = false;
    char qc = 0;
    size_t cur = start;
    while (cur < len) {
        char c = doc[cur];
        if (inQuote) {
            if (c == qc) inQuote = false;
        } else {
            if (c == '"' || c == '\'') { inQuote = true; qc = c; }
            else if (c == '>') return cur;
        }
        ++cur;
    }
    return len;
}

// Read a float-valued attribute named `attrName` from the tag interior
// [start, tagEnd).  Returns true and sets value on success.
bool readAttr(const char* doc, size_t start, size_t tagEnd,
              const char* attrName, float& value) {
    size_t alen = std::strlen(attrName);
    size_t cur = start;
    while (cur < tagEnd) {
        skipWS(doc, cur, tagEnd);
        if (cur >= tagEnd) break;
        bool match = true;
        for (size_t i = 0; i < alen && cur + i < tagEnd; ++i) {
            if (std::tolower(static_cast<unsigned char>(doc[cur + i])) !=
                std::tolower(static_cast<unsigned char>(attrName[i]))) {
                match = false; break;
            }
        }
        if (match && cur + alen < tagEnd) {
            size_t eq = cur + alen;
            while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
            if (eq < tagEnd && doc[eq] == '=') {
                ++eq;
                while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
                if (eq < tagEnd && (doc[eq] == '"' || doc[eq] == '\'')) ++eq;
                char* endPtr = nullptr;
                value = std::strtof(doc + eq, &endPtr);
                return (endPtr != doc + eq);
            }
        }
        // Skip this attribute
        while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
               && doc[cur] != '=') ++cur;
        skipWS(doc, cur, tagEnd);
        if (cur < tagEnd && doc[cur] == '=') {
            ++cur;
            skipWS(doc, cur, tagEnd);
            if (cur < tagEnd && (doc[cur] == '"' || doc[cur] == '\'')) {
                char qc2 = doc[cur++];
                while (cur < tagEnd && doc[cur] != qc2) ++cur;
                if (cur < tagEnd) ++cur;
            } else {
                while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
                       && doc[cur] != '>' && doc[cur] != '/') ++cur;
            }
        }
    }
    return false;
}

// Integer version of readAttr.
bool readAttrInt(const char* doc, size_t start, size_t tagEnd,
                 const char* attrName, int& value) {
    size_t alen = std::strlen(attrName);
    size_t cur = start;
    while (cur < tagEnd) {
        skipWS(doc, cur, tagEnd);
        if (cur >= tagEnd) break;
        bool match = true;
        for (size_t i = 0; i < alen && cur + i < tagEnd; ++i) {
            if (std::tolower(static_cast<unsigned char>(doc[cur + i])) !=
                std::tolower(static_cast<unsigned char>(attrName[i]))) {
                match = false; break;
            }
        }
        if (match && cur + alen < tagEnd) {
            size_t eq = cur + alen;
            while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
            if (eq < tagEnd && doc[eq] == '=') {
                ++eq;
                while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
                if (eq < tagEnd && (doc[eq] == '"' || doc[eq] == '\'')) ++eq;
                char* endPtr = nullptr;
                long lv = std::strtol(doc + eq, &endPtr, 10);
                if (endPtr != doc + eq) { value = static_cast<int>(lv); return true; }
            }
        }
        while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
               && doc[cur] != '=') ++cur;
        skipWS(doc, cur, tagEnd);
        if (cur < tagEnd && doc[cur] == '=') {
            ++cur;
            skipWS(doc, cur, tagEnd);
            if (cur < tagEnd && (doc[cur] == '"' || doc[cur] == '\'')) {
                char qc2 = doc[cur++];
                while (cur < tagEnd && doc[cur] != qc2) ++cur;
                if (cur < tagEnd) ++cur;
            } else {
                while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
                       && doc[cur] != '>' && doc[cur] != '/') ++cur;
            }
        }
    }
    return false;
}

// String version: copies the quoted attribute value into `value`.
bool readAttrString(const char* doc, size_t start, size_t tagEnd,
                    const char* attrName, std::string& value) {
    size_t alen = std::strlen(attrName);
    size_t cur = start;
    while (cur < tagEnd) {
        skipWS(doc, cur, tagEnd);
        if (cur >= tagEnd) break;
        bool match = true;
        for (size_t i = 0; i < alen && cur + i < tagEnd; ++i) {
            if (std::tolower(static_cast<unsigned char>(doc[cur + i])) !=
                std::tolower(static_cast<unsigned char>(attrName[i]))) {
                match = false; break;
            }
        }
        if (match && cur + alen < tagEnd) {
            size_t eq = cur + alen;
            while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
            if (eq < tagEnd && doc[eq] == '=') {
                ++eq;
                while (eq < tagEnd && std::isspace(static_cast<unsigned char>(doc[eq]))) ++eq;
                if (eq < tagEnd && (doc[eq] == '"' || doc[eq] == '\'')) {
                    char q = doc[eq++];
                    size_t valStart = eq;
                    while (eq < tagEnd && doc[eq] != q) ++eq;
                    value.assign(doc + valStart, eq - valStart);
                    return true;
                }
            }
        }
        // Skip this attribute
        while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
               && doc[cur] != '=') ++cur;
        skipWS(doc, cur, tagEnd);
        if (cur < tagEnd && doc[cur] == '=') {
            ++cur;
            skipWS(doc, cur, tagEnd);
            if (cur < tagEnd && (doc[cur] == '"' || doc[cur] == '\'')) {
                char qc2 = doc[cur++];
                while (cur < tagEnd && doc[cur] != qc2) ++cur;
                if (cur < tagEnd) ++cur;
            } else {
                while (cur < tagEnd && !std::isspace(static_cast<unsigned char>(doc[cur]))
                       && doc[cur] != '>' && doc[cur] != '/') ++cur;
            }
        }
    }
    return false;
}

// Parse a whitespace-separated list of 12 floats into a Mat34.
// Returns false if fewer than 12 floats are found.
bool parseMat34(const char* text, size_t len, Mat34& out) {
    const char* p = text;
    const char* end = text + len;
    for (int i = 0; i < 12; ++i) {
        while (p < end && std::isspace(static_cast<unsigned char>(*p))) ++p;
        if (p >= end) return false;
        char* endPtr = nullptr;
        out.m[i] = std::strtof(p, &endPtr);
        if (endPtr == p) return false;
        p = endPtr;
    }
    // Check if it is the identity (detect all-zero-off-diagonal + unit diagonal)
    // to avoid unnecessary matrix multiply later.
    out.isIdentity = (out.m[0]==1&&out.m[1]==0&&out.m[2]==0 &&
                      out.m[3]==0&&out.m[4]==1&&out.m[5]==0 &&
                      out.m[6]==0&&out.m[7]==0&&out.m[8]==1 &&
                      out.m[9]==0&&out.m[10]==0&&out.m[11]==0);
    return true;
}

// Apply a 3×4 column-major transform to positions[firstIdx..end).
void applyMat34(std::vector<glm::vec3>& positions, size_t firstIdx, const Mat34& M) {
    for (size_t i = firstIdx; i < positions.size(); ++i) {
        const glm::vec3 p = positions[i];
        positions[i] = {
            M.m[0]*p.x + M.m[3]*p.y + M.m[6]*p.z + M.m[ 9],
            M.m[1]*p.x + M.m[4]*p.y + M.m[7]*p.z + M.m[10],
            M.m[2]*p.x + M.m[5]*p.y + M.m[8]*p.z + M.m[11],
        };
    }
}

// Read a transform="..." attribute from [start, tagEnd) and parse it.
bool readTransform(const char* doc, size_t start, size_t tagEnd, Mat34& out) {
    std::string ts;
    if (!readAttrString(doc, start, tagEnd, "transform", ts)) return false;
    return parseMat34(ts.c_str(), ts.size(), out);
}

} // anonymous namespace

// =============================================================================
// ThreeMFLoader::parseModelXml
// =============================================================================
bool ThreeMFLoader::parseModelXml(const std::string& xmlStr,
                                  LoadedGeometry&            out,
                                  int                        objectIdFilter,
                                  std::vector<BuildItem>&    outBuildItems,
                                  std::vector<ComponentRef>& outComponentRefs)
{
    const char* doc = xmlStr.c_str();
    const size_t len = xmlStr.size();
    size_t cur = 0;

    int objectCount = 0;
    int baseIndex   = 0;   // global vertex offset for the current object
    int currentObjectId = -1;

    std::vector<glm::vec3> localVerts;
    bool collectMesh = false;  // true when inside an object we should harvest

    enum class Scope { None, Object, Vertices, Triangles, Build, Components };
    Scope scope = Scope::None;

    while (cur < len) {
        if (!skipTo(doc, cur, len, '<')) break;
        ++cur;
        if (cur >= len) break;

        // XML comment
        if (cur + 3 < len && doc[cur]=='!' && doc[cur+1]=='-' && doc[cur+2]=='-') {
            cur += 3;
            while (cur + 2 < len) {
                if (doc[cur]=='-' && doc[cur+1]=='-' && doc[cur+2]=='>') { cur += 3; break; }
                ++cur;
            }
            continue;
        }

        // Processing instruction
        if (cur < len && doc[cur] == '?') {
            while (cur < len && doc[cur] != '>') ++cur;
            if (cur < len) ++cur;
            continue;
        }

        // Closing tag
        if (cur < len && doc[cur] == '/') {
            ++cur;
            if (tagMatch(doc, cur, len, "object") || tagMatch(doc, cur, len, "3mf:object")) {
                if (collectMesh) {
                    baseIndex = static_cast<int>(out.positions.size());
                    for (const auto& v : localVerts) out.positions.push_back(v);
                    localVerts.clear();
                }
                scope = Scope::None;
                currentObjectId = -1;
                collectMesh = false;
            } else if (tagMatch(doc, cur, len, "vertices")) {
                if (scope == Scope::Vertices) scope = Scope::Object;
            } else if (tagMatch(doc, cur, len, "triangles")) {
                if (scope == Scope::Triangles) scope = Scope::Object;
            } else if (tagMatch(doc, cur, len, "build")) {
                if (scope == Scope::Build) scope = Scope::None;
            } else if (tagMatch(doc, cur, len, "components")) {
                if (scope == Scope::Components) scope = Scope::Object;
            }
            skipTo(doc, cur, len, '>');
            if (cur < len) ++cur;
            continue;
        }

        size_t tagStart = cur;
        size_t tagEnd   = findTagEnd(doc, tagStart, len);

        // --- <object ...> ---
        if (tagMatch(doc, tagStart, len, "object")) {
            ++objectCount;
            if (collectMesh && !localVerts.empty()) {
                for (const auto& v : localVerts) out.positions.push_back(v);
                localVerts.clear();
            }
            currentObjectId = -1;
            readAttrInt(doc, tagStart, tagEnd, "id", currentObjectId);
            collectMesh = (objectIdFilter < 0) || (currentObjectId == objectIdFilter);
            if (collectMesh) baseIndex = static_cast<int>(out.positions.size());
            scope = Scope::Object;
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <build> ---
        if (tagMatch(doc, tagStart, len, "build")) {
            scope = Scope::Build;
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <item objectid="N" transform="..."/> (inside <build>) ---
        if (scope == Scope::Build && tagMatch(doc, tagStart, len, "item")) {
            BuildItem item;
            readAttrInt(doc, tagStart, tagEnd, "objectid", item.objectId);
            readTransform(doc, tagStart, tagEnd, item.xform);
            outBuildItems.push_back(item);
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <components> ---
        if (scope == Scope::Object && tagMatch(doc, tagStart, len, "components")) {
            scope = Scope::Components;
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <component p:path="..." objectid="N" transform="..."/> ---
        if (scope == Scope::Components && tagMatch(doc, tagStart, len, "component")) {
            ComponentRef cref;
            readAttrInt(doc, tagStart, tagEnd, "objectid", cref.objectId);
            // p:path is namespaced — match just the local name "path"
            if (!readAttrString(doc, tagStart, tagEnd, "p:path", cref.pPath))
                readAttrString(doc, tagStart, tagEnd, "path", cref.pPath);
            readTransform(doc, tagStart, tagEnd, cref.xform);
            if (!cref.pPath.empty())
                outComponentRefs.push_back(cref);
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <vertices> ---
        if (scope == Scope::Object && tagMatch(doc, tagStart, len, "vertices")) {
            scope = Scope::Vertices;
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <triangles> ---
        if (scope == Scope::Object && tagMatch(doc, tagStart, len, "triangles")) {
            scope = Scope::Triangles;
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <vertex x="..." y="..." z="..."/> ---
        if (scope == Scope::Vertices && collectMesh && tagMatch(doc, tagStart, len, "vertex")) {
            float x = 0.0f, y = 0.0f, z = 0.0f;
            readAttr(doc, tagStart, tagEnd, "x", x);
            readAttr(doc, tagStart, tagEnd, "y", y);
            readAttr(doc, tagStart, tagEnd, "z", z);
            localVerts.push_back(glm::vec3(x, y, z));
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // --- <triangle v1="..." v2="..." v3="..."/> ---
        if (scope == Scope::Triangles && collectMesh && tagMatch(doc, tagStart, len, "triangle")) {
            int v1 = 0, v2 = 0, v3 = 0;
            bool ok1 = readAttrInt(doc, tagStart, tagEnd, "v1", v1);
            bool ok2 = readAttrInt(doc, tagStart, tagEnd, "v2", v2);
            bool ok3 = readAttrInt(doc, tagStart, tagEnd, "v3", v3);
            if (ok1 && ok2 && ok3 && v1 != v2 && v2 != v3 && v1 != v3) {
                out.indices.push_back(static_cast<unsigned int>(baseIndex + v1));
                out.indices.push_back(static_cast<unsigned int>(baseIndex + v2));
                out.indices.push_back(static_cast<unsigned int>(baseIndex + v3));
            }
            cur = tagEnd;
            if (cur < len) ++cur;
            continue;
        }

        // Unrecognised tag — skip past '>'
        skipTag(doc, cur, len);
    }

    // Flush any trailing localVerts (e.g. if </object> was absent)
    if (collectMesh && !localVerts.empty()) {
        for (const auto& v : localVerts) out.positions.push_back(v);
        localVerts.clear();
    }

    out.objectCount = std::max(1, objectCount);
    return !out.positions.empty();
}

// =============================================================================
// ThreeMFLoader::load
// =============================================================================
bool ThreeMFLoader::load(const std::string& path, LoadedGeometry& out) {
    out.clear();
    out.sourceLabel = fs::path(path).filename().string();

    // ------------------------------------------------------------------
    // Open the ZIP archive
    // ------------------------------------------------------------------
    mz_zip_archive zip;
    memset(&zip, 0, sizeof(zip));

    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        std::cout << "ThreeMFLoader: cannot open ZIP '" << path
                  << "' (" << mz_zip_get_error_string(mz_zip_get_last_error(&zip))
                  << ")" << std::endl;
        return false;
    }

    // ------------------------------------------------------------------
    // Read 3D/3dmodel.model (always present per spec)
    // ------------------------------------------------------------------
    const char* kPrimary = "3D/3dmodel.model";
    int primaryIdx = mz_zip_reader_locate_file(&zip, kPrimary, nullptr, 0);
    if (primaryIdx < 0) {
        // Last-resort: find any *.model entry
        int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
        for (int i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
            std::string fname(st.m_filename);
            if (fname.size() >= 6 && fname.substr(fname.size()-6) == ".model") {
                primaryIdx = i;
                std::cout << "ThreeMFLoader: primary not found; using '" << fname << "'." << std::endl;
                break;
            }
        }
    }
    if (primaryIdx < 0) {
        std::cout << "ThreeMFLoader: no .model file in '" << path << "'." << std::endl;
        mz_zip_reader_end(&zip);
        return false;
    }

    size_t primarySize = 0;
    void* primaryData = mz_zip_reader_extract_to_heap(&zip, primaryIdx, &primarySize, 0);
    if (!primaryData) {
        std::cout << "ThreeMFLoader: failed to extract primary model XML." << std::endl;
        mz_zip_reader_end(&zip);
        return false;
    }
    std::string primaryXml(static_cast<const char*>(primaryData), primarySize);
    mz_free(primaryData);

    // ------------------------------------------------------------------
    // new_TODO_04C: read the <model unit="..."> attribute so the real physical
    // size survives import (3MF default is millimeter). Scoped to the <model>
    // root tag so it cannot match a 'unit' substring elsewhere.
    // ------------------------------------------------------------------
    {
        out.fileUnitToMM = 1.0f;
        size_t mp = primaryXml.find("<model");
        size_t te = (mp == std::string::npos) ? std::string::npos : primaryXml.find('>', mp);
        if (mp != std::string::npos && te != std::string::npos) {
            std::string tag = primaryXml.substr(mp, te - mp);
            size_t up = tag.find("unit");
            size_t q1 = (up == std::string::npos) ? std::string::npos : tag.find('"', up);
            size_t q2 = (q1 == std::string::npos) ? std::string::npos : tag.find('"', q1 + 1);
            if (q2 != std::string::npos) {
                std::string u = tag.substr(q1 + 1, q2 - q1 - 1);
                for (auto& c : u) c = (char)std::tolower((unsigned char)c);
                if      (u == "micron")     out.fileUnitToMM = 0.001f;
                else if (u == "millimeter") out.fileUnitToMM = 1.0f;
                else if (u == "centimeter") out.fileUnitToMM = 10.0f;
                else if (u == "inch")       out.fileUnitToMM = 25.4f;
                else if (u == "foot")       out.fileUnitToMM = 304.8f;
                else if (u == "meter")      out.fileUnitToMM = 1000.0f;
                else                        out.fileUnitToMM = 1.0f;
                std::cout << "[3MF] unit: " << u << " (= " << out.fileUnitToMM
                          << " mm/unit; real size preserved)" << std::endl;
            }
        }
    }

    // ------------------------------------------------------------------
    // Parse the primary manifest
    // ------------------------------------------------------------------
    std::vector<BuildItem>    buildItems;
    std::vector<ComponentRef> componentRefs;
    parseModelXml(primaryXml, out, -1, buildItems, componentRefs);

    // ------------------------------------------------------------------
    // Bambu multi-file layout: if the primary has component references
    // but no inline mesh, load each referenced object file from the ZIP.
    // ------------------------------------------------------------------
    if (out.positions.empty() && !componentRefs.empty()) {
        std::cout << "ThreeMFLoader: multi-file layout detected ("
                  << componentRefs.size() << " component ref(s))." << std::endl;

        int loadedObjects = 0;
        for (const ComponentRef& cref : componentRefs) {
            // Strip leading '/' — ZIP entries don't have one.
            std::string entry = cref.pPath;
            if (!entry.empty() && entry[0] == '/') entry.erase(0, 1);

            int idx = mz_zip_reader_locate_file(&zip, entry.c_str(), nullptr, 0);
            if (idx < 0) {
                std::cout << "ThreeMFLoader: component '" << entry << "' not found in ZIP." << std::endl;
                continue;
            }

            size_t childSize = 0;
            void* childData = mz_zip_reader_extract_to_heap(&zip, idx, &childSize, 0);
            if (!childData) {
                std::cout << "ThreeMFLoader: failed to extract '" << entry << "'." << std::endl;
                continue;
            }
            std::string childXml(static_cast<const char*>(childData), childSize);
            mz_free(childData);

            size_t vertBase = out.positions.size();
            std::vector<BuildItem>    _bi;
            std::vector<ComponentRef> _cr;
            parseModelXml(childXml, out, cref.objectId, _bi, _cr);

            // Apply component transform to newly-appended vertices.
            if (!cref.xform.isIdentity)
                applyMat34(out.positions, vertBase, cref.xform);

            ++loadedObjects;
        }

        // Compose build-item transforms (applied on top of component transforms)
        // for any BuildItem that is non-identity.
        // For the common single-plate single-object Bambu case this is a no-op.
        // Full multi-object support would require per-object vertex range tracking;
        // for now, apply the first non-identity build transform to ALL loaded verts.
        for (const BuildItem& bi : buildItems) {
            if (!bi.xform.isIdentity) {
                applyMat34(out.positions, 0, bi.xform);
                break; // single global transform heuristic
            }
        }

        out.objectCount = std::max(1, loadedObjects);
    }

    mz_zip_reader_end(&zip);

    // ------------------------------------------------------------------
    // Diagnose gcode.3mf (geometry-stripped sliced exports)
    // ------------------------------------------------------------------
    if (out.positions.empty()) {
        const std::string& fn = out.sourceLabel;
        bool isGcodeThreeMF = fn.size() > 9 &&
            fn.substr(fn.size()-9) == ".gcode.3mf";
        if (isGcodeThreeMF) {
            std::cout << "ThreeMFLoader: '" << fn << "' appears to be a Bambu Studio\n"
                      << "  sliced export (.gcode.3mf).  Sliced files do NOT contain\n"
                      << "  3D geometry.  Export the project as a plain .3mf file from\n"
                      << "  Bambu Studio (File -> Export -> Export 3MF) and load that." << std::endl;
        } else {
            std::cout << "ThreeMFLoader: no geometry found in '" << path << "'." << std::endl;
        }
        return false;
    }

    std::cout << "ThreeMFLoader: '" << out.sourceLabel << "' -> "
              << out.positions.size() << " verts, "
              << (out.indices.size() / 3) << " tris, "
              << out.objectCount << " object(s)." << std::endl;
    return true;
}
