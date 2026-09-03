cmake_minimum_required(VERSION 3.20)

option(USE_CUDA "Enable CUDA solver" ON)

# ---------------------------------------------------------------------------
# OpenCASCADE (OCCT) — STEP loader + analytic B-rep retention.
#
# Usage:
#   cmake -S . -B build -DOCCT_ROOT="C:/OpenCASCADE-7.8.1-vc14-64" ...
#
# The OCCT official installer places OpenCASCADEConfig.cmake in
#   <OCCT_ROOT>/cmake/
# vcpkg installs it under
#   <vcpkg_root>/installed/x64-windows/share/opencascade/
#
# With USE_OCCT=OFF (or if find_package fails), STEP loading is disabled
# gracefully — all other features continue to work.
# ---------------------------------------------------------------------------
option(USE_OCCT "Enable STEP loader via OpenCASCADE" ON)
set(OCCT_ROOT "" CACHE PATH
    "OpenCASCADE install root (cmake/ sub-dir must contain OpenCASCADEConfig.cmake)")

# NOTE: find_package(OpenCASCADE) is deferred to AFTER project() so that the
# platform (shared-library support, compiler info) is fully initialised before
# OCCT's imported targets are created.  See the block after project() below.

# CMAKE_CUDA_COMPILER and CUDA arch must be set before project() / enable_language(CUDA).
if(USE_CUDA)
    set(CMAKE_CUDA_COMPILER "D:/program_files_delta/cuda/bin/nvcc.exe")
    # Pin to a real SM rather than 'native'; -arch=native adds compiler passes
    # that misparse Windows paths containing spaces and a dash.
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

project(FEAPreProcessor LANGUAGES C CXX)

# --- OpenCASCADE detection (must be after project() so platform is initialised) ---
if(USE_OCCT)
    # Ensure the vcpkg install prefix is on CMAKE_PREFIX_PATH so that
    # OpenCASCADEConfig.cmake's find_dependency(Freetype MODULE) resolves correctly.
    set(_VCPKG_INSTALLED "D:/program_files_delta/vcpkg/installed/x64-windows")
    if(EXISTS "${_VCPKG_INSTALLED}")
        list(PREPEND CMAKE_PREFIX_PATH "${_VCPKG_INSTALLED}")
    endif()

    # Set OpenCASCADE_DIR directly — bypasses find_package's path-search heuristics.
    if(NOT OpenCASCADE_DIR)
        foreach(_occt_hint
            "${OCCT_ROOT}/cmake"
            "${OCCT_ROOT}/lib/cmake/opencascade"
            "${OCCT_ROOT}"
            "D:/program_files_delta/vcpkg/installed/x64-windows/share/opencascade"
            "C:/OpenCASCADE-7.8.1-vc14-64/cmake"
            "C:/OpenCASCADE-7.7.0-vc14-64/cmake"
            "D:/OpenCASCADE-7.8.1-vc14-64/cmake"
            "D:/OpenCASCADE-7.7.0-vc14-64/cmake"
        )
            if(EXISTS "${_occt_hint}/OpenCASCADEConfig.cmake")
                set(OpenCASCADE_DIR "${_occt_hint}" CACHE PATH
                    "Directory containing OpenCASCADEConfig.cmake" FORCE)
                message(STATUS "OpenCASCADE_DIR auto-detected: ${OpenCASCADE_DIR}")
                break()
            endif()
        endforeach()
    endif()

    find_package(OpenCASCADE CONFIG QUIET)
    if(OpenCASCADE_FOUND)
        message(STATUS "OpenCASCADE found: ${OpenCASCADE_VERSION} at ${OpenCASCADE_INSTALL_PREFIX}")
        set(HAS_OCCT TRUE)
    else()
        message(WARNING
            "OpenCASCADE not found. STEP loader disabled.\n"
            "Set -DOCCT_ROOT=<path> to the OCCT install directory.")
        set(HAS_OCCT FALSE)
    endif()
else()
    set(HAS_OCCT FALSE)
    message(STATUS "OpenCASCADE disabled (USE_OCCT=OFF). STEP loader not built.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(MSVC)
    add_compile_options("$<$<COMPILE_LANGUAGE:CXX>:/utf-8>")
    add_compile_options("$<$<COMPILE_LANGUAGE:C>:/utf-8>")
endif()

# --- CUDA (optional) ---
if(USE_CUDA)
    set(CUDAToolkit_ROOT "D:/program_files_delta/cuda")
    enable_language(CUDA)
    find_package(CUDAToolkit REQUIRED)
    set(CMAKE_CUDA_STANDARD 17)
    set(CMAKE_CUDA_STANDARD_REQUIRED ON)
    # Disable CMake's auto -MD -MT -MF depfile generation for CUDA — this is the
    # flag combination that misparses on Windows paths with spaces and a dash,
    # causing nvcc to treat /utf-8 as a second input file.
    set(CMAKE_CUDA_DEPFILE_FORMAT "")
    set(CMAKE_DEPFILE_FLAGS_CUDA  "")
    set(HAS_CUDA TRUE)
else()
    set(HAS_CUDA FALSE)
endif()

include(FetchContent)
FetchContent_Declare(
  eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        3.4.0
)
FetchContent_MakeAvailable(eigen)

# --- GLM: header-only math library ---
# Fetched as a raw source tree (not via add_subdirectory) so only the headers
# are used — no GLM install/test targets pollute the build.
FetchContent_Declare(
  glm
  GIT_REPOSITORY https://github.com/g-truc/glm.git
  GIT_TAG        0.9.8.5
)
FetchContent_GetProperties(glm)
if(NOT glm_POPULATED)
  FetchContent_Populate(glm)
endif()
# glm_SOURCE_DIR is now set; see include_directories below.

# --- miniz: single-file ZIP decompressor for .3mf loading ---
# We use FetchContent to download the sources but deliberately do NOT call
# add_subdirectory on miniz (its CMakeLists uses cmake_minimum_required 2.8
# which is rejected by CMake 3.20+).  Instead we compile miniz.c directly as
# part of the main executable sources.
FetchContent_Declare(
  miniz
  GIT_REPOSITORY https://github.com/richgel999/miniz.git
  GIT_TAG        2.2.0
)
set(FETCHCONTENT_UPDATES_DISCONNECTED ON)
FetchContent_GetProperties(miniz)
if(NOT miniz_POPULATED)
  FetchContent_Populate(miniz)
  # Do NOT call add_subdirectory — we only need the raw .c/.h files.
endif()
# miniz_SOURCE_DIR is now set; add include path and .c to SOURCES below.

# --- Clipper2 (optional): robust 2-D polygon boolean + cleanup for slicing ---
# LayerSlicer uses Clipper2 Union(EvenOdd)+SimplifyPaths as a
# robustness backstop when stitching slice-plane contours from dirty STL. It is
# OPTIONAL — LayerSlicer.cpp has a raw-loop fallback guarded by HAS_CLIPPER2, so
# a failed/disabled fetch degrades gracefully (clean meshes still slice).
# Populated like miniz (raw sources compiled into the exe; no add_subdirectory).
option(USE_CLIPPER2 "Enable Clipper2 polygon cleanup for slicing" ON)
set(HAS_CLIPPER2 FALSE)
if(USE_CLIPPER2)
  FetchContent_Declare(
    clipper2
    GIT_REPOSITORY https://github.com/AngusJohnson/Clipper2.git
    GIT_TAG        Clipper2_1.4.0
  )
  FetchContent_GetProperties(clipper2)
  if(NOT clipper2_POPULATED)
    FetchContent_Populate(clipper2)
  endif()
  if(EXISTS "${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/include/clipper2/clipper.h")
    set(HAS_CLIPPER2 TRUE)
    set(CLIPPER2_INCLUDE_DIR "${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/include")
    set(CLIPPER2_SOURCES
      "${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.engine.cpp"
      "${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.offset.cpp"
      "${clipper2_SOURCE_DIR}/CPP/Clipper2Lib/src/clipper.rectclip.cpp")
    message(STATUS "Clipper2 enabled: ${clipper2_SOURCE_DIR}")
  else()
    message(WARNING "Clipper2 sources not found after fetch; slicing uses raw-loop fallback.")
  endif()
endif()

# --- CDT (optional): artem-ogre/CDT constrained Delaunay triangulation for
# the toolpath cookie sections (polygons with many holes — the hand-rolled
# hole-bridging failed on Bambu's 46-hole diagonal-grid infill). Header-only;
# populated like Clipper2. ---
option(USE_CDT "Enable CDT triangulation for toolpath sections" ON)
set(HAS_CDT FALSE)
if(USE_CDT)
  FetchContent_Declare(
    cdt
    GIT_REPOSITORY https://github.com/artem-ogre/CDT.git
    GIT_TAG        1.4.1
  )
  FetchContent_GetProperties(cdt)
  if(NOT cdt_POPULATED)
    FetchContent_Populate(cdt)
  endif()
  if(EXISTS "${cdt_SOURCE_DIR}/CDT/include/CDT.h")
    set(HAS_CDT TRUE)
    set(CDT_INCLUDE_DIR "${cdt_SOURCE_DIR}/CDT/include")
    message(STATUS "CDT enabled: ${cdt_SOURCE_DIR}")
  else()
    message(WARNING "CDT sources not found after fetch; toolpath meshing will error at runtime.")
  endif()
endif()

# --- Include directories ---
include_directories(
    ${CMAKE_SOURCE_DIR}/include        # glad, GLFW, KHR, stb_image headers
    ${CMAKE_SOURCE_DIR}/src            # project headers: camera.h, tetgen.h, mesh.h, model.h, etc.
    ${eigen_SOURCE_DIR}                # Eigen library headers
    ${glm_SOURCE_DIR}                  # GLM math library (header-only, fetched)
    ${CMAKE_SOURCE_DIR}/ext/meshoptimizer/src
    ${miniz_SOURCE_DIR}                # miniz ZIP library
)
if(HAS_CUDA)
    include_directories(${CUDAToolkit_INCLUDE_DIRS})   # cusolverSp.h, cusparse.h
endif()

# --- Library directories ---
link_directories(${CMAKE_SOURCE_DIR}/lib)

# --- Source files ---
set(SOURCES
    src/main.cpp
    src/BuiltInShader.cpp
    src/SimpleUI.cpp
    src/UIFontRenderer.cpp
    src/UIDesign.cpp
    src/GeometryUtils.cpp
    src/LoadPhysics.cpp
    src/FEAModel.cpp
    src/FEASolver.cpp
    src/MeshQuality.cpp
    src/Tet10Element.cpp
    # live solve-stage telemetry driving the viewport status overlay
    src/SolverStatus.cpp
    # layered-FDM slice data model + contour extraction
    src/LayerSlicer.cpp
    src/StepSlicer.cpp        # B-rep section seam
    # per-slab 2D triangulation + prism extrusion -> Tet4 slabs
    src/SlabMesher.cpp
    # Bambu .gcode.3mf toolpath ingest (authoritative FDM source)
    src/GcodeToolpathLoader.cpp
    # bead rectangles -> per-layer cookie sections (Clipper2)
    src/ToolpathSections.cpp
    # headless self-test / regression harness
    src/ScenarioRunner.cpp
    src/glad.c
    src/tetgen.cxx
    src/predicates.cxx
    # --- Geometry loader pipeline ---
    src/STLLoader.cpp
    src/ThreeMFLoader.cpp
    # STEP loader + analytic B-rep (OCC-guarded at compile time)
    src/StepLoader.cpp
    src/BRepHandle.cpp
    # miniz 2.2.0 split sources (ZIP decompressor for .3mf)
    ${miniz_SOURCE_DIR}/miniz.c
    ${miniz_SOURCE_DIR}/miniz_tdef.c
    ${miniz_SOURCE_DIR}/miniz_tinfl.c
    ${miniz_SOURCE_DIR}/miniz_zip.c
    # Clipper2 raw sources (empty when USE_CLIPPER2=OFF or fetch failed)
    ${CLIPPER2_SOURCES}
)

add_subdirectory(ext/meshoptimizer)

# --- CUDA kernels as an isolated static library ---
# Keeping CudaSolver.cu in its own target means nvcc only sees a minimal flag
# set, free of the main executable's per-target /utf-8 and /MDd flags that
# trip up nvcc on Windows paths containing spaces.
if(HAS_CUDA)
    add_library(FEACudaKernels STATIC src/CudaSolver.cu)
    set_target_properties(FEACudaKernels PROPERTIES
        CUDA_SEPARABLE_COMPILATION OFF
        POSITION_INDEPENDENT_CODE  ON)
    target_include_directories(FEACudaKernels PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${eigen_SOURCE_DIR}
        ${CUDAToolkit_INCLUDE_DIRS})
    target_compile_definitions(FEACudaKernels PRIVATE HAS_CUDA=1)
    target_link_libraries(FEACudaKernels PUBLIC
        CUDA::cudart CUDA::cublas CUDA::cusparse)
endif()

# --- Create executable (C/C++ only; CUDA linked via FEACudaKernels) ---
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(Freetype QUIET)
if(Freetype_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Freetype::Freetype)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_FREETYPE_UI=1)
else()
    message(WARNING "FreeType not found: UI will use the built-in stroke font")
endif()

add_library(ui_font_fallback_compile OBJECT src/UIFontRenderer.cpp)
target_include_directories(ui_font_fallback_compile PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${glm_SOURCE_DIR})
target_compile_definitions(ui_font_fallback_compile PRIVATE UI_FONT_FORCE_FALLBACK=1)

# --- Clipper2: include path + HAS_CLIPPER2 only when populated ---
if(HAS_CLIPPER2)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CLIPPER2_INCLUDE_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CLIPPER2=1)
endif()
# --- CDT: include path + HAS_CDT only when populated ---
if(HAS_CDT)
    target_include_directories(${PROJECT_NAME} PRIVATE ${CDT_INCLUDE_DIR})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CDT=1)
endif()

# --- Link libraries ---
find_package(OpenMP REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw3dll
    opengl32
    gdi32
    OpenMP::OpenMP_CXX
    meshoptimizer)

if(HAS_CUDA)
    target_link_libraries(${PROJECT_NAME} PRIVATE FEACudaKernels)
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CUDA=1)
endif()

# --- OpenCASCADE ---
if(HAS_OCCT)
    # OCCT modules needed for STEP reading, B-rep, tessellation, healing,
    # and exact nearest-point queries.  CMake resolves transitive deps.
    set(OCCT_LINK_MODULES
        TKernel TKMath
        # OCCT 7.9: STEP I/O is in TKDESTEP + TKDE (replaces TKSTEP/TKSTEPBase/TKSTEPAttr/TKSTEP209)
        TKDESTEP TKDE TKXSBase
        TKBRep TKTopAlgo TKShHealing
        TKG3d TKG2d TKGeomBase TKGeomAlgo
        TKMesh
    )
    target_include_directories(${PROJECT_NAME} PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    target_link_libraries(${PROJECT_NAME} PRIVATE ${OCCT_LINK_MODULES})
    target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_OCCT=1)
    # Resolve the directory that holds the OCCT DLLs.
    # Official OCCT installer sets OpenCASCADE_BINARY_DIR; vcpkg sets only
    # OpenCASCADE_INSTALL_PREFIX and puts DLLs in <prefix>/bin.
    if(OpenCASCADE_BINARY_DIR AND EXISTS "${OpenCASCADE_BINARY_DIR}")
        set(_occt_dll_dir "${OpenCASCADE_BINARY_DIR}")
    else()
        set(_occt_dll_dir "${OpenCASCADE_INSTALL_PREFIX}/bin")
    endif()
    message(STATUS "OCCT DLL source dir: ${_occt_dll_dir}")
    # OCCT requires its DLLs at runtime; copy them next to the executable.
    foreach(_occt_lib ${OCCT_LINK_MODULES})
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${_occt_dll_dir}/${_occt_lib}.dll"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
            COMMENT "Copying ${_occt_lib}.dll"
        )
    endforeach()
endif()

# --- Copy glfw3.dll next to the executable after build ---
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/lib/glfw3.dll"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    COMMENT "Copying glfw3.dll to output directory"
)

# --- Copy shader files and STL assets next to the executable ---
# The app scans "." for STL files and loads shaders from the working dir,
# so we copy everything needed from src/ to the build output.
file(GLOB SHADER_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/shaders/*.vs" "${CMAKE_SOURCE_DIR}/assets/shaders/*.fs")
file(GLOB STL_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/models/*.stl"
    "${CMAKE_SOURCE_DIR}/assets/models/*.STL")

foreach(SHADER_FILE ${SHADER_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SHADER_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

foreach(STL_FILE ${STL_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${STL_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

# Copy all model-format files from assets/models/ — glob runs at build time so
# newly added files are picked up without re-running cmake.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND}
        -DSRC_DIR="${CMAKE_SOURCE_DIR}/assets/models"
        -DDST_DIR="$<TARGET_FILE_DIR:${PROJECT_NAME}>"
        -P "${CMAKE_SOURCE_DIR}/cmake/CopyModelFiles.cmake"
    COMMENT "Copying model files from assets/models/ to build output"
)

# --- Copy test fixture STL files next to the executable ---
file(GLOB FIXTURE_STL_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/test_fixtures/*.stl")
foreach(FIXTURE_STL_FILE ${FIXTURE_STL_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FIXTURE_STL_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

# --- Copy material property files into a materials/ subdirectory ---
file(GLOB MAT_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/materials/*.mat")

add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/materials"
)

foreach(MAT_FILE ${MAT_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${MAT_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>/materials"
    )
endforeach()

# --- Copy scenarios/ and regression/ next to the executable ---
# The headless harness reads scenarios/*.json and regression/*.txt relative to
# the working directory, so mirror both folders into the build output.
# ALL-target (not POST_BUILD): a scenario-only edit produces "ninja: no work to
# do" for the exe, so a POST_BUILD copy never fires and `--regress all` runs
# STALE asserts from the previous staging — a harness that silently checks old
# checks. Caught 2026-07-02 (slab_mesh_box passed standalone, failed in
# regress). copy_directory of a few JSON files is cheap; run it every build.
add_custom_target(stage_harness_data ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenarios"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scenarios"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/regression"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/regression"
    # sliced-gcode fixtures + showcase defaults live in their OWN
    # staged subfolder (the UI file panel reads "." and "gcode_models/" as two
    # sections; scenarios reference "gcode_models/<file>").
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/assets/gcode_models"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/gcode_models"
    COMMENT "Staging scenarios/ + regression/ + gcode_models/ next to the executable"
)
add_dependencies(stage_harness_data ${PROJECT_NAME})

# --- Diagnostic tool ---
# MeshQuality.cpp is linked into mesh_diag as well so both binaries share the
# same quality library.  The diagnostic still has its own local aspect-ratio
# routine; routing mesh_diag fully through MeshQuality is future work.
add_executable(mesh_diag
    src/mesh_diag.cpp
    src/MeshQuality.cpp
    src/tetgen.cxx
    src/predicates.cxx)
find_package(OpenMP QUIET)
if(OpenMP_CXX_FOUND)
    target_link_libraries(mesh_diag PRIVATE OpenMP::OpenMP_CXX)
endif()
target_link_libraries(mesh_diag PRIVATE gdi32)
add_custom_command(TARGET mesh_diag POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/models/torus.stl"
        "$<TARGET_FILE_DIR:mesh_diag>"
)

# --- Unit-cube fixture generator ---
# Standalone one-shot tool that writes assets/test_fixtures/unit_cube.stl by
# replicating FEAModel::generate_face's tessellation at size=1, sub=5.
# Independent of OpenGL/TetGen so it builds and runs in any environment.
add_executable(gen_unit_cube_stl tools/gen_unit_cube_stl.cpp)
set_target_properties(gen_unit_cube_stl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

# --- STEP fixture generator ---
# Generates assets/test_fixtures/unit_cube.step and sphere_r1.step using OCCT
# primitives.  Only built when HAS_OCCT=1.
if(HAS_OCCT)
    add_executable(gen_step_fixtures tools/gen_step_fixtures.cpp)
    set_target_properties(gen_step_fixtures PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
    target_include_directories(gen_step_fixtures PRIVATE ${OpenCASCADE_INCLUDE_DIR})
    set(STEP_FIXTURE_MODULES
        TKernel TKMath TKBRep
        # OCCT 7.9: STEP I/O renamed
        TKDESTEP TKDE TKXSBase
        TKG3d TKG2d TKGeomBase TKPrim
    )
    target_link_libraries(gen_step_fixtures PRIVATE ${STEP_FIXTURE_MODULES})
    target_compile_definitions(gen_step_fixtures PRIVATE HAS_OCCT=1)
endif()

# --- Copy STEP test fixture files next to the executable ---
file(GLOB FIXTURE_STEP_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/test_fixtures/*.step")
foreach(FIXTURE_STEP_FILE ${FIXTURE_STEP_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${FIXTURE_STEP_FILE}"
            "$<TARGET_FILE_DIR:${PROJECT_NAME}>"
    )
endforeach()

# --- Focused physics-contract tests (no renderer or solver dependencies) ---
enable_testing()
add_executable(load_physics_tests
    tests/load_physics_tests.cpp
    src/LoadPhysics.cpp)
target_include_directories(load_physics_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(load_physics_tests PRIVATE /W4 /WX)
else()
    target_compile_options(load_physics_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME load_physics_tests COMMAND load_physics_tests)

# --- Focused UI-design contract tests (GL-free) ---
add_executable(ui_design_tests
    tests/ui_design_tests.cpp
    src/UIDesign.cpp)
target_include_directories(ui_design_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)
if(MSVC)
    target_compile_options(ui_design_tests PRIVATE /W4 /WX)
else()
    target_compile_options(ui_design_tests PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
add_test(NAME ui_design_tests COMMAND ui_design_tests)
