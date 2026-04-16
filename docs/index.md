# TIDE — Time-Interpolated Data Engine

TIDE is a Fortran library for reading, interpolating, regridding, and writing geophysical data streams. Designed for Earth system modeling workflows, TIDE provides a streamlined interface for managing input data streams — from reading raw files through temporal interpolation to producing CF-compliant NetCDF output on target grids.

## Key Features

- **YAML-Driven Configuration** — Define data streams, field mappings, and output settings in a simple YAML file. No code changes needed to add or modify streams.
- **ESMF Regridding** — Leverage the [Earth System Modeling Framework (ESMF)](https://earthsystemmodeling.org/) for high-quality regridding between source and target grids, with support for bilinear and other interpolation methods.
- **PIO NetCDF Output** — Write regridded results to CF-compliant NetCDF files using the [Parallel I/O (PIO)](https://ncar.github.io/ParallelIO/) library, suitable for large-scale parallel workflows.
- **Temporal Interpolation** — Automatically interpolate input data to the model's current timestep, with configurable interpolation modes per stream.

## How It Works

TIDE is configured via a YAML file that defines one or more data streams. Each stream specifies input files, field mappings, and temporal interpolation settings. Streams may optionally include an `output` block to enable regridding onto a target grid and writing results to NetCDF files.

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

The host model calls three routines to drive TIDE:

1. `tide_init` — Initialize streams and read configuration
2. `tide_advance` — Advance streams to the current model time (interpolation, regridding, and output happen automatically)
3. `tide_finalize` — Clean up resources

When output is configured, TIDE handles regridding and file writing automatically — no host model code changes required.

## Getting Started

New to TIDE? Start here:

- [Installation](getting-started/installation.md) — Prerequisites and dependency setup
- [Building](getting-started/building.md) — CMake build instructions
- [Quickstart](getting-started/quickstart.md) — A minimal example to get up and running

## Learn More

- [User Guide](user-guide/yaml-configuration.md) — Detailed documentation on YAML configuration, data streams, regridding, and output
- [API Reference](api/index.md) — Auto-generated documentation for all Fortran modules and C++ components
- [Developer Guide](developer-guide/contributing.md) — Contributing guidelines and build system documentation

## License

TIDE is part of the NOAA-EMC Ecosystem. See [LICENSE](https://github.com/NOAA-EMC/TIDE/blob/main/LICENSE) and [DISCLAIMER](https://github.com/NOAA-EMC/TIDE/blob/main/DISCLAIMER) for details.
