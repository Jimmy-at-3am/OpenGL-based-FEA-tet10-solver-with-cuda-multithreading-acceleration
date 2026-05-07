cmake_minimum_required(VERSION 3.20)
project(FEAPreProcessor LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# --- CUDA (optional) ---
set(CMAKE_CUDA_COMPILER "D:/program_files_delta/cuda/bin/nvcc.exe")
set(CUDAToolkit_ROOT     "D:/program_files_delta/cuda")
enable_language(CUDA)
find_package(CUDAToolkit REQUIRED)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_ARCHITECTURES native)
set(HAS_CUDA TRUE)

include(FetchContent)
FetchContent_Declare(
  eigen
  GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
  GIT_TAG        3.4.0
)
FetchContent_MakeAvailable(eigen)

# --- Include directories ---
include_directories(
    ${CMAKE_SOURCE_DIR}/include        # glad, GLFW, glm, KHR, stb_image, learnopengl headers
    ${CMAKE_SOURCE_DIR}/src            # project headers: camera.h, tetgen.h, mesh.h, model.h, etc.
    ${eigen_SOURCE_DIR}                # Eigen library headers
    ${CMAKE_SOURCE_DIR}/ext/meshoptimizer/src
    ${CUDAToolkit_INCLUDE_DIRS}        # CUDA headers (cusolverSp.h, cusparse.h)
)

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
    src/Tet10Element.cpp
    src/glad.c
    src/tetgen.cxx
    src/predicates.cxx
)

# CUDA source (compiled by nvcc)
set(CUDA_SOURCES
    src/CudaSolver.cu
)

add_subdirectory(ext/meshoptimizer)

# --- Create executable (C/C++ + CUDA sources) ---
add_executable(${PROJECT_NAME} ${SOURCES} ${CUDA_SOURCES})

# --- Link libraries ---
# glfw3dll.lib is the MSVC import library for glfw3.dll
# opengl32 and gdi32 are Windows system libraries
find_package(OpenMP REQUIRED)

target_link_libraries(${PROJECT_NAME} PRIVATE
    glfw3dll
    opengl32
    gdi32
    OpenMP::OpenMP_CXX
    meshoptimizer
    CUDA::cudart
    CUDA::cublas
    CUDA::cusparse
)

target_compile_definitions(${PROJECT_NAME} PRIVATE HAS_CUDA=1)

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
add_executable(mesh_diag src/mesh_diag.cpp src/tetgen.cxx src/predicates.cxx)
target_link_libraries(mesh_diag PRIVATE gdi32)
add_custom_command(TARGET mesh_diag POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_SOURCE_DIR}/assets/models/revenge spindex - Part 33 (6).stl"
        "$<TARGET_FILE_DIR:mesh_diag>"
)
