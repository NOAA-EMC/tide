# Core Modules

The core modules implement the primary functionality of the TIDE library, including the
high-level API, YAML configuration parsing, CF convention detection, ESMF-based regridding,
and PIO-based NetCDF output.

## tide_mod

High-level API for the TIDE library. Provides the main entry points (`tide_init`,
`tide_advance`, `tide_finalize`) that orchestrate stream data initialization, temporal
interpolation, regridding, and output.

::: doxy.tide.Class
    name: tide_mod

## tide_yaml_mod

Fortran interface to the TIDE C++ YAML parser. Defines the `tide_config_t`,
`tide_stream_config_t`, and `tide_output_config_t` derived types that map to C structs,
along with routines for parsing YAML configuration files and freeing allocated memory.

::: doxy.tide.Class
    name: tide_yaml_mod

## tide_cf_detection_mod

CF Convention Auto-Detection module. Provides types, error constants, and routines for
detecting and interpreting CF (Climate and Forecast) convention metadata in NetCDF files,
including standard name matching, unit comparison, and field mapping.

::: doxy.tide.Class
    name: tide_cf_detection_mod

## tide_regrid_mod

Regrid Manager module. Provides target grid loading (SCRIP/GRIDSPEC), ESMF RouteHandle
creation, field regridding, and resource cleanup for mapping data from source meshes to
target grids.

::: doxy.tide.Class
    name: tide_regrid_mod

## tide_output_mod

Output Manager module. Provides PIO-based NetCDF output including file creation with
CF-compliant metadata, time-record writing, output frequency gating, and resource cleanup.

::: doxy.tide.Class
    name: tide_output_mod
