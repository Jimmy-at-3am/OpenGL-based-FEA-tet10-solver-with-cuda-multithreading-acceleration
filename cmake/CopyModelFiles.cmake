# CopyModelFiles.cmake — called as a POST_BUILD script so the glob runs at
# build time, not configure time.  New files in assets/models/ are picked up
# on the next build without needing to re-run cmake.
#
# Required variables (passed via -D):
#   SRC_DIR  — source directory to scan
#   DST_DIR  — destination directory (TARGET_FILE_DIR of the executable)

file(GLOB _model_files
    "${SRC_DIR}/*.stl"  "${SRC_DIR}/*.STL"
    "${SRC_DIR}/*.3mf"  "${SRC_DIR}/*.3MF"
    "${SRC_DIR}/*.step" "${SRC_DIR}/*.STEP"
    "${SRC_DIR}/*.stp"  "${SRC_DIR}/*.STP"
)

foreach(_f ${_model_files})
    file(COPY "${_f}" DESTINATION "${DST_DIR}")
endforeach()
