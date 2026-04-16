# Output

TIDE writes regridded field data to NetCDF files using
[PIO](https://ncar.github.io/ParallelIO/) (Parallel I/O). Output is configured
per stream through the `output` block in the YAML configuration. When output is
enabled, TIDE creates a CF-compliant NetCDF file, gates writes by a configurable
frequency interval, and appends a new time record at each write step.

The output implementation lives in the `tide_output_mod` module
(`src/tide_output_mod.F90`).

## How It Works

The output pipeline follows the same three-phase lifecycle as the rest of TIDE:

1. **Initialization** (`tide_output_init`) — TIDE creates the NetCDF file via
   PIO, defines dimensions and coordinate variables with CF-compliant metadata,
   defines a data variable for each output field, writes global attributes, and
   populates the coordinate arrays from the target grid.
2. **Write** (`tide_output_write`) — At each model time step, TIDE checks
   whether the output frequency interval has elapsed. If so, it increments the
   time record counter, writes the model time to the time coordinate, and writes
   each field's 2-D data slice for the current record.
3. **Finalization** (`tide_output_finalize`) — Pending writes are flushed with
   `pio_syncfile` and the NetCDF file is closed.

## Output File

The `output_file` key specifies the path to the NetCDF file that TIDE will
create. If the file already exists it is overwritten (`PIO_CLOBBER` mode).

```yaml
output:
  output_file: "stream_output.nc"
```

The file is created during stream initialization and remains open for the
duration of the run. TIDE logs the file path and grid dimensions on creation:

```text
INFO: [TIDE] Output file created: stream_output.nc
INFO: [TIDE] Output dimensions: lon= 360  lat= 180
INFO: [TIDE] Output fields: 2  frequency= 3600 s
```

## Output Frequency

The `output_frequency` key controls how often data is written, specified in
**seconds**. TIDE accumulates elapsed model time internally and triggers a write
whenever the accumulated time reaches or exceeds the configured interval.

```yaml
output:
  output_frequency: 7200   # write every 2 hours
```

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `output_frequency` | integer | `3600` | Write interval in seconds. |

The frequency gating logic (`tide_output_should_write`) works as follows:

1. Each model time step adds its `dt_seconds` to an internal counter
   (`elapsed_seconds`).
2. When `elapsed_seconds >= output_frequency`, a write is triggered and the
   counter is decremented by `output_frequency` (preserving any remainder for
   the next cycle).
3. This approach ensures that writes stay aligned with the requested interval
   even when the model time step does not divide evenly into the frequency.

## Output Fields

The `output_fields` key selects which model fields are written to the output
file. If omitted or empty, all fields from the stream's `field_maps` are
included.

```yaml
output:
  output_fields:
    - "sea_surface_temperature"
    - "ice_fraction"
```

Each listed name must match a `model_var` from the stream's `field_maps`. TIDE
defines one NetCDF variable per output field with dimensions `(lon, lat, time)`.

## CF-Compliant NetCDF Output

TIDE produces output files that conform to the
[CF Conventions](https://cfconventions.org/) (version 1.8). This ensures that
output files are self-describing and interoperable with standard analysis tools
such as CDO, NCO, xarray, and ncview.

### Global Attributes

Every output file includes the following global attributes:

| Attribute | Value | Description |
| --- | --- | --- |
| `Conventions` | `CF-1.8` | Declares CF convention version. |
| `history` | `Created by TIDE` | Provenance record. |
| `source` | `TIDE` | Identifies the producing software. |

### Dimensions

The file defines three dimensions:

| Dimension | Size | Description |
| --- | --- | --- |
| `lon` | Grid-dependent | Number of longitude points on the target grid. |
| `lat` | Grid-dependent | Number of latitude points on the target grid. |
| `time` | Unlimited | Grows with each write record. |

### Coordinate Variables

Coordinate variables carry CF-required attributes so that tools can
automatically identify axes and units:

#### `lon(lon)` — Longitude

| Attribute | Value |
| --- | --- |
| `units` | `degrees_east` |
| `standard_name` | `longitude` |
| `axis` | `X` |

#### `lat(lat)` — Latitude

| Attribute | Value |
| --- | --- |
| `units` | `degrees_north` |
| `standard_name` | `latitude` |
| `axis` | `Y` |

#### `time(time)` — Time

| Attribute | Value |
| --- | --- |
| `units` | `seconds since 0001-01-01 00:00:00` |
| `calendar` | `noleap` |
| `axis` | `T` |

The time coordinate stores model time values in seconds. Each call to
`tide_output_write` appends one value to this variable.

### Data Variables

Each output field is stored as a double-precision variable with shape
`(lon, lat, time)`. Data variables include:

| Attribute | Value | Description |
| --- | --- | --- |
| `_FillValue` | `1.0e30` (`shr_const_spval`) | Missing-data sentinel. |
| `coordinates` | `lon lat` | Auxiliary coordinate reference. |

## Configuration Reference

All output options live inside the `output` block of a stream definition. The
`target_grid_file` key is required whenever an `output` block is present (see
[Regridding](regridding.md) for details on the target grid).

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `target_grid_file` | string | *(required)* | Path to the SCRIP or GRIDSPEC target grid file. |
| `output_file` | string | `""` | Path to the output NetCDF file. |
| `output_frequency` | integer | `3600` | Write interval in seconds. |
| `regrid_method` | string | `"bilinear"` | Regridding method (see [Regridding](regridding.md)). |
| `output_fields` | list | *(all fields)* | Subset of model fields to write. If omitted, all mapped fields are included. |

## Example

A stream that writes regridded SST and ice fraction every two hours:

```yaml
streams:
  - name: sst_forcing
    mesh_file: "ocean_mesh.nc"
    year_first: 1990
    year_last: 2020
    year_align: 1990
    input_files:
      - "sst_monthly.nc"
    field_maps:
      - { file_var: "SST", model_var: "sea_surface_temperature" }
      - { file_var: "ICE_FRAC", model_var: "ice_fraction" }
    output:
      target_grid_file: "/grids/target_1deg.nc"
      output_file: "sst_regridded.nc"
      output_frequency: 7200
      regrid_method: "bilinear"
      output_fields:
        - "sea_surface_temperature"
        - "ice_fraction"
```

The resulting `sst_regridded.nc` file will contain:

- Dimensions `lon`, `lat`, and an unlimited `time` dimension.
- Coordinate variables `lon`, `lat`, and `time` with CF attributes.
- Data variables `sea_surface_temperature(lon, lat, time)` and
  `ice_fraction(lon, lat, time)`.
- A new time record appended every 7200 seconds of model time.

## Inspecting Output Files

Because TIDE output is CF-compliant, standard NetCDF tools work out of the box:

```bash
# View file structure
ncdump -h sst_regridded.nc

# Quick field summary with CDO
cdo infon sst_regridded.nc

# Load in Python with xarray
python -c "import xarray as xr; print(xr.open_dataset('sst_regridded.nc'))"
```

## Troubleshooting

### Output file is not created

If TIDE reports `ERROR: [TIDE] Failed to create output file`, verify that:

- The output directory exists and is writable by all MPI ranks.
- The file path does not contain invalid characters.
- PIO is correctly initialized in the calling application.

### Write called on uninitialized state

The error `ERROR: [TIDE] tide_output_write called on uninitialised state` means
that `tide_output_init` was not called (or failed) before the first write
attempt. Check the initialization log messages for earlier errors.

### Missing fields in output

If expected variables are absent from the output file, confirm that the field
names listed in `output_fields` exactly match the `model_var` names in the
stream's `field_maps`.

## See Also

- [YAML Configuration](yaml-configuration.md) — full schema reference including
  the `output` block.
- [Regridding](regridding.md) — target grid file formats and regrid methods.
- [Data Streams](data-streams.md) — stream lifecycle and field mappings.
