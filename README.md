# TIDE — Time-Interpolated Data Engine

TIDE is a Fortran library for reading, interpolating, regridding, and writing geophysical data streams. It provides a YAML-driven configuration interface for managing input data streams with optional ESMF-based regridding and PIO-based NetCDF output.

## Prerequisites

- CMake >= 3.18
- Fortran, C, and C++ compilers
- [ESMF](https://earthsystemmodeling.org/) — Earth System Modeling Framework
- [PIO](https://ncar.github.io/ParallelIO/) — Parallel I/O library
- [yaml-cpp](https://github.com/jbeder/yaml-cpp) — YAML parser for C++

## Building

```bash
mkdir build
cd build
cmake ..
make
```

To install to a custom prefix:

```bash
cmake .. -DCMAKE_INSTALL_PREFIX=/path/to/install
make install

```

To build with tests enabled:

```bash
cmake .. -DBUILD_TESTING=ON
make
ctest
```

## Usage

TIDE is configured via a YAML file that defines one or more data streams. Each stream specifies input files, field mappings, and temporal interpolation settings. Streams may optionally include an `output` block to enable regridding onto a target grid and writing results to CF-compliant NetCDF files.

```yaml
streams:
  - name: sst_stream
    # ... input stream fields ...
    output:
      target_grid_file: "/grids/target_1deg.nc"
      output_file: "/output/sst_regridded.nc"
      output_frequency: 3600
      regrid_method: "bilinear"
      output_fields:
        - "sst"
        - "ice_fraction"
```

The host model calls `tide_init`, `tide_advance`, and `tide_finalize`. When output is configured, TIDE handles regridding and file writing automatically — no host model code changes required.

## License

This project is part of the NOAA-EMC Ecosystem.

See LICENSE and DISCLAIMER for details.
