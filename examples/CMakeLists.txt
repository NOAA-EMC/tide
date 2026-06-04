# =============================================================================
# TIDE Example Programs
# =============================================================================
# Built only when TIDE_BUILD_EXAMPLES=ON.
# Demonstrates TIDE Fortran binding usage in a model timestep loop.

# --- Fortran driver example ---
add_executable(driver_example driver_example.f90)
target_link_libraries(driver_example
  PRIVATE
    tide_fortran
    tide
    MPI::MPI_Fortran
)
# Ensure the Fortran .mod files are accessible
target_include_directories(driver_example
  PRIVATE
    ${CMAKE_BINARY_DIR}/mod
)
