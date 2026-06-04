# =============================================================================
# TIDE Test Suite - Google Test + RapidCheck + MPI Test Infrastructure
# =============================================================================
include(FetchContent)

# ---------------------------------------------------------------------------
# Google Test
# ---------------------------------------------------------------------------
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.14.0
)
# Prevent overriding parent project compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(BUILD_GMOCK ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# ---------------------------------------------------------------------------
# RapidCheck (property-based testing)
# ---------------------------------------------------------------------------
FetchContent_Declare(rapidcheck
  GIT_REPOSITORY https://github.com/emil-e/rapidcheck.git
  GIT_TAG        ff6af6fc683159deb51c543b065eba14dfcf329b
)
set(RC_ENABLE_GTEST ON CACHE BOOL "" FORCE)
set(RC_ENABLE_GMOCK ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(rapidcheck)

# ---------------------------------------------------------------------------
# Include Google Test's CTest integration
# ---------------------------------------------------------------------------
include(GoogleTest)

# ---------------------------------------------------------------------------
# Helper macro: add_tide_test
#
# Creates a unit test executable linked against GTest, RapidCheck, and TIDE.
# Automatically registers tests with CTest via gtest_discover_tests().
#
# Usage:
#   add_tide_test(NAME test_scaling SOURCES test_scaling.cpp)
# ---------------------------------------------------------------------------
macro(add_tide_test)
  cmake_parse_arguments(TIDE_TEST "" "NAME" "SOURCES" ${ARGN})

  add_executable(${TIDE_TEST_NAME} ${TIDE_TEST_SOURCES})
  target_link_libraries(${TIDE_TEST_NAME}
    PRIVATE
      tide
      GTest::gtest_main
      GTest::gmock
      rapidcheck
      rapidcheck_gtest
  )
  target_include_directories(${TIDE_TEST_NAME}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
  )

  gtest_discover_tests(${TIDE_TEST_NAME}
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
      LABELS "unit"
  )
endmacro()

# ---------------------------------------------------------------------------
# Helper macro: add_mpi_test
#
# Creates an MPI test executable and registers it with CTest using mpiexec.
# The test runs with the specified number of MPI ranks.
#
# Usage:
#   add_mpi_test(NAME test_mpi_pipeline NPROCS 4 SOURCES test_mpi.cpp)
# ---------------------------------------------------------------------------
macro(add_mpi_test)
  cmake_parse_arguments(MPI_TEST "" "NAME;NPROCS" "SOURCES" ${ARGN})

  if(NOT MPI_TEST_NPROCS)
    set(MPI_TEST_NPROCS 2)
  endif()

  add_executable(${MPI_TEST_NAME} ${MPI_TEST_SOURCES})
  target_link_libraries(${MPI_TEST_NAME}
    PRIVATE
      tide
      GTest::gtest_main
      GTest::gmock
      rapidcheck
      rapidcheck_gtest
      MPI::MPI_CXX
  )
  target_include_directories(${MPI_TEST_NAME}
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
  )

  add_test(
    NAME ${MPI_TEST_NAME}
    COMMAND ${MPIEXEC_EXECUTABLE} ${MPIEXEC_NUMPROC_FLAG} ${MPI_TEST_NPROCS}
            ${MPIEXEC_PREFLAGS} $<TARGET_FILE:${MPI_TEST_NAME}> ${MPIEXEC_POSTFLAGS}
  )
  set_tests_properties(${MPI_TEST_NAME} PROPERTIES
    LABELS "mpi"
    PROCESSORS ${MPI_TEST_NPROCS}
    TIMEOUT 120
  )
endmacro()

# ---------------------------------------------------------------------------
# Test data directory (available to tests via compile definition)
# ---------------------------------------------------------------------------
set(TIDE_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/data")
add_compile_definitions(TIDE_TEST_DATA_DIR="${TIDE_TEST_DATA_DIR}")

# ---------------------------------------------------------------------------
# Test subdirectories
# ---------------------------------------------------------------------------
add_subdirectory(unit)
add_subdirectory(integration)

# Conditionally include Fortran tests if Fortran is enabled
get_property(_languages GLOBAL PROPERTY ENABLED_LANGUAGES)
if("Fortran" IN_LIST _languages)
  add_subdirectory(fortran)
endif()
