// =============================================================================
//  ToolpathSections.cpp  --  new_TODO_19B implementation. See header.
//
//  Numerical care points:
//   - Union fill rule is NonZero, NOT EvenOdd: bead rectangles overlap heavily
//     (adjacent passes, wall seams) and EvenOdd would punch phantom holes at
//     every overlap. All input rectangles are emitted CCW -> positive winding.
//   - Fixed-point grid: 1000 units/mm (1 µm). Plate is <= 256 mm, so
//     coordinates stay < 2.6e5 * 1e3 = 2.6e8 << int64 limits, and Clipper2's
//     robustness guarantees hold.
//   - Morphological close (inflate +g then -g, Round joins) is what "welds
//     little corners and gaps that should not matter to simulation" (owner
//     wording): any gap narrower than 2g fuses; already-solid regions return
//     to their original outline up to corner rounding.
// =============================================================================
#include "ToolpathSections.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>

#ifdef HAS_CLIPPER2
#include "clipper2/clipper.h"
#endif

namespace ToolpathSections {

using LayerSlicer::Section;

bool build(const Toolpath::ToolpathModel& tp, const Options& opt,
           LayerSections& out, std::string& err)
{
#ifndef HAS_CLIPPER2
    (void)tp; (void)opt; (void)out;
    err = "ToolpathSections requires Clipper2 (build with USE_CLIPPER2=ON); "
          "exact polygon union is load-bearing for bead welding";
    return false;
#else
    using namespace Clipper2Lib;
    out = LayerSections{};
    if (tp.segments.empty() || tp.layerCount <= 0) {
        err = "toolpath has no segments/layers";
        return false;
    }

    // ---- median bead width (drives the auto knobs) ----
    {
        std::vector<float> widths;
        widths.reserve(tp.segments.size());
        for (const auto& s : tp.segments) widths.push_back(s.width);
        std::nth_element(widths.begin(), widths.begin() + widths.size() / 2,
                         widths.end());
        out.medianWidthMM = widths[widths.size() / 2];
    }
    const float g   = (opt.closeGapMM >= 0.0f) ? opt.closeGapMM
                                               : 0.5f * out.medianWidthMM;
    const float eps = std::max(1e-4f, opt.simplifyMM);
    const float dust = (opt.minAreaMM2 >= 0.0f)
                           ? opt.minAreaMM2
                           : 0.25f * out.medianWidthMM * out.medianWidthMM;
    out.closeGapUsedMM = g;

    const glm::vec3 centre = 0.5f * (tp.bbMin + tp.bbMax);
    const double S = 1000.0;                    // int64 units per mm (1 µm)

    // ---- bucket segments per layer ----
    std::vector<std::vector<const Toolpath::Segment*>> perLayer(tp.layerCount);
    for (const auto& s : tp.segments) {
        if (!opt.includeAids && Toolpath::isPrintAid(s.feature)) continue;
        if (s.layer >= 0 && s.layer < tp.layerCount)
            perLayer[s.layer].push_back(&s);
    }

    out.sections.resize(tp.layerCount);
    out.zTopMM.resize(tp.layerCount, 0.0f);
    out.heightMM.resize(tp.layerCount, 0.0f);
    out.netAreaMM2.resize(tp.layerCount, 0.0);

    float lastZ = tp.bbMin.z - centre.z, lastH = 0.2f;
    for (int L = 0; L < tp.layerCount; ++L) {
        const auto& segs = perLayer[L];
        if (segs.empty()) {
            // aid-only or empty layer: keep an empty section, interpolate z.
            out.zTopMM[L]  = lastZ + lastH;
            out.heightMM[L] = lastH;
            lastZ = out.zTopMM[L];
            continue;
        }

        // Layer plane: median of segment end z (part-centered) + median height.
        {
            std::vector<float> zs, hs;
            zs.reserve(segs.size()); hs.reserve(segs.size());
            for (const auto* s : segs) {
                zs.push_back(s->p1.z - centre.z);
                hs.push_back(s->height);
            }
            std::nth_element(zs.begin(), zs.begin() + zs.size() / 2, zs.end());
            std::nth_element(hs.begin(), hs.begin() + hs.size() / 2, hs.end());
            out.zTopMM[L]   = zs[zs.size() / 2];
            out.heightMM[L] = std::max(1e-3f, hs[hs.size() / 2]);
            lastZ = out.zTopMM[L];
            lastH = out.heightMM[L];
        }

        // ---- 1. rectangle-ize every bead (CCW; endpoints extended by w/2) ----
        Paths64 rects;
        rects.reserve(segs.size());
        for (const auto* s : segs) {
            const glm::vec2 a(s->p0.x - centre.x, s->p0.y - centre.y);
            const glm::vec2 b(s->p1.x - centre.x, s->p1.y - centre.y);
            const float h = 0.5f * s->width;
            glm::vec2 u = b - a;
            const float len = std::sqrt(u.x * u.x + u.y * u.y);
            glm::vec2 c0, c1, c2, c3;
            if (len < 1e-6f) {                       // dot -> w x w square
                c0 = a + glm::vec2(-h, -h); c1 = a + glm::vec2( h, -h);
                c2 = a + glm::vec2( h,  h); c3 = a + glm::vec2(-h,  h);
            } else {
                u /= len;
                const glm::vec2 n(-u.y, u.x);
                const glm::vec2 a2 = a - u * h, b2 = b + u * h;
                c0 = a2 - n * h; c1 = b2 - n * h; c2 = b2 + n * h; c3 = a2 + n * h;
            }
            Path64 r;
            r.reserve(4);
            r.push_back(Point64((int64_t)std::llround(c0.x * S), (int64_t)std::llround(c0.y * S)));
            r.push_back(Point64((int64_t)std::llround(c1.x * S), (int64_t)std::llround(c1.y * S)));
            r.push_back(Point64((int64_t)std::llround(c2.x * S), (int64_t)std::llround(c2.y * S)));
            r.push_back(Point64((int64_t)std::llround(c3.x * S), (int64_t)std::llround(c3.y * S)));
            rects.push_back(std::move(r));
        }

        // ---- 2. union + weld (close) + simplify + dust filter ----
        Paths64 solid = Union(rects, FillRule::NonZero);
        if (g > 1e-6f) {
            Paths64 grown  = InflatePaths(solid,  (double)g * S, JoinType::Round, EndType::Polygon);
            Paths64 closed = InflatePaths(grown, -(double)g * S, JoinType::Round, EndType::Polygon);
            if (!closed.empty()) solid = std::move(closed);
        }
        Paths64 simp = SimplifyPaths(solid, (double)eps * S);
        const Paths64& src = simp.empty() ? solid : simp;

        std::vector<std::vector<glm::vec2>> loops;
        loops.reserve(src.size());
        const double dustGrid = (double)dust * S * S;
        for (const auto& path : src) {
            if (path.size() < 3) continue;
            if (std::abs(Area(path)) < dustGrid) continue;
            std::vector<glm::vec2> lp;
            lp.reserve(path.size());
            for (const auto& pt : path)
                lp.emplace_back((float)((double)pt.x / S),
                                (float)((double)pt.y / S));
            loops.push_back(std::move(lp));
        }

        Section sec = LayerSlicer::classifySection(std::move(loops));
        double net = 0.0;
        for (const auto& poly : sec) {
            net += LayerSlicer::signedArea(poly.pts); // outer>0, hole<0
            ++out.totalLoops;
            if (poly.isHole) ++out.totalHoles;
        }
        out.netAreaMM2[L] = net;
        out.sections[L]   = std::move(sec);
    }

    std::cout << "[TPSEC] layers=" << tp.layerCount
              << " closeGapMM=" << g
              << " loops=" << out.totalLoops
              << " holes=" << out.totalHoles << "\n";
    return true;
#endif
}

} // namespace ToolpathSections
