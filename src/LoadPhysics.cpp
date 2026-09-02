#include "LoadPhysics.h"

#include <cmath>
#include <stdexcept>

namespace load_physics {
namespace {

CapabilityResult exact(const char* reason) noexcept {
    return {Capability::Exact, reason};
}

CapabilityResult approximate(const char* reason) noexcept {
    return {Capability::Approximate, reason};
}

CapabilityResult unsupported(const char* reason) noexcept {
    return {Capability::Unsupported, reason};
}

bool isFinite(const Vec3& value) noexcept {
    return std::isfinite(value.x) &&
           std::isfinite(value.y) &&
           std::isfinite(value.z);
}

double norm(const Vec3& value) noexcept {
    return std::hypot(std::hypot(value.x, value.y), value.z);
}

Vec3 subtract(const Vec3& left, const Vec3& right) noexcept {
    return {
        left.x - right.x,
        left.y - right.y,
        left.z - right.z,
    };
}

Vec3 scaled(const Vec3& value, double scale) noexcept {
    return {
        value.x * scale,
        value.y * scale,
        value.z * scale,
    };
}

Vec3 cross(const Vec3& left, const Vec3& right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

void add(Vec3& target, const Vec3& value) noexcept {
    target.x += value.x;
    target.y += value.y;
    target.z += value.z;
}

bool isKnown(AnalysisMode mode) noexcept {
    switch (mode) {
        case AnalysisMode::LinearStatic:
        case AnalysisMode::NonlinearStatic:
        case AnalysisMode::BrittleFracture:
            return true;
    }
    return false;
}

bool isKnown(FeatureKind kind) noexcept {
    switch (kind) {
        case FeatureKind::PointForce:
        case FeatureKind::PatchResultant:
        case FeatureKind::BoundaryTraction:
        case FeatureKind::Pressure:
        case FeatureKind::BearingLoad:
        case FeatureKind::RemoteResultant:
        case FeatureKind::BodyAcceleration:
        case FeatureKind::FixedConstraint:
        case FeatureKind::PrescribedDisplacement:
        case FeatureKind::NormalSupport:
        case FeatureKind::CylindricalSupport:
        case FeatureKind::ElasticSupport:
        case FeatureKind::Contact:
            return true;
    }
    return false;
}

bool isKnown(MagnitudeScope scope) noexcept {
    switch (scope) {
        case MagnitudeScope::TotalAcrossSelection:
        case MagnitudeScope::PerRegion:
            return true;
    }
    return false;
}

bool isKnown(DistributionKind distribution) noexcept {
    switch (distribution) {
        case DistributionKind::Unspecified:
        case DistributionKind::ConcentratedNode:
        case DistributionKind::ConsistentArea:
        case DistributionKind::LinearFacetTributary:
        case DistributionKind::EqualNode:
            return true;
    }
    return false;
}

bool isKnown(FrameKind frame) noexcept {
    switch (frame) {
        case FrameKind::Global:
        case FrameKind::BuildMaterial:
        case FrameKind::ReferenceGeometry:
        case FrameKind::CurrentBoundary:
            return true;
    }
    return false;
}

bool isKnown(EvolutionKind evolution) noexcept {
    switch (evolution) {
        case EvolutionKind::Dead:
        case EvolutionKind::Follower:
            return true;
    }
    return false;
}

bool isKnown(PresetKind preset) noexcept {
    switch (preset) {
        case PresetKind::CantileverBendingZ:
        case PresetKind::PointForceZ:
        case PresetKind::SurfaceCompressionY:
        case PresetKind::TensionX:
        case PresetKind::TensionY:
        case PresetKind::TensionZ:
            return true;
    }
    return false;
}

}  // namespace

const char* capabilityName(Capability capability) noexcept {
    switch (capability) {
        case Capability::Exact:       return "EXACT";
        case Capability::Approximate: return "APPROX";
        case Capability::Unsupported: return "BLOCKED";
    }
    return "BLOCKED";
}

const char* featureName(FeatureKind feature) noexcept {
    switch (feature) {
        case FeatureKind::PointForce:              return "POINT FORCE";
        case FeatureKind::PatchResultant:          return "PATCH RESULTANT";
        case FeatureKind::BoundaryTraction:        return "BOUNDARY TRACTION";
        case FeatureKind::Pressure:                return "PRESSURE";
        case FeatureKind::BearingLoad:             return "BEARING LOAD";
        case FeatureKind::RemoteResultant:         return "REMOTE RESULTANT";
        case FeatureKind::BodyAcceleration:        return "BODY ACCELERATION";
        case FeatureKind::FixedConstraint:         return "FIXED CONSTRAINT";
        case FeatureKind::PrescribedDisplacement:  return "PRESCRIBED DISPLACEMENT";
        case FeatureKind::NormalSupport:           return "NORMAL SUPPORT";
        case FeatureKind::CylindricalSupport:      return "CYLINDRICAL SUPPORT";
        case FeatureKind::ElasticSupport:          return "ELASTIC SUPPORT";
        case FeatureKind::Contact:                 return "CONTACT";
    }
    return "UNKNOWN FEATURE";
}

CapabilityResult assess(
    const FeatureDefinition& feature,
    AnalysisMode mode) noexcept {
    if (!isKnown(mode) ||
        !isKnown(feature.kind) ||
        !isKnown(feature.scope) ||
        !isKnown(feature.distribution) ||
        !isKnown(feature.frame) ||
        !isKnown(feature.evolution)) {
        return unsupported(
            "Invalid load/support contract value.");
    }

    if (feature.evolution == EvolutionKind::Follower) {
        return unsupported(
            "Follower loads require a state-dependent residual and tangent.");
    }
    if (feature.frame == FrameKind::CurrentBoundary) {
        return unsupported(
            "The current-boundary frame is unavailable for a frozen load vector.");
    }
    if (feature.frame == FrameKind::BuildMaterial) {
        return unsupported(
            "Build/material-frame load transformation is not assembled yet.");
    }
    if (feature.frame == FrameKind::ReferenceGeometry &&
        feature.kind != FeatureKind::Pressure &&
        feature.kind != FeatureKind::BoundaryTraction) {
        return unsupported(
            "This feature has no reference-geometry frame adapter.");
    }

    switch (feature.kind) {
        case FeatureKind::Pressure:
            return unsupported(
                "Pressure can be audited, but no pressure solver adapter exists yet.");
        case FeatureKind::BoundaryTraction:
            return unsupported(
                "Editable boundary traction needs a persistent surface adapter.");
        case FeatureKind::BearingLoad:
            return unsupported(
                "Bearing distribution and cylindrical geometry are not assembled.");
        case FeatureKind::RemoteResultant:
            return unsupported(
                "Remote coupling equations are not assembled.");
        case FeatureKind::BodyAcceleration:
            return unsupported(
                "Body-force and mass integration are not assembled.");
        case FeatureKind::PrescribedDisplacement:
            return unsupported(
                "The current constraint path supports zero displacement only.");
        case FeatureKind::NormalSupport:
            return unsupported(
                "Normal-only constraint equations are not assembled.");
        case FeatureKind::CylindricalSupport:
            return unsupported(
                "Cylindrical constraint equations are not assembled.");
        case FeatureKind::ElasticSupport:
            return unsupported(
                "Elastic support stiffness is not assembled.");
        case FeatureKind::Contact:
            return unsupported(
                "Contact state, residual, and tangent are not implemented.");
        case FeatureKind::PointForce:
        case FeatureKind::PatchResultant:
        case FeatureKind::FixedConstraint:
            break;
    }

    if (feature.kind == FeatureKind::PatchResultant) {
        if (feature.distribution == DistributionKind::Unspecified) {
            return unsupported(
                "A patch resultant must declare how it is distributed.");
        }
        if (feature.distribution == DistributionKind::EqualNode) {
            return approximate(
                "Equal force per node changes its spatial distribution with the mesh.");
        }
        if (feature.distribution == DistributionKind::LinearFacetTributary) {
            return approximate(
                "Linear-facet tributary loading omits quadratic midside nodes.");
        }
        if (feature.distribution == DistributionKind::ConcentratedNode) {
            return approximate(
                "A patch resultant concentrated at nodes has local singular behavior.");
        }
    }

    if (feature.kind == FeatureKind::PointForce) {
        if (feature.distribution != DistributionKind::ConcentratedNode &&
            feature.distribution != DistributionKind::Unspecified) {
            return unsupported(
                "Point force is incompatible with the declared distribution.");
        }
        return approximate(
            "A nodal point force has singular, mesh-sensitive peak stress.");
    }

    if (mode == AnalysisMode::BrittleFracture) {
        return approximate(
            "Element-deletion fracture is mesh-sensitive and unregularized.");
    }

    return exact(
        "The declared load/support semantics are represented by this solver path.");
}

PresetDescription describePreset(PresetKind preset) noexcept {
    const FeatureDefinition pointForce = {
        FeatureKind::PointForce,
        MagnitudeScope::TotalAcrossSelection,
        DistributionKind::ConcentratedNode,
        FrameKind::Global,
        EvolutionKind::Dead,
    };
    const FeatureDefinition linearFacetPatch = {
        FeatureKind::PatchResultant,
        MagnitudeScope::TotalAcrossSelection,
        DistributionKind::LinearFacetTributary,
        FrameKind::Global,
        EvolutionKind::Dead,
    };
    const FeatureDefinition equalNodePair = {
        FeatureKind::PatchResultant,
        MagnitudeScope::PerRegion,
        DistributionKind::EqualNode,
        FrameKind::Global,
        EvolutionKind::Dead,
    };

    switch (preset) {
        case PresetKind::CantileverBendingZ:
            return {
                pointForce,
                PresetSupportKind::FullCartesianClamp,
                "TIP F (MN)",
                "TOTAL F / DEAD / GLOBAL",
                "POINT NODE; STRESS SINGULAR",
                "X-MIN CARTESIAN CLAMP",
            };
        case PresetKind::PointForceZ:
            return {
                pointForce,
                PresetSupportKind::FullCartesianClamp,
                "POINT F (MN)",
                "TOTAL F / DEAD / GLOBAL",
                "POINT NODE; STRESS SINGULAR",
                "Z-MIN CARTESIAN CLAMP",
            };
        case PresetKind::SurfaceCompressionY:
            return {
                linearFacetPatch,
                PresetSupportKind::FullCartesianClamp,
                "FACE F (MN)",
                "TOTAL FACE F / DEAD / GLOBAL",
                "BBOX: CORNER-TRI OR EQUAL-NODE",
                "Y-MIN CARTESIAN CLAMP",
            };
        case PresetKind::TensionX:
        case PresetKind::TensionY:
        case PresetKind::TensionZ:
            return {
                equalNodePair,
                PresetSupportKind::FiveDofGauge,
                "F / FACE (MN)",
                "+/-F/FACE / DEAD / GLOBAL",
                "EQUAL NODE; MESH DEPENDENT",
                "5-DOF; >=1 RIGID MODE FREE",
            };
    }

    return {
        {static_cast<FeatureKind>(-1)},
        PresetSupportKind::Invalid,
        "INVALID",
        "INVALID PRESET",
        "INVALID PRESET",
        "INVALID PRESET",
    };
}

CapabilityResult assessPreset(PresetKind preset, AnalysisMode mode) noexcept {
    if (!isKnown(preset) || !isKnown(mode)) {
        return unsupported(
            "Invalid preset or analysis mode.");
    }

    const PresetDescription description = describePreset(preset);

    if (description.support == PresetSupportKind::FiveDofGauge) {
        return approximate(
            "5-DOF LEAVES >=1 RIGID MODE");
    }

    if (preset == PresetKind::CantileverBendingZ &&
        mode == AnalysisMode::NonlinearStatic) {
        return unsupported(
            "NR -> Y COMPRESSION");
    }

    const CapabilityResult featureResult =
        assess(description.feature, mode);

    return featureResult;
}

Resultant uniformPressureResultant(
    double pressurePa,
    const std::vector<SurfaceFacet>& facets,
    PressureDirection direction,
    Vec3 referencePointM) {
    if (!std::isfinite(pressurePa) || pressurePa < 0.0) {
        throw std::invalid_argument(
            "Pressure magnitude must be finite and non-negative.");
    }
    if (facets.empty()) {
        throw std::invalid_argument(
            "Pressure resultant requires at least one planar surface facet.");
    }
    if (!isFinite(referencePointM)) {
        throw std::invalid_argument(
            "Pressure reference point must be finite.");
    }
    if (direction != PressureDirection::Inward &&
        direction != PressureDirection::Outward) {
        throw std::invalid_argument(
            "Pressure direction is invalid.");
    }

    const double sign = direction == PressureDirection::Inward ? -1.0 : 1.0;
    Resultant result;

    for (const SurfaceFacet& facet : facets) {
        if (!isFinite(facet.outwardAreaVectorM2) ||
            !isFinite(facet.centroidM)) {
            throw std::invalid_argument(
                "Surface facet area vector and centroid must be finite.");
        }

        const double areaM2 = norm(facet.outwardAreaVectorM2);
        if (!std::isfinite(areaM2) || areaM2 <= 0.0) {
            throw std::invalid_argument(
                "Surface facet must have a positive finite area.");
        }

        const double scalarLoadN = pressurePa * areaM2;
        const Vec3 facetForceN = scaled(
            facet.outwardAreaVectorM2,
            sign * pressurePa);
        const Vec3 leverM = subtract(facet.centroidM, referencePointM);
        const Vec3 facetMomentNm = cross(leverM, facetForceN);

        if (!std::isfinite(scalarLoadN) ||
            !isFinite(facetForceN) ||
            !isFinite(facetMomentNm)) {
            throw std::invalid_argument(
                "Pressure resultant overflowed finite SI values.");
        }

        result.scalarNormalLoadN += scalarLoadN;
        add(result.forceN, facetForceN);
        add(result.momentNm, facetMomentNm);

        if (!std::isfinite(result.scalarNormalLoadN) ||
            !isFinite(result.forceN) ||
            !isFinite(result.momentNm)) {
            throw std::invalid_argument(
                "Accumulated pressure resultant overflowed finite SI values.");
        }
    }

    return result;
}

}  // namespace load_physics
