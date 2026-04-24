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
# FindESMF parses esmf.mk and also extracts PIO paths from it
include(FindESMF)

# yaml-cpp: try system install first, fetch from GitHub if not found
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  message(STATUS "yaml-cpp not found locally — fetching from GitHub")
  include(FetchContent)
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
  )
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(yaml-cpp)
endif()

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
option(TIDE_BUILD_TESTS "Build TIDE tests" OFF)
if(TIDE_BUILD_TESTS)
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
