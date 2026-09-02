#include "LoadPhysics.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

void expectNear(double actual, double expected, double tolerance = 1.0e-12) {
    const double scale = std::max(1.0, std::abs(expected));
    if (!std::isfinite(actual) || !std::isfinite(expected) ||
        std::abs(actual - expected) > tolerance * scale) {
        throw std::runtime_error(
            "expected " + std::to_string(expected) +
            ", got " + std::to_string(actual));
    }
}

void expectVecNear(
    const load_physics::Vec3& actual,
    const load_physics::Vec3& expected,
    double tolerance = 1.0e-12) {
    expectNear(actual.x, expected.x, tolerance);
    expectNear(actual.y, expected.y, tolerance);
    expectNear(actual.z, expected.z, tolerance);
}

template <typename Fn>
void expectInvalidArgument(Fn&& fn) {
    try {
        fn();
    } catch (const std::invalid_argument&) {
        return;
    }
    throw std::runtime_error("expected std::invalid_argument");
}

void expectCapability(
    const load_physics::FeatureDefinition& feature,
    load_physics::AnalysisMode mode,
    load_physics::Capability expected) {
    const auto result = load_physics::assess(feature, mode);
    if (result.status != expected) {
        throw std::runtime_error(
            std::string("expected capability ") +
            load_physics::capabilityName(expected) + ", got " +
            load_physics::capabilityName(result.status));
    }
}

void expectTrue(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testPlanarPressureResultant() {
    const std::vector<load_physics::SurfaceFacet> patches = {
        {{1.0, 0.0, 0.0}, {0.0, -0.5, 0.0}},
        {{1.0, 0.0, 0.0}, {0.0,  0.5, 0.0}},
    };

    const auto result = load_physics::uniformPressureResultant(
        100.0,
        patches,
        load_physics::PressureDirection::Inward,
        {0.0, 0.0, 0.0});

    expectNear(result.scalarNormalLoadN, 200.0);
    expectVecNear(result.forceN, {-200.0, 0.0, 0.0});
    expectVecNear(result.momentNm, {0.0, 0.0, 0.0});
}

void testClosedSurfaceSeparatesScalarLoadFromNetForce() {
    const std::vector<load_physics::SurfaceFacet> patches = {
        {{ 1.0,  0.0,  0.0}, { 0.5,  0.0,  0.0}},
        {{-1.0,  0.0,  0.0}, {-0.5,  0.0,  0.0}},
        {{ 0.0,  1.0,  0.0}, { 0.0,  0.5,  0.0}},
        {{ 0.0, -1.0,  0.0}, { 0.0, -0.5,  0.0}},
        {{ 0.0,  0.0,  1.0}, { 0.0,  0.0,  0.5}},
        {{ 0.0,  0.0, -1.0}, { 0.0,  0.0, -0.5}},
    };

    const auto result = load_physics::uniformPressureResultant(
        100.0,
        patches,
        load_physics::PressureDirection::Inward,
        {0.0, 0.0, 0.0});

    expectNear(result.scalarNormalLoadN, 600.0);
    expectVecNear(result.forceN, {0.0, 0.0, 0.0});
    expectVecNear(result.momentNm, {0.0, 0.0, 0.0});
}

void testTriangulatedCurvedSurfaceResultant() {
    const double a = std::sqrt(0.5);
    const std::vector<load_physics::SurfaceFacet> facets = {
        {{ a, a, 0.0}, { a, a, 0.0}},
        {{-a, a, 0.0}, {-a, a, 0.0}},
    };

    const auto result = load_physics::uniformPressureResultant(
        10.0,
        facets,
        load_physics::PressureDirection::Inward,
        {1.0, 0.0, 0.0});

    expectNear(result.scalarNormalLoadN, 20.0);
    expectVecNear(result.forceN, {0.0, -10.0 * std::sqrt(2.0), 0.0});
    expectVecNear(result.momentNm, {0.0, 0.0, 10.0 * std::sqrt(2.0)});
}

void testPressureMomentUsesDeclaredReferencePoint() {
    const std::vector<load_physics::SurfaceFacet> patches = {
        {{0.0, 0.0, 2.0}, {2.0, 0.0, 0.0}},
    };

    const auto result = load_physics::uniformPressureResultant(
        3.0,
        patches,
        load_physics::PressureDirection::Inward,
        {1.0, 0.0, 0.0});

    expectNear(result.scalarNormalLoadN, 6.0);
    expectVecNear(result.forceN, {0.0, 0.0, -6.0});
    expectVecNear(result.momentNm, {0.0, 6.0, 0.0});

    const auto outward = load_physics::uniformPressureResultant(
        3.0,
        patches,
        load_physics::PressureDirection::Outward,
        {1.0, 0.0, 0.0});
    expectVecNear(outward.forceN, {0.0, 0.0, 6.0});
    expectVecNear(outward.momentNm, {0.0, -6.0, 0.0});
}

void testPressureInputValidation() {
    const std::vector<load_physics::SurfaceFacet> valid = {
        {{0.0, 0.0, 2.0}, {1.0, 2.0, 3.0}},
    };
    const std::vector<load_physics::SurfaceFacet> degenerate = {
        {{0.0, 0.0, 0.0}, {1.0, 2.0, 3.0}},
    };
    const std::vector<load_physics::SurfaceFacet> nonFinite = {
        {{0.0, 0.0, 2.0},
         {std::numeric_limits<double>::quiet_NaN(), 2.0, 3.0}},
    };

    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            -1.0, valid, load_physics::PressureDirection::Inward);
    });
    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            std::numeric_limits<double>::infinity(),
            valid,
            load_physics::PressureDirection::Inward);
    });
    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            1.0, {}, load_physics::PressureDirection::Inward);
    });
    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            1.0, degenerate, load_physics::PressureDirection::Inward);
    });
    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            1.0, nonFinite, load_physics::PressureDirection::Inward);
    });
    expectInvalidArgument([&] {
        load_physics::uniformPressureResultant(
            1.0,
            valid,
            static_cast<load_physics::PressureDirection>(999));
    });
}

void testCapabilityContract() {
    using namespace load_physics;

    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea},
        AnalysisMode::LinearStatic,
        Capability::Exact);
    expectCapability(
        {FeatureKind::PatchResultant},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::PerRegion,
         DistributionKind::EqualNode},
        AnalysisMode::LinearStatic,
        Capability::Approximate);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::LinearFacetTributary},
        AnalysisMode::LinearStatic,
        Capability::Approximate);
    expectCapability(
        {FeatureKind::PointForce,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConcentratedNode},
        AnalysisMode::LinearStatic,
        Capability::Approximate);
    expectCapability(
        {FeatureKind::Pressure,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea,
         FrameKind::ReferenceGeometry,
         EvolutionKind::Dead},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::Pressure,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea,
         FrameKind::CurrentBoundary,
         EvolutionKind::Follower},
        AnalysisMode::NonlinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PrescribedDisplacement},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea},
        AnalysisMode::BrittleFracture,
        Capability::Approximate);
    expectCapability(
        {static_cast<FeatureKind>(999)},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         static_cast<DistributionKind>(999)},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         static_cast<MagnitudeScope>(999),
         DistributionKind::ConsistentArea},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea,
         static_cast<FrameKind>(999),
         EvolutionKind::Dead},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectCapability(
        {FeatureKind::PatchResultant,
         MagnitudeScope::TotalAcrossSelection,
         DistributionKind::ConsistentArea,
         FrameKind::Global,
         static_cast<EvolutionKind>(999)},
        AnalysisMode::LinearStatic,
        Capability::Unsupported);
    expectTrue(
        assessPreset(
            PresetKind::TensionX,
            static_cast<AnalysisMode>(999)).status == Capability::Unsupported,
        "unknown analysis mode must fail closed before support approximation");
}

void testExistingPresetAdapterContract() {
    using namespace load_physics;

    expectCapability(
        describePreset(PresetKind::CantileverBendingZ).feature,
        AnalysisMode::LinearStatic,
        Capability::Approximate);

    const auto cantileverNonlinear = assessPreset(
        PresetKind::CantileverBendingZ,
        AnalysisMode::NonlinearStatic);
    expectTrue(
        cantileverNonlinear.status == Capability::Unsupported,
        "cantilever nonlinear preset must be blocked");
    expectTrue(
        !cantileverNonlinear.canRun(),
        "blocked cantilever nonlinear preset must not run");

    expectTrue(
        assessPreset(PresetKind::PointForceZ, AnalysisMode::NonlinearStatic).status ==
            Capability::Approximate,
        "nonlinear point-force preset should remain available as idealized");
    expectTrue(
        assessPreset(PresetKind::SurfaceCompressionY, AnalysisMode::NonlinearStatic).status ==
            Capability::Approximate,
        "bbox surface preset should disclose its equal-node fallback");
    for (const auto tensionPreset : {
             PresetKind::TensionX,
             PresetKind::TensionY,
             PresetKind::TensionZ}) {
        for (const auto mode : {
                 AnalysisMode::LinearStatic,
                 AnalysisMode::NonlinearStatic,
                 AnalysisMode::BrittleFracture}) {
            expectTrue(
                assessPreset(tensionPreset, mode).status == Capability::Approximate,
                "underconstrained five-DOF tension gauge must warn without changing the legacy workflow");
        }
    }
    expectTrue(
        assessPreset(PresetKind::SurfaceCompressionY, AnalysisMode::BrittleFracture).status ==
            Capability::Approximate,
        "element-deletion fracture should be marked approximate");

    const auto tension = describePreset(PresetKind::TensionX);
    expectTrue(
        tension.feature.scope == MagnitudeScope::PerRegion,
        "tension magnitude must be declared per face");
    expectTrue(
        tension.feature.distribution == DistributionKind::EqualNode,
        "tension distribution must disclose its equal-node approximation");
    expectTrue(
        tension.support == PresetSupportKind::FiveDofGauge,
        "tension preset must disclose its five-DOF numerical gauge");
    expectTrue(
        std::string(tension.supportSummary).find(">=1 RIGID MODE") != std::string::npos,
        "tension receipt must expose at least one remaining rigid mode");

    const auto surface = describePreset(PresetKind::SurfaceCompressionY);
    expectTrue(
        std::string(surface.distributionSummary).find("BBOX") != std::string::npos &&
            std::string(surface.distributionSummary).find("CORNER-TRI") != std::string::npos &&
            std::string(surface.distributionSummary).find("EQUAL-NODE") != std::string::npos,
        "surface receipt must expose bbox scoping, corner-triangle loading, and fallback");
    expectTrue(
        surface.feature.distribution == DistributionKind::LinearFacetTributary,
        "Tet10 surface preset must not claim quadratic consistent-area loading");

    const auto invalidPreset = static_cast<PresetKind>(999);
    expectTrue(
        assessPreset(invalidPreset, AnalysisMode::LinearStatic).status ==
            Capability::Unsupported,
        "unknown preset must fail closed");
    expectTrue(
        describePreset(invalidPreset).support == PresetSupportKind::Invalid,
        "unknown preset description must be visibly invalid");

    const auto cantilever = describePreset(PresetKind::CantileverBendingZ);
    expectTrue(
        cantilever.support == PresetSupportKind::FullCartesianClamp,
        "cantilever preset must disclose its full-face clamp");

    for (const auto preset : {
             PresetKind::CantileverBendingZ,
             PresetKind::PointForceZ,
             PresetKind::SurfaceCompressionY,
             PresetKind::TensionX,
             PresetKind::TensionY,
             PresetKind::TensionZ}) {
        const auto description = describePreset(preset);
        // SimpleUI advances 8.5 px text by 10.2 px/character inside 267 px,
        // and 5.9 px receipt text by 7.08 px/character inside 275 px.
        expectTrue(
            std::string(description.magnitudeLabel).size() + 10 <= 26,
            "slider label plus 1000.000 must fit the existing panel");
        expectTrue(
            std::string(description.scopeSummary).size() + 6 <= 38,
            "load receipt must fit the existing panel");
        expectTrue(
            std::string(description.distributionSummary).size() + 6 <= 38,
            "distribution receipt must fit the existing panel");
        expectTrue(
            std::string(description.supportSummary).size() + 9 <= 38,
            "support receipt must fit the existing panel");

        const auto linear = assessPreset(preset, AnalysisMode::LinearStatic);
        const auto nonlinear = assessPreset(preset, AnalysisMode::NonlinearStatic);
        const auto fracture = assessPreset(preset, AnalysisMode::BrittleFracture);
        const char* fractureReceipt =
            fracture.status == Capability::Unsupported ? "BLOCKED" : "MESH-DEP";
        const std::string capabilityReceipt =
            std::string("L ") + capabilityName(linear.status) +
            " | NL " + capabilityName(nonlinear.status) +
            " | FR " + fractureReceipt;
        expectTrue(
            capabilityReceipt.size() <= 38,
            "capability receipt must fit the existing panel");

        for (const auto result : {linear, nonlinear, fracture}) {
            if (!result.canRun()) {
                expectTrue(
                    std::string("BLOCK: ").size() +
                            std::string(result.reason).size() <= 38,
                    "blocked-mode reason must fit the existing panel");
            }
        }
    }
}

}  // namespace

int main() {
    const std::vector<std::pair<const char*, std::function<void()>>> tests = {
        {"planar pressure resultant", testPlanarPressureResultant},
        {"closed-surface pressure audit", testClosedSurfaceSeparatesScalarLoadFromNetForce},
        {"triangulated curved-surface audit", testTriangulatedCurvedSurfaceResultant},
        {"pressure moment and direction", testPressureMomentUsesDeclaredReferencePoint},
        {"pressure validation", testPressureInputValidation},
        {"capability contract", testCapabilityContract},
        {"existing preset adapter", testExistingPresetAdapterContract},
    };

    int failures = 0;
    for (const auto& test : tests) {
        try {
            test.second();
            std::cout << "[PASS] " << test.first << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.first << ": " << error.what() << '\n';
        }
    }

    if (failures != 0) {
        std::cerr << failures << " focused test(s) failed\n";
        return 1;
    }

    std::cout << "All load-physics tests passed\n";
    return 0;
}
