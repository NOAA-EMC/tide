# ---------------------------------------------------------------------------
# tide sub-library (API layer)
# ---------------------------------------------------------------------------

# Collect Fortran sources via glob so that new .F90 files are picked up automatically.
file(GLOB F90_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/*.F90)

# C++ source (yaml-cpp bridge)
set(CXX_SRCS ${CMAKE_CURRENT_SOURCE_DIR}/tide_yaml_c.cpp)

add_library(tide ${F90_SRCS} ${CXX_SRCS})

# Fortran .mod files are written to CMAKE_CURRENT_BINARY_DIR
target_include_directories(tide PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
)

# ESMF and PIO headers for Fortran compilation
target_include_directories(tide PRIVATE
  ${ESMF_F90COMPILEPATHS}
  ${PIO_Fortran_INCLUDE_DIR}
)

# Link sub-libraries and external dependencies
target_link_libraries(tide PUBLIC dshr streams cdeps_share)
target_link_libraries(tide PRIVATE yaml-cpp::yaml-cpp)

install(TARGETS tide ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
