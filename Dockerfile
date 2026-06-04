# =============================================================================
# TIDE Multi-Stage Dockerfile
# Builds the complete TIDE library and all dependencies from source,
# then runs the full test suite in an isolated, reproducible environment.
#
# Stage 1 (deps):  System packages (HDF5-MPI, NetCDF-C) + build eckit,
#                   Atlas, yaml-cpp, AMIO from source
# Stage 2 (build): Copy TIDE source, configure and build with tests enabled
#
# Usage:
#   docker build -t tide-test .
#   docker run --rm tide-test
# =============================================================================

# ---------------------------------------------------------------------------
# Stage 1: Build all external dependencies
# Ubuntu 24.04 with GCC 14 provides full C++23 support including std::mdspan
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS deps

ENV DEBIAN_FRONTEND=noninteractive
ENV INSTALL_PREFIX=/opt/tide-deps
ENV PATH="${INSTALL_PREFIX}/bin:${PATH}"
ENV LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX}/lib64"
ENV CMAKE_PREFIX_PATH="${INSTALL_PREFIX}"

# System packages: compilers, MPI, build tools, HDF5-MPI, and NetCDF-C.
# Using system-packaged HDF5/NetCDF avoids build-time zlib detection issues
# and ensures proper transitive dependency resolution.
# GCC 14 is used for full C++23 support including std::mdspan.
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gfortran-14 \
    g++-14 \
    gcc-14 \
    cmake \
    ninja-build \
    git \
    wget \
    ca-certificates \
    pkg-config \
    libopenmpi-dev \
    openmpi-bin \
    libhdf5-mpi-dev \
    libnetcdf-dev \
    libnetcdf-mpi-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    zlib1g-dev \
    libqhull-dev \
    python3 \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-14 100

# Verify GCC version is 14+
RUN gcc --version | head -1

WORKDIR /build

# ---------------------------------------------------------------------------
# Build yaml-cpp
# ---------------------------------------------------------------------------
ARG YAMLCPP_VERSION=0.8.0
RUN wget -q "https://github.com/jbeder/yaml-cpp/archive/refs/tags/${YAMLCPP_VERSION}.tar.gz" \
    && tar xzf ${YAMLCPP_VERSION}.tar.gz \
    && cd yaml-cpp-${YAMLCPP_VERSION} \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        -DCMAKE_BUILD_TYPE=Release \
        -DYAML_CPP_BUILD_TESTS=OFF \
        -DYAML_CPP_BUILD_TOOLS=OFF \
        -DYAML_CPP_BUILD_CONTRIB=OFF \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && cd /build && rm -rf yaml-cpp-${YAMLCPP_VERSION}* ${YAMLCPP_VERSION}.tar.gz

# ---------------------------------------------------------------------------
# Build ecbuild (ECMWF CMake macros - required by eckit)
# ---------------------------------------------------------------------------
ARG ECBUILD_VERSION=3.8.5
RUN git clone --depth 1 --branch ${ECBUILD_VERSION} \
        https://github.com/ecmwf/ecbuild.git \
    && cd ecbuild \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
    && cmake --build build \
    && cmake --install build \
    && cd /build && rm -rf ecbuild

# ---------------------------------------------------------------------------
# Build eckit (required dependency for Atlas)
# ---------------------------------------------------------------------------
ARG ECKIT_VERSION=1.28.0
RUN git clone --depth 1 --branch ${ECKIT_VERSION} \
        https://github.com/ecmwf/eckit.git \
    && cd eckit \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=${INSTALL_PREFIX} \
        -DENABLE_TESTS=OFF \
        -DENABLE_EXAMPLES=OFF \
        -DENABLE_MPI=ON \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && cd /build && rm -rf eckit

# ---------------------------------------------------------------------------
# Build ECMWF Atlas (horizontal regridding)
# ---------------------------------------------------------------------------
ARG ATLAS_VERSION=0.39.0
RUN git clone --depth 1 --branch ${ATLAS_VERSION} \
        https://github.com/ecmwf/atlas.git \
    && cd atlas \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=${INSTALL_PREFIX} \
        -DENABLE_TESTS=OFF \
        -DENABLE_EXAMPLES=OFF \
        -DATLAS_ENABLE_FORTRAN=OFF \
        -DATLAS_ENABLE_QHULL=ON \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && cd /build && rm -rf atlas

# ---------------------------------------------------------------------------
# Build AMIO (MPI-parallel I/O for NetCDF/GRIB2)
# ---------------------------------------------------------------------------
RUN git clone --depth 1 https://github.com/bbakernoaa/amio.git \
    && cd amio \
    && cmake -S . -B build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=${INSTALL_PREFIX} \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_PREFIX_PATH=${INSTALL_PREFIX} \
        -DBUILD_TESTING=OFF \
    && cmake --build build -j$(nproc) \
    && cmake --install build \
    && cd /build && rm -rf amio

# Clean up build directory
RUN rm -rf /build

# ---------------------------------------------------------------------------
# Stage 2: Build TIDE with tests enabled
# ---------------------------------------------------------------------------
FROM ubuntu:24.04 AS build

ENV DEBIAN_FRONTEND=noninteractive
ENV INSTALL_PREFIX=/opt/tide-deps
ENV PATH="${INSTALL_PREFIX}/bin:${PATH}"
ENV LD_LIBRARY_PATH="${INSTALL_PREFIX}/lib:${INSTALL_PREFIX}/lib64"
ENV CMAKE_PREFIX_PATH="${INSTALL_PREFIX}"
ENV CC=gcc-14
ENV CXX=g++-14
ENV FC=gfortran-14

# Install runtime dependencies and build tools
RUN apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    gfortran-14 \
    g++-14 \
    gcc-14 \
    cmake \
    ninja-build \
    git \
    libopenmpi-dev \
    openmpi-bin \
    libhdf5-mpi-dev \
    libnetcdf-dev \
    libnetcdf-mpi-dev \
    zlib1g \
    libcurl4t64 \
    libqhull-dev \
    ca-certificates \
    python3 \
    python3-netcdf4 \
    python3-numpy \
    && rm -rf /var/lib/apt/lists/* \
    && update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-14 100 \
    && update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-14 100 \
    && update-alternatives --install /usr/bin/gfortran gfortran /usr/bin/gfortran-14 100

# Copy pre-built dependencies from Stage 1
COPY --from=deps ${INSTALL_PREFIX} ${INSTALL_PREFIX}

# Copy TIDE source code
WORKDIR /tide
COPY . .

# Generate synthetic test data (NetCDF files for integration tests)
RUN python3 tests/data/generate_test_data.py tests/data/

# Configure and build TIDE with tests enabled
# Install kokkos/mdspan reference implementation (provides <experimental/mdspan> for compilers without native <mdspan>)
RUN git clone --depth 1 --branch mdspan-0.6.0 https://github.com/kokkos/mdspan.git /tmp/mdspan \
    && cmake -S /tmp/mdspan -B /tmp/mdspan-build -G Ninja \
        -DCMAKE_INSTALL_PREFIX=/usr/local \
        -DMDSPAN_ENABLE_TESTS=OFF \
        -DMDSPAN_ENABLE_BENCHMARKS=OFF \
    && cmake --install /tmp/mdspan-build \
    && rm -rf /tmp/mdspan /tmp/mdspan-build

RUN cmake -S . -B build -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="${INSTALL_PREFIX};/usr/local" \
    -DCMAKE_C_COMPILER=gcc-14 \
    -DCMAKE_CXX_COMPILER=g++-14 \
    -DCMAKE_Fortran_COMPILER=gfortran-14 \
    -DTIDE_BUILD_TESTS=ON \
    -DTIDE_BUILD_EXAMPLES=ON \
    && cmake --build build -j$(nproc)

# Create non-root user for security
RUN useradd -m -s /bin/bash tideuser && chown -R tideuser:tideuser /tide/build
USER tideuser

# ---------------------------------------------------------------------------
# Entrypoint: run the test suite and report pass/fail via exit code
# ---------------------------------------------------------------------------
ENTRYPOINT ["ctest", "--test-dir", "/tide/build", "--output-on-failure", "--timeout", "300"]
