// =============================================================================
//  GcodeToolpathLoader.cpp  --  new_TODO_19A: Bambu .gcode.3mf -> ToolpathModel.
//
//  Port of code/fea_slicer/gcode_viewer/gcode_parser.cpp (kept line-for-line
//  where possible so the prototype remains the reference implementation).
//  Differences, all deliberate and covered by scenario asserts:
//    - featureCounts[] tallied per emitted segment (Sum == segments.size()).
//    - Object bbox excludes print AIDS (support/interface/brim) in addition to
//      FT_CUSTOM: bbMin/bbMax = the part the FEA simulates.
//    - Options::partOnly drops aid segments entirely (negative-control knob).
//
//  Parser care points inherited from the prototype:
//    - Bambu emits M83 (RELATIVE E) -- assuming absolute E classifies every
//      move as travel and yields 0 segments (asserted against).
//    - G2/G3 arcs tessellated at ~0.3 mm sagitta step, 2..400 subsegments.
// =============================================================================
#include "GcodeToolpathLoader.h"

#include <cstring>
#include <cstdlib>
#include <cmath>
#include <cctype>
#include <algorithm>
#include <iostream>

#include "miniz.h"

namespace Toolpath {

static const double PI = 3.14159265358979323846;

bool isPrintAid(int f) {
    return f == FT_SUPPORT || f == FT_SUPPORT_INTERFACE ||
           f == FT_BRIM    || f == FT_CUSTOM;
}

int featureFromString(const std::string& s) {
    if (s == "Outer wall")              return FT_OUTER_WALL;
    if (s == "Inner wall")              return FT_INNER_WALL;
    if (s == "Overhang wall")           return FT_OVERHANG_WALL;
    if (s == "Internal solid infill")   return FT_SOLID_INFILL;
    if (s == "Sparse infill")           return FT_SPARSE_INFILL;
    if (s == "Top surface")             return FT_TOP_SURFACE;
    if (s == "Bottom surface")          return FT_BOTTOM_SURFACE;
    if (s == "Bridge")                  return FT_BRIDGE;
    if (s == "Gap infill")              return FT_GAP_INFILL;
    if (s == "Support")                 return FT_SUPPORT;
    if (s == "Support interface")       return FT_SUPPORT_INTERFACE;
    if (s == "Support transition")      return FT_SUPPORT_INTERFACE;
    if (s == "Brim")                    return FT_BRIM;
    if (s == "Floating vertical shell") return FT_FLOATING_SHELL;
    if (s == "Custom")                  return FT_CUSTOM;
    return FT_OTHER;
}

const char* featureName(int f) {
    switch (f) {
        case FT_OUTER_WALL:        return "Outer wall";
        case FT_INNER_WALL:        return "Inner wall";
        case FT_OVERHANG_WALL:     return "Overhang wall";
        case FT_SOLID_INFILL:      return "Internal solid infill";
        case FT_SPARSE_INFILL:     return "Sparse infill";
        case FT_TOP_SURFACE:       return "Top surface";
        case FT_BOTTOM_SURFACE:    return "Bottom surface";
        case FT_BRIDGE:            return "Bridge";
        case FT_GAP_INFILL:        return "Gap infill";
        case FT_SUPPORT:           return "Support";
        case FT_SUPPORT_INTERFACE: return "Support interface";
        case FT_BRIM:              return "Brim";
        case FT_FLOATING_SHELL:    return "Floating vertical shell";
        case FT_CUSTOM:            return "Custom";
        default:                   return "Other";
    }
}

} // namespace Toolpath

namespace GcodeToolpathLoader {

using namespace Toolpath;

// Value of parameter letter `c` in a tokenized move line (toks[0] = command).
static double paramVal(const std::vector<std::string>& toks, char c, bool& found) {
    for (size_t i = 1; i < toks.size(); ++i) {
        if (!toks[i].empty() &&
            std::toupper(static_cast<unsigned char>(toks[i][0])) == c) {
            found = true;
            return std::atof(toks[i].c_str() + 1);
        }
    }
    found = false;
    return 0.0;
}

bool load(const std::string& path, const Options& opt,
          ToolpathModel& out, std::string& err)
{
    mz_zip_archive zip;
    std::memset(&zip, 0, sizeof(zip));
    if (!mz_zip_reader_init_file(&zip, path.c_str(), 0)) {
        err = "cannot open 3mf archive: " + path;
        return false;
    }
    int idx = mz_zip_reader_locate_file(&zip, "Metadata/plate_1.gcode", nullptr, 0);
    if (idx < 0) {
        int n = static_cast<int>(mz_zip_reader_get_num_files(&zip));
        for (int i = 0; i < n; ++i) {
            mz_zip_archive_file_stat st;
            if (!mz_zip_reader_file_stat(&zip, i, &st)) continue;
            std::string fn = st.m_filename;
            if (fn.size() > 6 && fn.substr(fn.size() - 6) == ".gcode") { idx = i; break; }
        }
    }
    if (idx < 0) {
        mz_zip_reader_end(&zip);
        err = "no .gcode entry inside 3mf (is this a sliced .gcode.3mf export?)";
        return false;
    }

    size_t sz = 0;
    void* data = mz_zip_reader_extract_to_heap(&zip, idx, &sz, 0);
    if (!data) {
        mz_zip_reader_end(&zip);
        err = std::string("failed to extract gcode from 3mf: ") +
              mz_zip_get_error_string(mz_zip_get_last_error(&zip));
        return false;
    }
    std::string g(static_cast<const char*>(data), sz);
    mz_free(data);
    mz_zip_reader_end(&zip);

    // --- parse state (identical to the prototype) ---
    double X = 0, Y = 0, Z = 0, ePrev = 0;
    bool absPos = true;     // G90
    bool relE   = true;     // M83 (Bambu default; files emit no M82)
    int    curFeature = FT_OTHER;
    double curWidth   = 0.42;
    double curHeight  = 0.20;
    int    layer      = -1;

    out = ToolpathModel{};
    auto& segs = out.segments;
    segs.reserve(200000);
    glm::vec3 bbmin(1e9f), bbmax(-1e9f), rbmin(1e9f), rbmax(-1e9f);

    auto track = [&](double px, double py, double pz, int feat) {
        glm::vec3 p(static_cast<float>(px), static_cast<float>(py),
                    static_cast<float>(pz));
        rbmin = glm::min(rbmin, p); rbmax = glm::max(rbmax, p);
        if (!isPrintAid(feat)) { bbmin = glm::min(bbmin, p); bbmax = glm::max(bbmax, p); }
    };
    auto emit = [&](double x0, double y0, double z0,
                    double x1, double y1, double z1) {
        track(x0, y0, z0, curFeature);
        track(x1, y1, z1, curFeature);
        if (opt.partOnly && isPrintAid(curFeature)) return;
        Segment s;
        s.p0 = glm::vec3((float)x0, (float)y0, (float)z0);
        s.p1 = glm::vec3((float)x1, (float)y1, (float)z1);
        s.width   = static_cast<float>(curWidth);
        s.height  = static_cast<float>(curHeight);
        s.feature = curFeature;
        s.layer   = layer < 0 ? 0 : layer;
        segs.push_back(s);
        out.featureCounts[curFeature >= 0 && curFeature < FT_COUNT ? curFeature
                                                                   : FT_OTHER]++;
    };

    std::vector<std::string> toks;
    toks.reserve(12);
    auto trim = [](std::string s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        size_t b = s.find_last_not_of(" \t\r\n");
        return (a == std::string::npos) ? std::string() : s.substr(a, b - a + 1);
    };

    size_t pos = 0, N = g.size();
    while (pos < N) {
        size_t nl = g.find('\n', pos);
        if (nl == std::string::npos) nl = N;
        std::string line = g.substr(pos, nl - pos);
        pos = nl + 1;

        size_t first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos) continue;

        if (line[first] == ';') {
            std::string c = line.substr(first);
            if (c.rfind("; CHANGE_LAYER", 0) == 0)        { ++layer; }
            else if (c.rfind("; LAYER_HEIGHT:", 0) == 0)  { curHeight = std::atof(c.c_str() + 15); }
            else if (c.rfind("; LINE_WIDTH:", 0) == 0)    { curWidth  = std::atof(c.c_str() + 13); }
            else if (c.rfind("; FEATURE:", 0) == 0)       { curFeature = featureFromString(trim(c.substr(10))); }
            continue;
        }

        std::string code = line.substr(first);
        size_t sc = code.find(';');
        if (sc != std::string::npos) code = code.substr(0, sc);
        toks.clear();
        {
            size_t i = 0, M = code.size();
            while (i < M) {
                while (i < M && std::isspace(static_cast<unsigned char>(code[i]))) ++i;
                size_t j = i;
                while (j < M && !std::isspace(static_cast<unsigned char>(code[j]))) ++j;
                if (j > i) toks.push_back(code.substr(i, j - i));
                i = j;
            }
        }
        if (toks.empty()) continue;
        const std::string& cmd = toks[0];

        if (cmd == "G0" || cmd == "G1") {
            bool fx, fy, fz, fe;
            double nx = paramVal(toks, 'X', fx);
            double ny = paramVal(toks, 'Y', fy);
            double nz = paramVal(toks, 'Z', fz);
            double ev = paramVal(toks, 'E', fe);
            double tx = fx ? (absPos ? nx : X + nx) : X;
            double ty = fy ? (absPos ? ny : Y + ny) : Y;
            double tz = fz ? (absPos ? nz : Z + nz) : Z;
            double dE = 0;
            if (fe) { if (relE) dE = ev; else { dE = ev - ePrev; ePrev = ev; } }
            if (dE > 1e-6 && (fx || fy)) emit(X, Y, Z, tx, ty, tz);
            X = tx; Y = ty; Z = tz;
        }
        else if (cmd == "G2" || cmd == "G3") {
            bool fi, fj, fx, fy, fz, fe;
            double I  = paramVal(toks, 'I', fi);
            double J  = paramVal(toks, 'J', fj);
            double ex = paramVal(toks, 'X', fx);
            double ey = paramVal(toks, 'Y', fy);
            double ez = paramVal(toks, 'Z', fz);
            double ev = paramVal(toks, 'E', fe);
            double dE = 0;
            if (fe) { if (relE) dE = ev; else { dE = ev - ePrev; ePrev = ev; } }
            bool extruding = dE > 1e-6;
            bool cw = (cmd == "G2");

            double cx = X + (fi ? I : 0.0);
            double cy = Y + (fj ? J : 0.0);
            double r  = std::hypot(X - cx, Y - cy);
            double endx = fx ? (absPos ? ex : X + ex) : X;
            double endy = fy ? (absPos ? ey : Y + ey) : Y;
            double endz = fz ? (absPos ? ez : Z + ez) : Z;
            bool full = !(fx || fy);

            double a0 = std::atan2(Y - cy, X - cx);
            double a1 = std::atan2(endy - cy, endx - cx);
            double sweep;
            if (full) {
                sweep = cw ? -2 * PI : 2 * PI;
            } else if (cw) {
                sweep = a1 - a0;
                while (sweep > 0) sweep -= 2 * PI;
                if (std::abs(sweep) < 1e-9) sweep = -2 * PI;
            } else {
                sweep = a1 - a0;
                while (sweep < 0) sweep += 2 * PI;
                if (std::abs(sweep) < 1e-9) sweep = 2 * PI;
            }

            int n = static_cast<int>(std::ceil(std::abs(sweep) * r / 0.3));
            if (n < 2) n = 2;
            if (n > 400) n = 400;
            double px = X, py = Y, pz = Z;
            for (int i = 1; i <= n; ++i) {
                double t = static_cast<double>(i) / n;
                double ang = a0 + sweep * t;
                double qx = cx + r * std::cos(ang);
                double qy = cy + r * std::sin(ang);
                double qz = Z + (endz - Z) * t;
                if (extruding) emit(px, py, pz, qx, qy, qz);
                px = qx; py = qy; pz = qz;
            }
            if (extruding) out.arcSegments += n;
            X = endx; Y = endy; Z = endz;
        }
        else if (cmd == "G92") {
            bool fe; double ev = paramVal(toks, 'E', fe); if (fe) ePrev = ev;
        }
        else if (cmd == "G90") absPos = true;
        else if (cmd == "G91") absPos = false;
        else if (cmd == "M82") relE = false;
        else if (cmd == "M83") relE = true;
    }

    if (segs.empty()) { err = "parsed 0 extrusion segments"; return false; }
    out.layerCount = layer + 1;
    out.bbMin = bbmin; out.bbMax = bbmax;
    out.rawMin = rbmin; out.rawMax = rbmax;

    std::cout << "[GCODE] " << path << ": layers=" << out.layerCount
              << " segments=" << segs.size()
              << " arcSubSegs=" << out.arcSegments
              << " part bbox mm=(" << (bbmax.x - bbmin.x) << " x "
              << (bbmax.y - bbmin.y) << " x " << (bbmax.z - bbmin.z) << ")"
              << (opt.partOnly ? "  [part-only]" : "") << "\n";
    return true;
}

} // namespace GcodeToolpathLoader
