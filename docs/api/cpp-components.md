# C++ Components

The C++ component provides the YAML configuration bridge between the yaml-cpp library
and the Fortran-based TIDE runtime. It exposes C-linkage functions that the Fortran
`tide_yaml_mod` module calls via `iso_c_binding` to parse YAML stream configuration
files and manage the resulting configuration memory.

## tide_yaml_c

C++ implementation of the TIDE YAML parser using yaml-cpp. Defines three C-interoperable
structures (`tide_output_config_t`, `tide_stream_config_t`, `tide_config_t`) that mirror
the Fortran derived types in `tide_yaml_mod`. Provides `tide_parse_yaml` to load and
validate a YAML configuration file into a `tide_config_t` structure, `tide_free_config`
to release all allocated memory, and `tide_output_config_to_yaml` to serialize an output
configuration back to a YAML string.

### Structures

- `tide_output_config_t` — Configuration for output and regridding of a single stream,
  including target grid file, output file path, write frequency, regrid method, and
  optional output field list.
- `tide_stream_config_t` — Configuration for a single data stream, including name, mesh
  file, temporal interpolation mode, year range, input file list, field mappings, CF
  detection settings, and an embedded `tide_output_config_t`.
- `tide_config_t` — Top-level configuration containing an array of stream configurations
  and the stream count.

### Functions

- `tide_parse_yaml(filename)` — Parses a TIDE YAML configuration file and returns a
  pointer to a populated `tide_config_t`, or `nullptr` on failure. Validates required
  keys (`streams`, `name`, `target_grid_file` when output is present) and regrid method
  values. Handles `YAML::BadFile`, `YAML::ParserException`, and general exceptions with
  descriptive error messages.
- `tide_free_config(cfg)` — Frees all memory allocated by `tide_parse_yaml`, including
  nested string arrays and output field lists. Safe to call on partially initialized
  structures.
- `tide_output_config_to_yaml(oc)` — Serializes a `tide_output_config_t` to a YAML
  string using `YAML::Emitter`. Returns a newly allocated C string (caller must free)
  or `nullptr` if output is disabled.

::: doxy.tide.Class
    name: tide_yaml_c
