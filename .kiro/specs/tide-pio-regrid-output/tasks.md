# Implementation Plan: TIDE PIO Regrid Output

## Overview

This plan implements output, regridding, and repository restructuring for the TIDE library. Tasks proceed in dependency order: build system first, then YAML parsing extensions, regrid manager, output manager, API integration, and field validation. Each task builds on the previous, ending with full wiring and integration tests.

## Tasks

- [x] 1. Repository restructuring and build system
  - [x] 1.1 Create top-level CMakeLists.txt
    - Set `cmake_minimum_required(VERSION 3.18)` and `project(TIDE Fortran C CXX)`
    - Include `cmake/FindESMF.cmake` and `cmake/FindPIO.cmake`
    - Find yaml-cpp via `find_package(yaml-cpp REQUIRED)`
    - Add subdirectories in dependency order: `share`, `streams`, `dshr`, `tide`
    - Define `BUILD_TESTING` option; when ON, add `tide/tests` as a subdirectory
    - Provide `install()` targets for libraries and Fortran `.mod` files
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

  - [x] 1.2 Create tide/CMakeLists.txt
    - Compile `tide_yaml_c.cpp` (C++) and all `.F90` sources in `tide/src/`
    - Link against `dshr`, `streams`, `cdeps_share`, ESMF, PIO, and yaml-cpp
    - Install the `tide` library target and Fortran `.mod` files
    - _Requirements: 1.1, 1.2, 1.5_

  - [x] 1.3 Update project README.md
    - Document project purpose, build prerequisites (ESMF, PIO, yaml-cpp), build instructions, and usage overview
    - _Requirements: 1.6_

- [x] 2. YAML output configuration parsing
  - [x] 2.1 Extend tide_yaml_c.cpp with tide_output_config_t and output block parsing
    - Add `tide_output_config_t` C struct with fields: `target_grid_file`, `output_file`, `output_frequency`, `regrid_method`, `output_fields`, `num_output_fields`, `output_enabled`
    - Add `tide_output_config_t` as a member of `tide_stream_config_t`
    - Parse `output` sub-block within each stream; set `output_enabled=0` when absent
    - Validate: error if `output` present but `target_grid_file` missing/empty
    - Validate: error if `regrid_method` not in {"bilinear","neareststod","nearestdtos","conserve"}
    - Default `regrid_method` to "bilinear" if absent; default `output_frequency` to 3600 if absent
    - Set `num_output_fields=0` when `output_fields` absent (meaning write all fields)
    - Add `tide_output_config_to_yaml` pretty-printer function exposed via `extern "C"`
    - Update `tide_free_config` to free output config memory
    - _Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7_

  - [x] 2.2 Extend tide_yaml_mod.F90 with Fortran bind(c) type for tide_output_config_t
    - Add `tide_output_config_t` bind(c) type mirroring the C struct
    - Add it as a member of `tide_stream_config_t`
    - _Requirements: 2.1, 2.2_

  - [x] 2.3 Write property test for YAML output config round-trip (Property 1)
    - **Property 1: YAML Output Configuration Round-Trip**
    - Generate random valid `tide_output_config_t` structures (non-empty target_grid_file, valid regrid_method, positive output_frequency, non-empty output_file, 0+ output_fields)
    - Pretty-print to YAML string, parse back, verify equivalence
    - Minimum 100 iterations
    - **Validates: Requirements 2.1, 2.2, 2.7, 2.8**

  - [x] 2.4 Write property test for invalid regrid method rejection (Property 2)
    - **Property 2: Invalid Regrid Method Rejection**
    - Generate random strings not in {"bilinear","neareststod","nearestdtos","conserve"}
    - Verify parser returns error (null pointer / non-zero return code)
    - Minimum 100 iterations
    - **Validates: Requirements 2.6**

  - [x] 2.5 Write unit tests for YAML output parsing edge cases
    - Test: parse stream without `output` block → `output_enabled == 0`
    - Test: parse with `output` but no `target_grid_file` → error
    - Test: parse with each valid `regrid_method` → success
    - Test: parse with empty `output_fields` → `num_output_fields == 0`
    - _Requirements: 2.3, 2.4, 2.5, 2.6_

- [x] 3. Checkpoint - Verify build and YAML parsing
  - Ensure all tests pass, ask the user if questions arise.

- [x] 4. Regrid Manager implementation
  - [x] 4.1 Create tide_regrid_mod.F90 with types and public interface
    - Define `tide_regrid_state_t` type (target_grid, route_handle, src_field, dst_field, initialized flag)
    - Implement `tide_regrid_init`: load target grid from SCRIP or GRIDSPEC file, create ESMF RouteHandle
    - Map regrid_method string to ESMF constants (bilinear, neareststod, nearestdtos, conserve)
    - Attempt SCRIP format first, fall back to GRIDSPEC, return error if both fail
    - Log grid dimensions and coordinate range on success
    - _Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 4.1, 4.2, 4.4, 4.6_

  - [x] 4.2 Implement tide_regrid_apply and tide_regrid_finalize
    - `tide_regrid_apply`: apply precomputed RouteHandle via `ESMF_FieldRegrid` to regrid one field
    - `tide_regrid_finalize`: destroy RouteHandle, fields, and target grid ESMF objects
    - _Requirements: 4.3, 4.5_

  - [x] 4.3 Write unit tests for Regrid Manager error paths
    - Test: `tide_regrid_init` with nonexistent file → error
    - Test: `tide_regrid_init` with invalid format file → error
    - _Requirements: 3.4, 3.5_

- [x] 5. Output Manager implementation
  - [x] 5.1 Create tide_output_mod.F90 with types and public interface
    - Define `tide_output_field_t` and `tide_output_state_t` types
    - Implement `tide_output_init`: create NetCDF file via PIO, define dims (lat, lon, time unlimited), coordinate variables with CF attributes, data variables for each output field, global attributes (Conventions="CF-1.8", history, source)
    - _Requirements: 5.1, 5.2, 5.3, 5.4, 5.5_

  - [x] 5.2 Implement tide_output_write, tide_output_should_write, and tide_output_finalize
    - `tide_output_should_write`: track elapsed_seconds, return true when >= output_frequency, reset counter
    - `tide_output_write`: write regridded field data as new time record for all fields via PIO put_var
    - `tide_output_finalize`: sync and close PIO file
    - _Requirements: 5.6, 5.7, 5.8_

  - [x] 5.3 Write property test for output write frequency trigger (Property 3)
    - **Property 3: Output Write Frequency Trigger**
    - Generate random positive output_frequency and sequences of positive dt values
    - Verify `tide_output_should_write` returns true exactly when cumulative elapsed >= output_frequency, and resets correctly
    - Minimum 100 iterations
    - **Validates: Requirements 5.6**

  - [x] 5.4 Write unit tests for Output Manager error paths
    - Test: `tide_output_init` with invalid path → error
    - Test: `tide_output_finalize` flushes and closes file
    - _Requirements: 5.7, 5.8_

- [x] 6. Checkpoint - Verify regrid and output modules
  - Ensure all tests pass, ask the user if questions arise.

- [x] 7. Output field selection and validation
  - [x] 7.1 Implement field validation logic in tide_mod.F90
    - During `tide_init`, when `output_fields` is specified, validate each entry against the stream's `field_maps` model variable names
    - Return `ESMF_FAILURE` and log unrecognized field names
    - When `output_fields` is empty (`num_output_fields == 0`), use all fields from `field_maps`
    - Preserve field ordering from `output_fields` in NetCDF variable definitions
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

  - [x] 7.2 Write property test for field selection and ordering preservation (Property 4)
    - **Property 4: Field Selection and Ordering Preservation**
    - Generate random ordered subsets of field names from a mock stream's field_maps
    - Verify Output_Manager defines NetCDF variables for exactly those fields in exactly that order
    - Minimum 100 iterations
    - **Validates: Requirements 7.1, 7.4**

  - [x] 7.3 Write property test for invalid output field rejection (Property 5)
    - **Property 5: Invalid Output Field Rejection**
    - Generate random field name strings not in the stream's field_maps
    - Verify TIDE initialization returns error code and output file is not created
    - Minimum 100 iterations
    - **Validates: Requirements 7.3**

  - [x] 7.4 Write unit tests for field selection edge cases
    - Test: `output_fields` empty → all `field_maps` fields written
    - Test: `output_fields` with valid subset → only those fields written in order
    - Test: `output_fields` with invalid entry → error during init
    - _Requirements: 7.1, 7.2, 7.3, 7.4_

- [x] 8. TIDE API integration
  - [x] 8.1 Extend tide_type with regrid and output state
    - Add `regrid(:)`, `output(:)`, and `output_enabled(:)` arrays to `tide_type`
    - Allocate per-stream arrays in `tide_init`
    - _Requirements: 6.5, 6.6_

  - [x] 8.2 Integrate output initialization into tide_init
    - After parsing YAML and initializing streams, for each stream where `output_enabled == 1`:
      - Validate `output_fields` against stream's `field_maps` (calls logic from 7.1)
      - Call `tide_regrid_init` with source mesh, target grid file, and regrid method
      - Call `tide_output_init` with PIO subsystem, output file path, target grid, field names, and frequency
    - When no output blocks present, skip all output initialization (zero overhead)
    - _Requirements: 6.1, 6.5_

  - [x] 8.3 Integrate regrid and output writing into tide_advance
    - After `shr_strdata_advance`, for each output-enabled stream:
      - Check `tide_output_should_write` against timestep size
      - If true: for each output field, call `tide_regrid_apply` then `tide_output_write`
      - If false: skip (no performance overhead)
    - _Requirements: 6.2, 6.3_

  - [x] 8.4 Integrate cleanup into tide_finalize
    - Before deallocating `sdat`, for each output-enabled stream:
      - Call `tide_output_finalize`
      - Call `tide_regrid_finalize`
    - Deallocate regrid, output, and output_enabled arrays
    - _Requirements: 6.4_

  - [x] 8.5 Write unit tests for API integration
    - Test: `tide_init` without output blocks → identical to current behavior
    - Test: two independent `tide_type` instances with different output configs
    - _Requirements: 6.5, 6.6_

- [x] 9. Checkpoint - Verify full API integration
  - Ensure all tests pass, ask the user if questions arise.

- [x] 10. Integration tests
  - [x] 10.1 Write integration tests for end-to-end output pipeline
    - Test: full `tide_init` → `tide_advance` → `tide_finalize` with output enabled
    - Test: output file has correct dimensions, variables, and CF attributes
    - Test: advance clock < output_frequency → no write occurs
    - Test: load SCRIP target grid file → valid ESMF_Grid
    - Test: load GRIDSPEC target grid file → valid ESMF_Grid
    - Test: create RouteHandle for each regrid method
    - _Requirements: 3.1, 3.2, 3.3, 4.1, 4.2, 4.3, 5.2, 5.3, 5.4, 5.5, 6.1, 6.2, 6.3, 6.4_

  - [x] 10.2 Write build system integration tests
    - Test: top-level CMake configure and build succeeds
    - Test: CMake install produces lib/ and include/ artifacts
    - Test: BUILD_TESTING=ON adds test targets
    - _Requirements: 1.1, 1.2, 1.3, 1.4, 1.5_

- [x] 11. Final checkpoint - Ensure all tests pass
  - Ensure all tests pass, ask the user if questions arise.

## Notes

- Tasks marked with `*` are optional and can be skipped for faster MVP
- Each task references specific requirements for traceability
- Checkpoints ensure incremental validation
- Property tests validate universal correctness properties from the design document
- Unit tests validate specific examples and edge cases
- The codebase uses Fortran (F90) with C++ (yaml-cpp) via iso_c_binding — all code tasks target these languages
- Existing patterns in tide_yaml_c.cpp and tide_yaml_mod.F90 should be followed for consistency
