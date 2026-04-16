# Installation

This page covers the prerequisites and dependency installation steps needed
before building TIDE.

## Prerequisites

| Dependency | Minimum Version | Purpose |
| --- | --- | --- |
| CMake | 3.18 | Build system generator |
| Fortran compiler | — | Compiles Fortran source modules |
| C compiler | — | Required by CMake `C` language support |
| C++ compiler | — | Compiles the YAML bridge (`tide_yaml_c.cpp`) |
| ESMF | — | Earth System Modeling Framework for regridding and field management |
| PIO | — | Parallel I/O library for NetCDF read/write |
| yaml-cpp | — | YAML parser used by the C++ configuration layer |

GCC (`gfortran`, `gcc`, `g++`) and Intel (`ifort`/`ifx`, `icc`/`icx`, `icpc`/`icpx`)
compiler suites are both known to work.

## Installing Dependencies

### CMake

CMake 3.18 or newer is required. Most Linux distributions ship a recent
enough version. You can check your installed version with:

```bash
cmake --version
```

If you need to install or upgrade:

```bash
# Ubuntu / Debian
sudo apt-get install cmake

# macOS (Homebrew)
brew install cmake

# From source (any platform)
wget https://github.com/Kitware/CMake/releases/download/v3.28.3/cmake-3.28.3.tar.gz
tar xzf cmake-3.28.3.tar.gz
cd cmake-3.28.3
./bootstrap && make && sudo make install
```

### Fortran, C, and C++ Compilers

TIDE requires compilers for all three languages. The GCC toolchain is the
most common choice:

```bash
# Ubuntu / Debian
sudo apt-get install gfortran gcc g++

# macOS (Homebrew)
brew install gcc
```

### ESMF (Earth System Modeling Framework)

[ESMF](https://earthsystemmodeling.org/) provides the regridding and
field management infrastructure that TIDE relies on. ESMF also bundles PIO,
so installing ESMF may satisfy the PIO requirement as well.

The build system locates ESMF through the `ESMFMKFILE` environment variable,
which should point to the `esmf.mk` file installed with ESMF:

```bash
export ESMFMKFILE=/path/to/esmf/lib/esmf.mk
```

To build ESMF from source, refer to the
[ESMF User Guide](https://earthsystemmodeling.org/doc/).
A typical build looks like:

```bash
git clone https://github.com/esmf-org/esmf.git
cd esmf
export ESMF_DIR=$(pwd)
export ESMF_INSTALL_PREFIX=/opt/esmf
make
make install
export ESMFMKFILE=/opt/esmf/lib/esmf.mk
```

If you use [Spack](https://spack.io/), ESMF can be installed with:

```bash
spack install esmf
spack load esmf
```

### PIO (Parallel I/O)

[PIO](https://ncar.github.io/ParallelIO/) is the Parallel I/O library used
for NetCDF file operations. In many HPC environments PIO is bundled with
ESMF and is discovered automatically from `esmf.mk`. If PIO is not found
through ESMF, you can install it separately.

From source:

```bash
git clone https://github.com/NCAR/ParallelIO.git
cd ParallelIO
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/pio
make && make install
```

With Spack:

```bash
spack install parallelio
spack load parallelio
```

### yaml-cpp

[yaml-cpp](https://github.com/jbeder/yaml-cpp) is a C++ YAML parser.
If a system installation is found by CMake's `find_package`, it will be
used automatically. Otherwise, the TIDE build system fetches yaml-cpp
v0.8.0 from GitHub via CMake `FetchContent`, so a manual install is
optional.

To install it explicitly:

```bash
# Ubuntu / Debian
sudo apt-get install libyaml-cpp-dev

# macOS (Homebrew)
brew install yaml-cpp

# From source
git clone https://github.com/jbeder/yaml-cpp.git
cd yaml-cpp
mkdir build && cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/opt/yaml-cpp
make && make install
```

## Verifying Your Environment

Before building TIDE, confirm that the required tools are available:

```bash
cmake --version          # Should be >= 3.18
gfortran --version       # Or your Fortran compiler of choice
gcc --version            # Or your C compiler of choice
g++ --version            # Or your C++ compiler of choice
echo $ESMFMKFILE         # Should point to esmf.mk
```

Once all prerequisites are in place, proceed to [Building](building.md).
