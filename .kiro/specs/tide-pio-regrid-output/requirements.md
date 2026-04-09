# Requirements Document

## Introduction

This feature adds output capabilities to the TIDE library, enabling it to write interpolated and regridded data to NetCDF files via PIO (Parallel I/O). It also introduces ESMF-based regridding to transform data from the host model's mesh (which may be unstructured or tiled) onto a user-provided rectilinear or curvilinear target grid. The output configuration is driven by YAML, extending the existing yaml-cpp/Fortran interface pattern. Additionally, the repository is restructured into a formal Fortran project layout with a top-level CMakeLists.txt and a project-specific README.

## Glossary

- **TIDE**: Time-Interpolated Data Engine — the library providing the high-level API (tide_mod.F90) for reading, interpolating, and now writing geophysical data streams.
- **PIO**: Parallel I/O library used for scalable NetCDF file access across MPI ranks.
- **ESMF**: Earth System Modeling Framework — provides mesh, field, clock, and regridding abstractions.
- **Regrid_Manager**: The module responsible for creating and applying ESMF RouteHandles to remap fields from a source mesh to a target grid.
- **Output_Manager**: The module responsible for creating, defining, and writing NetCDF output files via PIO.
- **YAML_Parser**: The C++/Fortran interface (tide_yaml_c.cpp / tide_yaml_mod.F90) that parses YAML configuration files into bind(c) structs.
- **Target_Grid**: A user-provided ESMF Grid or Mesh describing the desired output coordinate system (e.g., a rectilinear lat-lon grid read from a SCRIP or GRIDSPEC file).
- **Source_Mesh**: The ESMF Mesh of the host model from which data originates.
- **RouteHandle**: An ESMF object that encodes the regridding weights between a source and destination field pair.
- **CF_Conventions**: Climate and Forecast metadata conventions for NetCDF files.
- **SCRIP**: Spherical Coordinate Remapping and Interpolation Package grid description format.
- **GRIDSPEC**: A CF-compliant grid specification format for describing structured grids.
- **Build_System**: The CMake-based build infrastructure for the project.
- **Stream**: A named collection of input files, field mappings, and temporal interpolation settings managed by TIDE.
- **Output_Stream**: A named collection of output settings (target grid, file path, frequency, fields) associated with a TIDE stream.

## Requirements

### Requirement 1: Repository Restructuring

**User Story:** As a developer, I want the repository to follow a standard Fortran project layout with a top-level CMakeLists.txt and a meaningful README, so that the project is easy to build, navigate, and contribute to.

#### Acceptance Criteria

1. THE Build_System SHALL provide a top-level CMakeLists.txt that discovers ESMF, PIO, and yaml-cpp dependencies and builds all sub-libraries (cdeps_share, streams, dshr, tide) in correct dependency order.
2. THE Build_System SHALL produce a single installable TIDE library target that transitively links all required sub-libraries.
3. THE Build_System SHALL support an option to build tests via a BUILD_TESTING CMake variable.
4. WHEN a developer runs `cmake --build .` from the build directory, THE Build_System SHALL compile all source files without errors given that ESMF, PIO, and yaml-cpp are available.
5. THE Build_System SHALL install Fortran module files and library archives to standard CMAKE_INSTALL_PREFIX subdirectories (lib, include).
6. THE Build_System SHALL provide a project-specific README.md that documents the project purpose, build prerequisites, build instructions, and usage overview.

### Requirement 2: YAML Output Configuration Parsing

**User Story:** As a model developer, I want to specify output settings in the same YAML configuration file used for input streams, so that I have a single configuration source for TIDE.

#### Acceptance Criteria

1. WHEN a YAML configuration file contains an `output` block within a stream definition, THE YAML_Parser SHALL parse the output settings into a tide_output_config_t structure.
2. THE YAML_Parser SHALL parse the following output fields: target_grid_file (string), output_file (string), output_frequency (integer, in seconds), regrid_method (string), and output_fields (list of field name strings).
3. WHEN the `output` block is absent from a stream definition, THE YAML_Parser SHALL set a flag indicating that output is disabled for that stream.
4. IF the `output` block is present but target_grid_file is missing or empty, THEN THE YAML_Parser SHALL return an error and log a descriptive message.
5. WHEN the `output` block specifies a regrid_method, THE YAML_Parser SHALL accept the values "bilinear", "neareststod", "nearestdtos", and "conserve".
6. IF an unrecognized regrid_method value is provided, THEN THE YAML_Parser SHALL return an error and log the invalid value.
7. THE YAML_Pretty_Printer SHALL format tide_output_config_t structures back into valid YAML output blocks.
8. FOR ALL valid tide_output_config_t structures, parsing then printing then parsing SHALL produce an equivalent structure (round-trip property).

### Requirement 3: Target Grid Loading

**User Story:** As a model developer, I want TIDE to load a target grid from a standard grid description file, so that I can regrid output to any supported grid format.

#### Acceptance Criteria

1. WHEN a target_grid_file path is provided in the output configuration, THE Regrid_Manager SHALL read the file and create an ESMF Grid or Mesh object representing the Target_Grid.
2. THE Regrid_Manager SHALL support SCRIP-format grid description files.
3. THE Regrid_Manager SHALL support GRIDSPEC-format (CF-compliant) grid description files.
4. IF the target_grid_file does not exist or cannot be read, THEN THE Regrid_Manager SHALL return an error code and log a descriptive message including the file path.
5. IF the target_grid_file contains an unrecognized format, THEN THE Regrid_Manager SHALL return an error code and log a descriptive message.
6. WHEN the Target_Grid is successfully loaded, THE Regrid_Manager SHALL log the grid dimensions and coordinate range at info log level.

### Requirement 4: ESMF Regridding

**User Story:** As a model developer, I want TIDE to regrid interpolated data from the model mesh to the target grid using ESMF, so that output is on a regular or user-defined grid suitable for analysis and visualization.

#### Acceptance Criteria

1. WHEN output is enabled for a stream, THE Regrid_Manager SHALL create an ESMF RouteHandle mapping from the Source_Mesh to the Target_Grid using the configured regrid_method.
2. THE Regrid_Manager SHALL support the regrid methods: bilinear, nearest-source-to-destination, nearest-destination-to-source, and first-order conservative.
3. WHEN tide_advance completes for a stream with output enabled, THE Regrid_Manager SHALL apply the RouteHandle to regrid each output field from the Source_Mesh to the Target_Grid.
4. IF RouteHandle creation fails due to incompatible source and target geometries, THEN THE Regrid_Manager SHALL return an error code and log a descriptive message.
5. WHEN tide_finalize is called, THE Regrid_Manager SHALL release all RouteHandle objects and associated ESMF resources.
6. THE Regrid_Manager SHALL create the RouteHandle once during initialization and reuse it for all subsequent regrid operations within the same stream.

### Requirement 5: PIO NetCDF Output

**User Story:** As a model developer, I want TIDE to write regridded data to NetCDF files via PIO, so that output is produced efficiently in parallel and follows CF conventions.

#### Acceptance Criteria

1. WHEN output is enabled for a stream, THE Output_Manager SHALL create a NetCDF output file at the path specified by output_file using PIO.
2. THE Output_Manager SHALL define dimensions in the output file matching the Target_Grid coordinate structure (e.g., lat, lon, and time as unlimited dimension).
3. THE Output_Manager SHALL define a NetCDF variable for each field listed in output_fields, with dimensions matching the Target_Grid plus the time dimension.
4. THE Output_Manager SHALL write CF-compliant global attributes including Conventions, history, and source.
5. THE Output_Manager SHALL write coordinate variables (latitude, longitude) with appropriate CF attributes (units, standard_name, axis).
6. WHEN the output_frequency interval elapses relative to the model clock, THE Output_Manager SHALL write the current regridded field data to the output file as a new time record.
7. WHEN tide_finalize is called, THE Output_Manager SHALL flush all pending writes and close the output file via PIO.
8. IF the output file cannot be created or written to, THEN THE Output_Manager SHALL return an error code and log a descriptive message including the file path.

### Requirement 6: TIDE API Integration

**User Story:** As a model developer, I want the output and regridding capabilities integrated into the existing TIDE API (tide_init, tide_advance, tide_finalize), so that enabling output requires only a YAML configuration change with no code changes in the host model.

#### Acceptance Criteria

1. WHEN tide_init is called with a YAML configuration containing output blocks, THE TIDE SHALL initialize the Regrid_Manager and Output_Manager for each stream that has output enabled.
2. WHEN tide_advance is called and the output_frequency interval has elapsed, THE TIDE SHALL regrid the current interpolated fields and write them to the output file.
3. WHEN tide_advance is called and the output_frequency interval has not elapsed, THE TIDE SHALL skip the regrid and write operations for that time step.
4. WHEN tide_finalize is called, THE TIDE SHALL finalize all Output_Manager and Regrid_Manager resources before releasing stream data.
5. WHEN output is not configured for any stream, THE TIDE SHALL behave identically to the current implementation with no performance overhead.
6. THE TIDE SHALL store output state (RouteHandle, PIO file descriptor, time record counter) within the tide_type derived type so that multiple TIDE instances operate independently.

### Requirement 7: Output Field Selection and Validation

**User Story:** As a model developer, I want to select which fields are written to the output file and have TIDE validate that those fields exist, so that I get clear errors for misconfigured output.

#### Acceptance Criteria

1. WHEN output_fields lists field names, THE Output_Manager SHALL write only those fields to the output file.
2. WHEN output_fields is empty or absent, THE Output_Manager SHALL write all fields defined in the stream's field_maps to the output file.
3. IF an output_fields entry does not match any field in the stream's field_maps, THEN THE TIDE SHALL return an error code during initialization and log the unrecognized field name.
4. THE Output_Manager SHALL preserve the field ordering specified in output_fields when defining NetCDF variables.
