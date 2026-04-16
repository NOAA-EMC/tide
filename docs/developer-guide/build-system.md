# Build System

TIDE uses CMake (>= 3.18) as its build system. This page documents the project
structure, library targets, dependency management, and testing configuration.

## Project Structure

The top-level `CMakeLists.txt` defines the TIDE project and orchestrates the
build. Subdirectories are added in dependency order so that downstream libraries
can link against upstream ones:

```text
CMakeLists.txt              ← project root (Fortran, C, CXX)
├── cmake/
│   ├── FindESMF.cmake      ← locates ESMF via esmf.mk, also extracts PIO
│   ├── FindPIO.cmake       ← standalone PIO finder (C & Fortran components)
│   ├── LibFind.cmake       ← helper utilities for find-package modules
│   └── LibCheck.cmake      ← helper utilities for version/macro checks
├── src/
│   ├── share/              ← tide_share (static library)
│   ├── streams/            ← streams (library)
│   ├── dshr/               ← dshr (library)
│   └── CMakeLists.txt      ← tide (top-level API library)
└── tests/
    └── CMakeLists.txt      ← test executables (Fortran, C++, shell)
```

Subdirectories are added in this order inside the root `CMakeLists.txt`:

```cmake
add_subdirectory(src/share)    # 1. tide_share — no internal deps
add_subdirectory(src/streams)  # 2. streams    — depends on tide_share
add_subdirectory(src/dshr)     # 3. dshr       — depends on tide_share, streams
add_subdirectory(src)          # 4. tide       — depends on all three above
```

## Library Targets

### `tide_share` — Shared Utilities

Defined in `src/share/CMakeLists.txt`. A **static** Fortran library containing
low-level utilities (kind definitions, calendar, constants, string handling,
logging, assertions, etc.).

| Property | Value |
| --- | --- |
| Target name | `tide_share` |
| Library type | `STATIC` |
| Language | Fortran |
| Compile definition | `CPRGNU` |
| Source files | `shr_kind_mod.F90`, `shr_cal_mod.F90`, `shr_const_mod.F90`, `shr_string_mod.F90`, `shr_file_mod.F90`, `shr_sys_mod.F90`, `shr_abort_mod.F90`, `shr_log_mod.F90`, `shr_orb_mod.F90`, `shr_nl_mod.F90`, `shr_timer_mod.F90`, `shr_strconvert_mod.F90`, `shr_infnan_mod.F90` (generated), `shr_assert_mod.F90` (generated), `nuopc_shr_methods.F90` |

Two source files (`shr_infnan_mod.F90`, `shr_assert_mod.F90`) are generated at
build time from `.F90.in` templates using the
[genf90](https://github.com/PARALLELIO/genf90) preprocessor. If `GENF90_PATH`
is not set, CMake fetches genf90 automatically via `ExternalProject_Add`.

### `streams` — Stream I/O

Defined in `src/streams/CMakeLists.txt`. Provides data-stream reading, temporal
interpolation, and stream-data management.

| Property | Value |
| --- | --- |
| Target name | `streams` |
| Compile definition | `DISABLE_FoX` |
| Dependencies | `tide_share` (via `add_dependencies`) |
| Source files | `dshr_methods_mod.F90`, `dshr_strdata_mod.F90`, `dshr_stream_mod.F90`, `dshr_tinterp_mod.F90` |

### `dshr` — Data-Stream Handlers

Defined in `src/dshr/CMakeLists.txt`. Higher-level data-stream handler modules
that build on `streams` and `tide_share`.

| Property | Value |
| --- | --- |
| Target name | `dshr` |
| Public link libraries | `tide_share`, `streams` |
| Source files | `dshr_dfield_mod.F90`, `dshr_fldlist_mod.F90`, `dshr_mod.F90` |

### `tide` — Core API Library

Defined in `src/CMakeLists.txt`. The main TIDE library that applications link
against. It combines Fortran modules with a C++ bridge for YAML parsing.

| Property | Value |
| --- | --- |
| Target name | `tide` |
| Languages | Fortran + C++ |
| Fortran sources | Collected via `file(GLOB *.F90)` — includes `tide_mod.F90`, `tide_yaml_mod.F90`, `tide_cf_detection_mod.F90`, `tide_regrid_mod.F90`, `tide_output_mod.F90` |
| C++ sources | `tide_yaml_c.cpp` |
| Public link libraries | `dshr`, `streams`, `tide_share`, `PIO::PIO_Fortran`, `PIO::PIO_C`, ESMF |
| Private link libraries | `yaml-cpp::yaml-cpp` |

## External Dependencies

### ESMF

The Earth System Modeling Framework is located through `cmake/FindESMF.cmake`,
which parses the `esmf.mk` makefile shipped with every ESMF installation. The
path to `esmf.mk` is resolved in this order:

1. `ESMFMKFILE` environment variable
2. `ESMFMKFILE` CMake cache variable
3. Automatic search in `/opt/views/view/lib`, `/usr/lib`, `/usr/local/lib`

The module extracts `ESMF_F90COMPILEPATHS`, `ESMF_F90ESMFLINKRPATHS`,
`ESMF_F90LINKPATHS`, and `ESMF_F90ESMFLINKLIBS` from `esmf.mk` and converts
them into CMake lists.

### PIO (Parallel I/O)

PIO is discovered in two ways:

- **Via ESMF** — `FindESMF.cmake` also extracts PIO include directories and
  library paths from the ESMF link flags, then creates imported targets
  `PIO::PIO_Fortran` and `PIO::PIO_C`.
- **Standalone** — `cmake/FindPIO.cmake` provides a full component-based finder
  (C and Fortran components) using the helper modules `LibFind.cmake` and
  `LibCheck.cmake`.

### yaml-cpp

Used by the C++ YAML bridge (`tide_yaml_c.cpp`). The top-level `CMakeLists.txt`
first attempts `find_package(yaml-cpp QUIET)`. If no system installation is
found, it fetches version 0.8.0 from GitHub via `FetchContent`:

```cmake
FetchContent_Declare(yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG        0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)
```

Tests and extra tools are disabled during the fetch to keep the build minimal.

### genf90

A Perl-based Fortran preprocessor used to generate `shr_infnan_mod.F90` and
`shr_assert_mod.F90` from `.F90.in` templates. Fetched automatically via
`ExternalProject_Add` if `GENF90_PATH` is not provided.

## Testing

Tests are gated behind the `BUILD_TESTING` option (off by default):

```bash
cmake -B build -DBUILD_TESTING=ON
cmake --build build
ctest --test-dir build
```

The test suite in `tests/CMakeLists.txt` includes:

- **C++ tests** — linked against `yaml-cpp` for YAML round-trip and validation
  property tests.
- **Fortran tests** — linked against the `tide` library, ESMF, and PIO. A
  helper function `add_tide_fortran_test()` standardizes target creation.
- **Shell tests** — a build-system integration test (`test_build_system.sh`).

Tests are labeled (`property`, `unit`, `integration`, etc.) so you can run
subsets with `ctest -L <label>`.

## Install Layout

Each sub-library installs its archive to `lib/`. Fortran `.mod` files from all
four build directories are installed to `include/`:

```cmake
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/share/   DESTINATION include FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/streams/ DESTINATION include FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/dshr/    DESTINATION include FILES_MATCHING PATTERN "*.mod")
install(DIRECTORY ${CMAKE_BINARY_DIR}/src/         DESTINATION include FILES_MATCHING PATTERN "*.mod")
```

Use `CMAKE_INSTALL_PREFIX` to control the install destination:

```bash
cmake -B build -DCMAKE_INSTALL_PREFIX=/path/to/install
cmake --build build --target install
```
