cmake_minimum_required(VERSION 3.20)

option(USE_CUDA "Enable CUDA solver" ON)

# ---------------------------------------------------------------------------
# TODO_04: OpenCASCADE (OCCT) — STEP loader + analytic B-rep retention.
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
    src/GeometryUtils.cpp
    src/FEAModel.cpp
    src/FEASolver.cpp
    src/MeshQuality.cpp
    src/Tet10Element.cpp
    # new_TODO_02: headless agent self-test harness
    src/ScenarioRunner.cpp
    src/glad.c
    src/tetgen.cxx
    src/predicates.cxx
    # --- Geometry loader pipeline ---
    src/STLLoader.cpp
    src/ThreeMFLoader.cpp
    # TODO_04: STEP loader + analytic B-rep (OCC-guarded at compile time)
    src/StepLoader.cpp
    src/BRepHandle.cpp
    # miniz 2.2.0 split sources (ZIP decompressor for .3mf)
    ${miniz_SOURCE_DIR}/miniz.c
    ${miniz_SOURCE_DIR}/miniz_tdef.c
    ${miniz_SOURCE_DIR}/miniz_tinfl.c
    ${miniz_SOURCE_DIR}/miniz_zip.c
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

# --- OpenCASCADE (TODO_04) ---
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

# --- Copy scenarios/ and regression/ next to the executable (new_TODO_02) ---
# The headless harness reads scenarios/*.json and regression/*.txt relative to
# the working directory, so mirror both folders into the build output.
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/scenarios"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/scenarios"
    COMMENT "Copying scenarios/ to build output"
)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/regression"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/regression"
    COMMENT "Copying regression/ to build output"
)

# --- Diagnostic tool ---
# MeshQuality.cpp is linked into mesh_diag as well so both binaries share the
# same quality library (TODO_01).  The diagnostic still has its own local
# aspect-ratio routine, but future TODOs route mesh_diag through MeshQuality.
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

# --- TODO_01 fixture generator ---
# Standalone one-shot tool that writes assets/test_fixtures/unit_cube.stl by
# replicating FEAModel::generate_face's tessellation at size=1, sub=5.
# Independent of OpenGL/TetGen so it builds and runs in any environment.
add_executable(gen_unit_cube_stl tools/gen_unit_cube_stl.cpp)
set_target_properties(gen_unit_cube_stl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")

# --- TODO_04 STEP fixture generator ---
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
