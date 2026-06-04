# =============================================================================
# TIDE - Time and Interpolation Data Engine
# =============================================================================
cmake_minimum_required(VERSION 3.23)
project(TIDE
  VERSION 0.2.0
  LANGUAGES C CXX Fortran
  DESCRIPTION "Time and Interpolation Data Engine"
)

message(STATUS "TIDE version: ${PROJECT_VERSION}")

# ---------------------------------------------------------------------------
# C++ standard
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Position-independent code for shared library builds
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Make cmake/ modules available
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# ---------------------------------------------------------------------------
# External dependencies
# ---------------------------------------------------------------------------
include(FetchContent)

# --- MPI ---
find_package(MPI REQUIRED COMPONENTS C CXX Fortran)

# --- NetCDF-C (required for direct NetCDF fallback I/O) ---
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(NetCDF IMPORTED_TARGET netcdf)
endif()
if(NOT NetCDF_FOUND)
  find_library(NETCDF_LIBRARY NAMES netcdf netcdf_mpi)
  find_path(NETCDF_INCLUDE_DIR NAMES netcdf.h)
  if(NETCDF_LIBRARY AND NETCDF_INCLUDE_DIR)
    set(NetCDF_FOUND TRUE)
    add_library(NetCDF::netcdf UNKNOWN IMPORTED)
    set_target_properties(NetCDF::netcdf PROPERTIES
      IMPORTED_LOCATION "${NETCDF_LIBRARY}"
      INTERFACE_INCLUDE_DIRECTORIES "${NETCDF_INCLUDE_DIR}"
    )
  endif()
endif()

# --- mdspan (kokkos reference implementation, fallback if <mdspan> not available) ---
find_package(mdspan QUIET)
if(mdspan_FOUND)
  message(STATUS "Found kokkos/mdspan")
endif()

# --- yaml-cpp ---
find_package(yaml-cpp QUIET)
if(NOT yaml-cpp_FOUND)
  message(STATUS "yaml-cpp not found locally — fetching from GitHub")
  FetchContent_Declare(yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
  )
  set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(yaml-cpp)
  set(YAML_CPP_VERSION "0.8.0 (fetched)")
else()
  if(DEFINED yaml-cpp_VERSION)
    set(YAML_CPP_VERSION "${yaml-cpp_VERSION} (system)")
  else()
    set(YAML_CPP_VERSION "unknown (system)")
  endif()
endif()

# --- ECMWF Atlas ---
find_package(atlas QUIET)
if(NOT atlas_FOUND)
  # Also try eckit which Atlas depends on
  find_package(eckit QUIET)
  if(NOT eckit_FOUND)
    message(STATUS "eckit not found locally — fetching from GitHub")
    FetchContent_Declare(eckit
      GIT_REPOSITORY https://github.com/ecmwf/eckit.git
      GIT_TAG        1.28.0
    )
    set(ECKIT_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(eckit)
    set(ECKIT_VERSION "1.28.0 (fetched)")
  endif()

  message(STATUS "Atlas not found locally — fetching from GitHub")
  FetchContent_Declare(atlas
    GIT_REPOSITORY https://github.com/ecmwf/atlas.git
    GIT_TAG        0.39.0
  )
  set(ATLAS_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(atlas)
  set(ATLAS_VERSION "0.39.0 (fetched)")
else()
  if(DEFINED atlas_VERSION)
    set(ATLAS_VERSION "${atlas_VERSION} (system)")
  else()
    set(ATLAS_VERSION "unknown (system)")
  endif()
endif()

# --- AMIO (MPI-parallel I/O) ---
find_package(AMIO QUIET)
if(NOT AMIO_FOUND)
  find_package(amio QUIET)
endif()
if(NOT AMIO_FOUND AND NOT amio_FOUND)
  message(STATUS "AMIO not found locally — fetching from GitHub")
  FetchContent_Declare(amio
    GIT_REPOSITORY https://github.com/bbakernoaa/amio.git
    GIT_TAG        v0.3.0
  )
  FetchContent_MakeAvailable(amio)
  set(AMIO_VERSION "v0.3.0 (fetched)")
else()
  if(DEFINED AMIO_VERSION)
    set(AMIO_VERSION "${AMIO_VERSION} (system)")
  elseif(DEFINED amio_VERSION)
    set(AMIO_VERSION "${amio_VERSION} (system)")
  else()
    set(AMIO_VERSION "unknown (system)")
  endif()
endif()

# Determine the AMIO target name (varies by find method)
if(TARGET AMIO::amio_core)
  set(AMIO_TARGET AMIO::amio_core)
elseif(TARGET amio::amio)
  set(AMIO_TARGET amio::amio)
elseif(TARGET amio)
  set(AMIO_TARGET amio)
else()
  set(AMIO_TARGET "")
endif()

# --- TSPACK (vendored tension spline library) ---
add_subdirectory(extern/tspack)
set(TSPACK_VERSION "vendored")

# ---------------------------------------------------------------------------
# TIDE library sources
# ---------------------------------------------------------------------------
# Collect source files (populated as implementation progresses)
file(GLOB_RECURSE TIDE_CXX_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)
file(GLOB_RECURSE TIDE_C_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.c"
)
set(TIDE_SOURCES ${TIDE_CXX_SOURCES} ${TIDE_C_SOURCES})

# Fortran module sources
file(GLOB TIDE_FORTRAN_SOURCES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.f90"
  "${CMAKE_CURRENT_SOURCE_DIR}/src/*.F90"
)

# ---------------------------------------------------------------------------
# Shared library target
# ---------------------------------------------------------------------------
add_library(tide SHARED ${TIDE_SOURCES})
target_include_directories(tide
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(tide
  PUBLIC
    MPI::MPI_CXX
    yaml-cpp::yaml-cpp
  PRIVATE
    tspack
)
# Link NetCDF-C for direct NetCDF fallback I/O
if(TARGET PkgConfig::NetCDF)
  target_link_libraries(tide PRIVATE PkgConfig::NetCDF)
elseif(TARGET NetCDF::netcdf)
  target_link_libraries(tide PRIVATE NetCDF::netcdf)
elseif(NETCDF_LIBRARY)
  target_link_libraries(tide PRIVATE ${NETCDF_LIBRARY})
  target_include_directories(tide PRIVATE ${NETCDF_INCLUDE_DIR})
endif()
# Link mdspan if found (provides <experimental/mdspan> include path)
if(TARGET std::mdspan)
  target_link_libraries(tide PUBLIC std::mdspan)
endif()
# Link Atlas if available as imported target
if(TARGET atlas)
  target_link_libraries(tide PUBLIC atlas)
endif()
# Link AMIO if available as imported target
if(TARGET AMIO::amio_core)
  target_link_libraries(tide PUBLIC AMIO::amio_core)
elseif(TARGET amio)
  target_link_libraries(tide PUBLIC amio)
elseif(TARGET amio::amio)
  target_link_libraries(tide PUBLIC amio::amio)
endif()
set_target_properties(tide PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
  EXPORT_NAME tide
)

# ---------------------------------------------------------------------------
# Static library target
# ---------------------------------------------------------------------------
add_library(tide_static STATIC ${TIDE_SOURCES})
target_include_directories(tide_static
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)
target_link_libraries(tide_static
  PUBLIC
    MPI::MPI_CXX
    yaml-cpp::yaml-cpp
  PRIVATE
    tspack
)
# Link NetCDF-C for direct NetCDF fallback I/O
if(TARGET PkgConfig::NetCDF)
  target_link_libraries(tide_static PRIVATE PkgConfig::NetCDF)
elseif(TARGET NetCDF::netcdf)
  target_link_libraries(tide_static PRIVATE NetCDF::netcdf)
elseif(NETCDF_LIBRARY)
  target_link_libraries(tide_static PRIVATE ${NETCDF_LIBRARY})
  target_include_directories(tide_static PRIVATE ${NETCDF_INCLUDE_DIR})
endif()
if(TARGET std::mdspan)
  target_link_libraries(tide_static PUBLIC std::mdspan)
endif()
if(TARGET atlas)
  target_link_libraries(tide_static PUBLIC atlas)
endif()
if(TARGET AMIO::amio_core)
  target_link_libraries(tide_static PUBLIC AMIO::amio_core)
elseif(TARGET amio)
  target_link_libraries(tide_static PUBLIC amio)
elseif(TARGET amio::amio)
  target_link_libraries(tide_static PUBLIC amio::amio)
endif()
set_target_properties(tide_static PROPERTIES
  OUTPUT_NAME tide
  EXPORT_NAME tide_static
)

# ---------------------------------------------------------------------------
# Fortran module target
# ---------------------------------------------------------------------------
if(TIDE_FORTRAN_SOURCES)
  add_library(tide_fortran ${TIDE_FORTRAN_SOURCES})
  target_link_libraries(tide_fortran
    PUBLIC tide
    PRIVATE MPI::MPI_Fortran
  )
  set_target_properties(tide_fortran PROPERTIES
    Fortran_MODULE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mod
  )
  target_include_directories(tide_fortran
    PUBLIC
      $<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/mod>
      $<INSTALL_INTERFACE:include>
  )
else()
  # Create an interface target placeholder until Fortran sources are added
  add_library(tide_fortran INTERFACE)
  target_link_libraries(tide_fortran INTERFACE tide)
endif()

# ---------------------------------------------------------------------------
# Install rules
# ---------------------------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS tide tide_static tide_fortran tspack
  EXPORT TIDETargets
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public headers
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  FILES_MATCHING
    PATTERN "*.h"
    PATTERN "*.hpp"
)

# Install Fortran module files
if(EXISTS ${CMAKE_CURRENT_BINARY_DIR}/mod)
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/mod/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.mod"
  )
endif()

# Export targets for downstream find_package
install(EXPORT TIDETargets
  FILE TIDETargets.cmake
  NAMESPACE TIDE::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TIDE
)

# ---------------------------------------------------------------------------
# Testing
# ---------------------------------------------------------------------------
option(TIDE_BUILD_TESTS "Build TIDE unit and integration tests" OFF)
if(TIDE_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

# ---------------------------------------------------------------------------
# Examples
# ---------------------------------------------------------------------------
option(TIDE_BUILD_EXAMPLES "Build example programs" OFF)
if(TIDE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# ---------------------------------------------------------------------------
# Status summary — resolved dependency versions
# ---------------------------------------------------------------------------
message(STATUS "")
message(STATUS "=== TIDE Build Configuration ===")
message(STATUS "  TIDE version    : ${PROJECT_VERSION}")
message(STATUS "  C++ standard    : C++${CMAKE_CXX_STANDARD}")
message(STATUS "  MPI             : ${MPI_CXX_VERSION} (${MPI_CXX_COMPILER})")
message(STATUS "  yaml-cpp        : ${YAML_CPP_VERSION}")
message(STATUS "  Atlas           : ${ATLAS_VERSION}")
message(STATUS "  AMIO            : ${AMIO_VERSION}")
message(STATUS "  TSPACK          : ${TSPACK_VERSION}")
message(STATUS "  Build tests     : ${TIDE_BUILD_TESTS}")
message(STATUS "  Build examples  : ${TIDE_BUILD_EXAMPLES}")
message(STATUS "  Install prefix  : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "================================")
message(STATUS "")
