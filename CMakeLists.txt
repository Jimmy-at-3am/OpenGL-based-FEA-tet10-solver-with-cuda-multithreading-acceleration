cmake_minimum_required(VERSION 3.20)

option(USE_CUDA "Enable CUDA solver" ON)

# CMAKE_CUDA_COMPILER and CUDA arch must be set before project() / enable_language(CUDA).
if(USE_CUDA)
    set(CMAKE_CUDA_COMPILER "D:/program_files_delta/cuda/bin/nvcc.exe")
    # Pin to a real SM rather than 'native'; -arch=native adds compiler passes
    # that misparse Windows paths containing spaces and a dash.
    set(CMAKE_CUDA_ARCHITECTURES 75)
endif()

project(FEAPreProcessor LANGUAGES C CXX)

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
    ${CMAKE_SOURCE_DIR}/include        # glad, GLFW, glm, KHR, stb_image, learnopengl headers
    ${CMAKE_SOURCE_DIR}/src            # project headers: camera.h, tetgen.h, mesh.h, model.h, etc.
    ${eigen_SOURCE_DIR}                # Eigen library headers
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
    src/glad.c
    src/tetgen.cxx
    src/predicates.cxx
    # --- Geometry loader pipeline ---
    src/STLLoader.cpp
    src/ThreeMFLoader.cpp
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
file(GLOB STL_FILES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/assets/models/*.stl" "${CMAKE_SOURCE_DIR}/assets/models/*.STL")

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

# --- Copy .3mf model files next to the executable ---
file(GLOB MODEL_3MF_FILES CONFIGURE_DEPENDS
    "${CMAKE_SOURCE_DIR}/assets/models/*.3mf"
    "${CMAKE_SOURCE_DIR}/assets/models/*.3MF")

foreach(TMF_FILE ${MODEL_3MF_FILES})
    add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${TMF_FILE}"
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
        "${CMAKE_SOURCE_DIR}/assets/models/revenge spindex - Part 33 (6).stl"
        "$<TARGET_FILE_DIR:mesh_diag>"
)

# --- TODO_01 fixture generator ---
# Standalone one-shot tool that writes assets/test_fixtures/unit_cube.stl by
# replicating FEAModel::generate_face's tessellation at size=1, sub=5.
# Independent of OpenGL/TetGen so it builds and runs in any environment.
add_executable(gen_unit_cube_stl tools/gen_unit_cube_stl.cpp)
set_target_properties(gen_unit_cube_stl PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tools")
