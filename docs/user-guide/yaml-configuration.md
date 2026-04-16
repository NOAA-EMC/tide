# YAML Configuration

TIDE uses a YAML configuration file to define data streams. Each stream specifies
input files, field mappings, temporal interpolation settings, and optional output
and regridding parameters. The configuration is parsed by the C++ YAML parser
(`tide_yaml_c.cpp`) using the [yaml-cpp](https://github.com/jbeder/yaml-cpp) library.

## File Structure

A TIDE configuration file contains a single top-level `streams` key holding a list
of stream definitions:

```yaml
streams:
  - name: my_stream
    # ... stream fields ...
  - name: another_stream
    # ... stream fields ...
```

## Stream Fields

Each stream entry supports the following fields:

### Required Fields

| Field | Type | Description |
| --- | --- | --- |
| `name` | string | Unique identifier for the stream. |
| `year_first` | integer | First year of the data time range. |
| `year_last` | integer | Last year of the data time range. |
| `year_align` | integer | Model year that aligns with `year_first` in the data. |
| `input_files` | list of strings | Paths to input NetCDF data files. |
| `field_maps` | list of objects | Mappings between file variables and model variables (see [Field Maps](#field-maps)). |

### Optional Fields

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `mesh_file` | string | `"none"` | Path to the mesh descriptor file for the input data. |
| `lev_dimname` | string | `"null"` | Name of the vertical level dimension in the input files. |
| `tax_mode` | string | `"cycle"` | Time axis mode. Controls how the time axis is interpreted when the model year range exceeds the data year range. |
| `time_interp` | string | `"linear"` | Temporal interpolation method applied between time steps. |
| `map_algo` | string | `"bilinear"` | Mapping algorithm used for horizontal regridding of input data. |
| `read_mode` | string | `"single"` | File reading mode. |
| `dt_limit` | float | `1.5` | Maximum allowable time extrapolation factor. |
| `offset` | integer | `0` | Time offset in seconds applied to the data time axis. |
| `cf_detection_mode` | string | `"auto"` | CF metadata detection mode. One of `"auto"`, `"strict"`, or `"disabled"`. |
| `cf_cache_enabled` | boolean | `true` | Enable caching of CF metadata lookups. |
| `cf_log_level` | integer | `2` | Verbosity level for CF detection logging (0–3). |

## Field Maps

The `field_maps` list defines how variables in the input files correspond to model
variables. Each entry is an object with two keys:

| Key | Type | Description |
| --- | --- | --- |
| `file_var` | string | Variable name as it appears in the input NetCDF file. |
| `model_var` | string | Corresponding variable name used by the model. |

```yaml
field_maps:
  - { file_var: "SST", model_var: "sea_surface_temperature" }
  - { file_var: "UWIND", model_var: "u_wind_10m" }
```

## Output Block

An optional `output` sub-block enables regridding and NetCDF output for a stream.
When the `output` block is present, `target_grid_file` is required.

| Field | Type | Default | Description |
| --- | --- | --- | --- |
| `target_grid_file` | string | *(required)* | Path to the target SCRIP or GRIDSPEC grid file for regridding. |
| `output_file` | string | `""` | Path to the output NetCDF file. |
| `output_frequency` | integer | `3600` | Write interval in seconds. |
| `regrid_method` | string | `"bilinear"` | Regridding method. Must be one of: `bilinear`, `neareststod`, `nearestdtos`, `conserve`. |
| `output_fields` | list of strings | *(all fields)* | Subset of fields to write. If omitted or empty, all mapped fields are written. |

```yaml
output:
  target_grid_file: "/grids/target_1deg.nc"
  output_file: "stream_output.nc"
  output_frequency: 7200
  regrid_method: "bilinear"
  output_fields:
    - "sea_surface_temperature"
    - "u_wind_10m"
```

## Complete Example

The following example is based on the test configuration used in the TIDE test suite
(`tests/test_config.yaml`):

```yaml
streams:
  - name: test_stream
    mesh_file: "test_mesh.nc"
    tax_mode: "cycle"
    time_interp: "linear"
    map_algo: "bilinear"
    year_first: 2000
    year_last: 2000
    year_align: 2000
    input_files:
      - "input_data.nc"
    field_maps:
      - { file_var: "data_var", model_var: "model_data" }
```

This minimal configuration defines a single stream named `test_stream` that:

- Reads from `input_data.nc` on the mesh described by `test_mesh.nc`
- Maps the file variable `data_var` to the model variable `model_data`
- Uses linear temporal interpolation with a cyclic time axis
- Covers the single year 2000

## Extended Example with Output

A more complete configuration that includes output and regridding:

```yaml
streams:
  - name: sst_forcing
    mesh_file: "ocean_mesh.nc"
    tax_mode: "cycle"
    time_interp: "linear"
    map_algo: "bilinear"
    read_mode: "single"
    dt_limit: 1.5
    year_first: 1990
    year_last: 2020
    year_align: 1990
    offset: 0
    cf_detection_mode: "auto"
    cf_cache_enabled: true
    cf_log_level: 2
    input_files:
      - "sst_monthly_1990_2000.nc"
      - "sst_monthly_2001_2010.nc"
      - "sst_monthly_2011_2020.nc"
    field_maps:
      - { file_var: "SST", model_var: "sea_surface_temperature" }
      - { file_var: "ICE_FRAC", model_var: "ice_fraction" }
    output:
      target_grid_file: "/grids/target_1deg.nc"
      output_file: "sst_regridded.nc"
      output_frequency: 3600
      regrid_method: "bilinear"
      output_fields:
        - "sea_surface_temperature"
        - "ice_fraction"
```

## Validation

The YAML parser performs the following validation at parse time:

- The top-level `streams` key must be present.
- Each stream must have a `name`, `year_first`, `year_last`, `year_align`, `input_files`, and `field_maps`.
- If an `output` block is present, `target_grid_file` must be non-empty.
- If `regrid_method` is specified, it must be one of: `bilinear`, `neareststod`, `nearestdtos`, `conserve`. Invalid values cause a parse error.
- Malformed YAML or missing required fields produce error messages prefixed with `ERROR: [TIDE]`.
