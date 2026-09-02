#pragma once

#include <vector>

namespace load_physics {

enum class AnalysisMode {
    LinearStatic,
    NonlinearStatic,
    BrittleFracture,
};

enum class FeatureKind {
    PointForce,
    PatchResultant,
    BoundaryTraction,
    Pressure,
    BearingLoad,
    RemoteResultant,
    BodyAcceleration,
    FixedConstraint,
    PrescribedDisplacement,
    NormalSupport,
    CylindricalSupport,
    ElasticSupport,
    Contact,
};

enum class MagnitudeScope {
    TotalAcrossSelection,
    PerRegion,
};

enum class DistributionKind {
    Unspecified,
    ConcentratedNode,
    ConsistentArea,
    LinearFacetTributary,
    EqualNode,
};

enum class FrameKind {
    Global,
    BuildMaterial,
    ReferenceGeometry,
    CurrentBoundary,
};

enum class EvolutionKind {
    Dead,
    Follower,
};

enum class Capability {
    Exact,
    Approximate,
    Unsupported,
};

// Adapter identifiers for the six presets exposed by the current generic UI.
// They are intentionally separate from FeatureKind: two presets can represent
// the same physical load but have different solver-path coverage.
enum class PresetKind {
    CantileverBendingZ,
    PointForceZ,
    SurfaceCompressionY,
    TensionX,
    TensionY,
    TensionZ,
};

struct FeatureDefinition {
    FeatureKind kind;
    MagnitudeScope scope = MagnitudeScope::TotalAcrossSelection;
    DistributionKind distribution = DistributionKind::Unspecified;
    FrameKind frame = FrameKind::Global;
    EvolutionKind evolution = EvolutionKind::Dead;
};

struct CapabilityResult {
    Capability status;
    const char* reason;

    bool canRun() const noexcept {
        return status != Capability::Unsupported;
    }
};

enum class PresetSupportKind {
    FullCartesianClamp,
    FiveDofGauge,
    Invalid,
};

struct PresetDescription {
    FeatureDefinition feature;
    PresetSupportKind support;
    const char* magnitudeLabel;
    const char* scopeSummary;
    const char* distributionSummary;
    const char* supportSummary;
};

const char* capabilityName(Capability capability) noexcept;
const char* featureName(FeatureKind feature) noexcept;
CapabilityResult assess(
    const FeatureDefinition& feature,
    AnalysisMode mode) noexcept;
PresetDescription describePreset(PresetKind preset) noexcept;
CapabilityResult assessPreset(PresetKind preset, AnalysisMode mode) noexcept;

struct Vec3 {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

struct SurfaceFacet {
    // One planar facet with constant outward normal. Curved surfaces must be
    // supplied as their constituent planar facets, not as one aggregate patch.
    // Oriented outward area vector in square metres.
    Vec3 outwardAreaVectorM2;
    // Area centroid in metres, in the same frame as referencePointM.
    Vec3 centroidM;
};

struct Resultant {
    // Integral of |pressure| dA. This is not generally the norm of forceN.
    double scalarNormalLoadN = 0.0;
    Vec3 forceN;
    Vec3 momentNm;
};

enum class PressureDirection {
    Inward,
    Outward,
};

Resultant uniformPressureResultant(
    double pressurePa,
    const std::vector<SurfaceFacet>& facets,
    PressureDirection direction,
    Vec3 referencePointM = {});

}  // namespace load_physics
