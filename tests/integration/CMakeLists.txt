# =============================================================================
# TIDE Integration Tests
# =============================================================================
# Integration and MPI test executables registered conditionally — missing
# source files do not break the build.

# --- Atlas regridding integration tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_atlas_regrid.cpp")
  add_tide_test(NAME test_atlas_regrid SOURCES test_atlas_regrid.cpp)
endif()

# --- SCRIP weight file round-trip integration test ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_scrip_roundtrip.cpp")
  add_tide_test(NAME test_scrip_roundtrip SOURCES test_scrip_roundtrip.cpp)
endif()

# --- Find NetCDF-C for integration tests that read data directly ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(NETCDF_PKG QUIET IMPORTED_TARGET netcdf)
endif()
# Fallback: try to find netcdf library directly
if(NOT NETCDF_PKG_FOUND)
  find_library(NETCDF_LIBRARY NAMES netcdf netcdf_mpi HINTS /usr/lib /usr/lib64)
  find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h HINTS /usr/include)
  if(NETCDF_LIBRARY AND NETCDF_INCLUDE_DIR)
    set(NETCDF_FOUND TRUE)
  endif()
endif()

# --- End-to-end pipeline test ---
# Uses a custom main() for MPI initialization, so we don't use add_tide_test
# (which links GTest::gtest_main). Instead we link GTest::gtest directly.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_pipeline_e2e.cpp")
  add_executable(test_pipeline_e2e test_pipeline_e2e.cpp)
  target_link_libraries(test_pipeline_e2e
    PRIVATE
      tide
      GTest::gtest
      GTest::gmock
      MPI::MPI_CXX
  )
  # Link NetCDF-C directly for test data reading
  if(TARGET PkgConfig::NETCDF_PKG)
    target_link_libraries(test_pipeline_e2e PRIVATE PkgConfig::NETCDF_PKG)
  elseif(NETCDF_FOUND)
    target_link_libraries(test_pipeline_e2e PRIVATE ${NETCDF_LIBRARY})
    target_include_directories(test_pipeline_e2e PRIVATE ${NETCDF_INCLUDE_DIR})
  endif()
  target_include_directories(test_pipeline_e2e
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/tests/data
  )

  # Register as an MPI test (single rank is sufficient for this test)
  add_test(
    NAME test_pipeline_e2e
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 1
            --allow-run-as-root
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:test_pipeline_e2e> ${MPIEXEC_POSTFLAGS}
  )
  set_tests_properties(test_pipeline_e2e PROPERTIES
    LABELS "integration;mpi"
    PROCESSORS 1
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()

# --- Conservative regridding with ESMF mesh integration test ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_conservative_esmf.cpp")
  add_executable(test_conservative_esmf test_conservative_esmf.cpp)
  target_link_libraries(test_conservative_esmf
    PRIVATE
      tide
      GTest::gtest
      GTest::gmock
      MPI::MPI_CXX
  )
  # Link NetCDF-C directly for ESMF mesh file reading
  if(TARGET PkgConfig::NETCDF_PKG)
    target_link_libraries(test_conservative_esmf PRIVATE PkgConfig::NETCDF_PKG)
  elseif(NETCDF_FOUND)
    target_link_libraries(test_conservative_esmf PRIVATE ${NETCDF_LIBRARY})
    target_include_directories(test_conservative_esmf PRIVATE ${NETCDF_INCLUDE_DIR})
  endif()
  target_include_directories(test_conservative_esmf
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/tests/data
  )

  # Register as MPI test (single rank sufficient)
  add_test(
    NAME test_conservative_esmf
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 1
            --allow-run-as-root
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:test_conservative_esmf> ${MPIEXEC_POSTFLAGS}
  )
  set_tests_properties(test_conservative_esmf PROPERTIES
    LABELS "integration"
    PROCESSORS 1
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()

# --- Prefetch equivalence integration test ---
# Validates Property 7: prefetch enabled produces bitwise identical output to disabled
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_prefetch.cpp")
  add_executable(test_prefetch_equivalence test_prefetch.cpp)
  target_link_libraries(test_prefetch_equivalence
    PRIVATE
      tide
      GTest::gtest
      GTest::gmock
      MPI::MPI_CXX
  )
  target_include_directories(test_prefetch_equivalence
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
      ${CMAKE_SOURCE_DIR}/tests/data
  )

  add_test(
    NAME test_prefetch_equivalence
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 1
            --allow-run-as-root
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:test_prefetch_equivalence> ${MPIEXEC_POSTFLAGS}
  )
  set_tests_properties(test_prefetch_equivalence PROPERTIES
    LABELS "integration;mpi"
    PROCESSORS 1
    TIMEOUT 120
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
endif()

# --- MPI reproducibility test (run with 2 ranks for CI efficiency) ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_mpi_reproducibility.cpp")
  # Custom target: uses its own main() for MPI init/finalize, so we
  # link GTest::gtest (not gtest_main) to avoid duplicate main symbols.
  add_executable(test_mpi_reproducibility test_mpi_reproducibility.cpp)
  target_link_libraries(test_mpi_reproducibility
    PRIVATE
      tide
      GTest::gtest
      GTest::gmock
      MPI::MPI_CXX
  )
  target_include_directories(test_mpi_reproducibility
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
  )

  add_test(
    NAME test_mpi_reproducibility
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} 2
            --oversubscribe --allow-run-as-root
            $<TARGET_FILE:test_mpi_reproducibility>
  )
  set_tests_properties(test_mpi_reproducibility PROPERTIES
    LABELS "mpi"
    PROCESSORS 2
    TIMEOUT 120
  )
endif()
