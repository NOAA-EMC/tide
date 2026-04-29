# Building

This page walks through the CMake build steps for TIDE. Make sure you have
all [prerequisites installed](installation.md) before proceeding.

## Environment Setup

TIDE locates ESMF (and often PIO) through the `ESMFMKFILE` environment
variable. Ensure it is set before running CMake:

```bash
export ESMFMKFILE=/path/to/esmf/lib/esmf.mk
```

If yaml-cpp is not installed system-wide, the build system fetches it
automatically via CMake `FetchContent` — no extra setup is needed.

## Basic Build

Create an out-of-source build directory and run CMake:

```bash
mkdir build
cd build
cmake ..
make
```

CMake requires version 3.18 or newer and enables the Fortran, C, and C++
languages. The build discovers ESMF, PIO, and yaml-cpp through the custom
find modules in the `cmake/` directory.

## Custom Install Prefix

To install TIDE libraries and Fortran `.mod` files to a specific location,
pass `CMAKE_INSTALL_PREFIX`:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/install
make install
```

The install step places:

- Library archives in `<prefix>/lib/`
- Fortran module files (`.mod`) in `<prefix>/include/`

## Building with Tests

TIDE ships with a test suite that is disabled by default. Enable it with the
`TIDE_BUILD_TESTS` option:

```bash
cmake .. -DTIDE_BUILD_TESTS=ON
make
ctest
```

The test suite includes Fortran unit tests, C++ property tests, and
integration tests. Some tests require the ESMF and PIO runtime environment
to be available.

You can combine options as needed:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/tide -DTIDE_BUILD_TESTS=ON
make
ctest
make install
```

## Build Options Reference

| Option | Default | Description |
| --- | --- | --- |
| `CMAKE_INSTALL_PREFIX` | system default | Installation path for libraries and module files |
| `TIDE_BUILD_TESTS` | `OFF` | Enable the TIDE test suite |

## Next Steps

Once the build completes, head to the [Quickstart](quickstart.md) guide for
a minimal usage example.
