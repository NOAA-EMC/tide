cmake_minimum_required(VERSION 3.18)
project(TIDE
  VERSION 0.1.0
  LANGUAGES Fortran C CXX
  DESCRIPTION "Time-Interpolated Data Engine"
)

# Semantic version components available as:
#   TIDE_VERSION_MAJOR, TIDE_VERSION_MINOR, TIDE_VERSION_PATCH
#   PROJECT_VERSION  (e.g. "0.1.0")
message(STATUS "TIDE version: ${PROJECT_VERSION}")

# Make cmake/ modules available for FindESMF, FindPIO, etc.
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------
include(FindESMF)
find_package(PIO REQUIRED COMPONENTS C Fortran)
find_package(yaml-cpp REQUIRED)

# ---------------------------------------------------------------------------
# Subdirectories in dependency order
# ---------------------------------------------------------------------------
add_subdirectory(src/share)
add_subdirectory(src/streams)
add_subdirectory(src/dshr)
add_subdirectory(src)

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
option(BUILD_TESTING "Build TIDE tests" OFF)
if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Install: Fortran .mod files from each sub-library build directory
# (Library archive installs are handled by each sub-library's CMakeLists.txt)
# ---------------------------------------------------------------------------
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/share/ DESTINATION include
        FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/streams/ DESTINATION include
        FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/dshr/ DESTINATION include
        FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/ DESTINATION include
        FILES_MATCHING PATTERN "*.mod")
