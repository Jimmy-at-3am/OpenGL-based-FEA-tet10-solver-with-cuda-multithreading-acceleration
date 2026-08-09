// =============================================================================
// gen_step_fixtures.cpp  --  helper tool
//
// Generates two STEP test fixtures:
//   assets/test_fixtures/unit_cube.step   -- 1 mm × 1 mm × 1 mm box
//   assets/test_fixtures/sphere_r1.step   -- sphere, radius 1 mm
//
// Build target: gen_step_fixtures (CMakeLists.txt links OCCT modules).
// Usage (from build dir):  tools/gen_step_fixtures.exe [output_dir]
//   output_dir defaults to "assets/test_fixtures" relative to CWD.
//
// Note: dimensions are in mm (OCC cascade unit default).  processRawGeometry
//       will normalise to 3-unit diagonal on load, so the actual unit is
//       irrelevant for the renderer — but the Hausdorff regression is in the
//       same unit as the STEP file.
// =============================================================================

#ifdef HAS_OCCT

#include <BRepPrimAPI_MakeBox.hxx>
#include <BRepPrimAPI_MakeSphere.hxx>
#include <STEPControl_Writer.hxx>
#include <IFSelect_ReturnStatus.hxx>
#include <Interface_Static.hxx>

#include <iostream>
#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static bool writeSTEP(const TopoDS_Shape& shape, const std::string& path) {
    STEPControl_Writer writer;
    IFSelect_ReturnStatus st = writer.Transfer(shape, STEPControl_AsIs);
    if (st != IFSelect_RetDone) {
        std::cerr << "gen_step_fixtures: Transfer failed for " << path << "\n";
        return false;
    }
    st = writer.Write(path.c_str());
    if (st != IFSelect_RetDone) {
        std::cerr << "gen_step_fixtures: Write failed for " << path << "\n";
        return false;
    }
    std::cout << "  Written: " << path << "\n";
    return true;
}

int main(int argc, char* argv[]) {
    std::string outDir = "assets/test_fixtures";
    if (argc > 1) outDir = argv[1];

    // Ensure output directory exists.
    fs::create_directories(outDir);

    std::cout << "gen_step_fixtures: output dir = " << outDir << "\n";

    // OCC default cascade unit is MM — all dimensions in millimetres.
    Interface_Static::SetCVal("write.step.unit", "MM");

    // -- Unit cube: 1 mm × 1 mm × 1 mm, corner at origin --
    {
        BRepPrimAPI_MakeBox boxMaker(1.0, 1.0, 1.0);
        TopoDS_Shape box = boxMaker.Shape();
        const std::string path = outDir + "/unit_cube.step";
        if (!writeSTEP(box, path)) return 1;
        std::cout << "  unit_cube.step: 1×1×1 mm box, 6 faces, 12 edges, 8 vertices\n";
    }

    // -- Sphere: radius 1 mm, centred at origin --
    {
        BRepPrimAPI_MakeSphere sphereMaker(1.0);
        TopoDS_Shape sphere = sphereMaker.Shape();
        const std::string path = outDir + "/sphere_r1.step";
        if (!writeSTEP(sphere, path)) return 1;
        std::cout << "  sphere_r1.step: r=1 mm sphere, OCC spherical B-rep\n";
    }

    std::cout << "gen_step_fixtures: done.\n";
    return 0;
}

#else  // !HAS_OCCT

#include <iostream>
int main() {
    std::cerr << "gen_step_fixtures: built without OCCT (USE_OCCT=OFF). "
                 "Cannot generate STEP fixtures.\n";
    return 1;
}

#endif // HAS_OCCT
