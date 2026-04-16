# API Reference

The TIDE (Time-Interpolated Data Engine) API is organized into five layers, each responsible for a distinct part of the data processing pipeline. This page provides an architectural overview and links to the detailed, auto-generated documentation for every module.

## Architecture Overview

```text
┌─────────────────────────────────────────────────┐
│                  Core Modules                    │
│  tide_mod · tide_yaml_mod · tide_cf_detection_mod│
│  tide_regrid_mod · tide_output_mod               │
├──────────────┬──────────────────┬───────────────┤
│ Data Stream  │  Stream Modules  │    C++ Bridge │
│  Handlers    │                  │               │
│ dshr_mod     │ dshr_strdata_mod │ tide_yaml_c   │
│ dshr_dfield  │ dshr_stream_mod  │  (YAML parse) │
│ dshr_fldlist │ dshr_tinterp_mod │               │
│              │ dshr_methods_mod │               │
├──────────────┴──────────────────┴───────────────┤
│              Shared Utilities                    │
│  shr_kind_mod · shr_cal_mod · shr_const_mod     │
│  shr_string_mod                                  │
└─────────────────────────────────────────────────┘
```

## Core Modules

The top-level Fortran modules in `src/` implement the main TIDE workflow — initialization, time-stepping, and finalization.

| Module | Purpose |
| --- | --- |
| `tide_mod` | Public entry points: `tide_init`, `tide_advance`, `tide_finalize`. Orchestrates the full data-engine lifecycle. |
| `tide_yaml_mod` | Reads and validates the YAML stream configuration file, producing internal data structures consumed by other modules. |
| `tide_cf_detection_mod` | Detects CF-convention metadata (standard names, units, coordinates) in input NetCDF fields and maps them to model fields. |
| `tide_regrid_mod` | Wraps ESMF regridding to interpolate source-grid data onto the target model grid. |
| `tide_output_mod` | Writes CF-compliant NetCDF output files at the configured frequency using PIO. |

→ [Core Modules reference](core-modules.md)

## Data Stream Handlers

Located in `src/dshr/`, these modules manage the association between external data files and the fields requested by the model.

| Module | Purpose |
| --- | --- |
| `dshr_mod` | Top-level data-stream handler; coordinates field reads and time management across all active streams. |
| `dshr_dfield_mod` | Defines the `dfield` derived type representing a single data field, including its metadata and storage. |
| `dshr_fldlist_mod` | Maintains ordered lists of requested fields and provides lookup and matching utilities. |

→ [Data Stream Handlers reference](data-stream-handlers.md)

## Stream Modules

Located in `src/streams/`, these modules handle the low-level mechanics of reading, interpolating, and delivering stream data.

| Module | Purpose |
| --- | --- |
| `dshr_strdata_mod` | Manages stream-data objects that bundle file paths, field lists, and interpolation state for each configured stream. |
| `dshr_stream_mod` | Opens and reads individual NetCDF stream files, populating field arrays on the source grid. |
| `dshr_tinterp_mod` | Performs temporal interpolation (e.g., linear, lower-bound, upper-bound) between time levels read from stream files. |
| `dshr_methods_mod` | Provides shared helper routines used across the stream layer (state creation, field access, logging). |

→ [Stream Modules reference](stream-modules.md)

## C++ Components

A thin C++ layer bridges Fortran and the yaml-cpp library.

| File | Purpose |
| --- | --- |
| `tide_yaml_c.cpp` | Exposes C-linkage functions that `tide_yaml_mod` calls via `ISO_C_BINDING` to parse YAML configuration files using yaml-cpp. |

→ [C++ Components reference](cpp-components.md)

## Shared Utilities

Located in `src/share/`, these modules supply fundamental types, constants, and string-handling routines used throughout the codebase.

| Module | Purpose |
| --- | --- |
| `shr_kind_mod` | Defines Fortran kind parameters (`r8`, `i4`, `CS`, `CL`, etc.) used for portable numeric and string declarations. |
| `shr_cal_mod` | Calendar and date-conversion utilities for translating between model time representations. |
| `shr_const_mod` | Physical and mathematical constants (π, Earth radius, etc.) shared across modules. |
| `shr_string_mod` | String manipulation helpers: case conversion, tokenization, whitespace trimming. |

→ [Shared Utilities reference](shared-utilities.md)
