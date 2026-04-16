# Regridding

TIDE uses the [ESMF](https://earthsystemmodeling.org/) regridding infrastructure
to interpolate field data from a source mesh onto a target grid. Regridding is
configured per stream through the `output` block in the YAML configuration. When
an output block is present, TIDE loads the target grid, precomputes a route
handle for the chosen interpolation method, and applies it to every field at
each output step.

The regridding implementation lives in the `tide_regrid_mod` module
(`src/tide_regrid_mod.F90`).

## How It Works

The regrid pipeline has three phases that mirror the stream lifecycle:

1. **Initialization** (`tide_regrid_init`) — TIDE loads the target grid from a
   file, creates scratch source and destination ESMF fields, and precomputes an
   `ESMF_RouteHandle` that encodes the interpolation weights between the source
   mesh and the target grid.
2. **Apply** (`tide_regrid_apply`) — On each output step, source data is copied
   into the scratch source field, `ESMF_FieldRegrid` applies the precomputed
   route handle, and the result is extracted from the destination field.
3. **Finalization** (`tide_regrid_finalize`) — The route handle, scratch fields,
   and target grid are destroyed to release ESMF resources.

Because the route handle is computed once during initialization, the per-step
cost of regridding is limited to the sparse matrix multiply — there is no
repeated weight calculation.

## Target Grid File

The `target_grid_file` key in the `output` block specifies the path to a NetCDF
file that describes the destination grid. TIDE attempts to load the file in two
formats, in order:

| Format | Description |
| --- | --- |
| **SCRIP** | The standard Spherical Coordinate Remapping and Interpolation Package format. TIDE tries this first. |
| **GRIDSPEC** | A CF-style grid specification format. Used as a fallback if SCRIP parsing fails. |

Corner stagger coordinates are added automatically (`addCornerStagger=.true.`)
so that conservative regridding methods have the cell boundary information they
need.

```yaml
output:
  target_grid_file: "/grids/target_1deg.nc"
```

After loading, TIDE logs the grid dimensions and coordinate range to help verify
that the correct grid was picked up:

```text
INFO: [TIDE] Loaded target grid from SCRIP file: /grids/target_1deg.nc
INFO: [TIDE] Target grid: dimCount=2  tileCount=1
INFO: [TIDE] Target grid size: 360 x 180
INFO: [TIDE] Coordinate range: lon=[0.5, 359.5] lat=[-89.5, 89.5]
```

If both SCRIP and GRIDSPEC parsing fail, TIDE reports an error and the stream
initialization is aborted.

## Regrid Methods

The `regrid_method` key selects the ESMF interpolation algorithm. TIDE supports
four methods:

| Method | ESMF Constant | Description |
| --- | --- | --- |
| `bilinear` | `ESMF_REGRIDMETHOD_BILINEAR` | Bilinear interpolation. Suitable for smooth, continuous fields such as temperature or pressure. This is the default. |
| `neareststod` | `ESMF_REGRIDMETHOD_NEAREST_STOD` | Nearest source-to-destination. Each destination point receives the value of the closest source point. Useful for categorical or discrete data. |
| `nearestdtos` | `ESMF_REGRIDMETHOD_NEAREST_DTOS` | Nearest destination-to-source. Each source point contributes to the closest destination point. |
| `conserve` | `ESMF_REGRIDMETHOD_CONSERVE` | First-order conservative remapping. Preserves the area-integrated value of the field across grids. Best for flux quantities (precipitation, radiation). Requires cell corner coordinates in the grid file. |

If an unrecognized value is provided, TIDE reports an error listing the valid
options and aborts initialization:

```text
ERROR: [TIDE] Unrecognized regrid_method: 'patch'
ERROR: [TIDE] Valid values: bilinear, neareststod, nearestdtos, conserve
```

### Choosing a Method

- Use **`bilinear`** (the default) for most smooth geophysical fields. It is
  fast and produces continuous output.
- Use **`conserve`** when the integrated quantity must be preserved — for
  example, precipitation fluxes or radiative fluxes. The target grid file must
  include cell corner coordinates for this method to work correctly.
- Use **`neareststod`** for fields that should not be smoothed, such as land-use
  categories or mask fields.
- Use **`nearestdtos`** when you need each source point to map to exactly one
  destination point (less common; useful for certain diagnostic workflows).

## Configuration Reference

All regridding options are specified inside the `output` block of a stream
definition. The `target_grid_file` key is required whenever an `output` block is
present.

| Key | Type | Default | Description |
| --- | --- | --- | --- |
| `target_grid_file` | string | *(required)* | Path to the SCRIP or GRIDSPEC target grid file. |
| `regrid_method` | string | `"bilinear"` | Interpolation method: `bilinear`, `neareststod`, `nearestdtos`, or `conserve`. |
| `output_file` | string | `""` | Path to the output NetCDF file. |
| `output_frequency` | integer | `3600` | Write interval in seconds. |
| `output_fields` | list | *(all fields)* | Subset of model fields to regrid and write. If omitted, all mapped fields are included. |

## Example

A stream that regrids SST and ice fraction onto a one-degree target grid using
conservative remapping:

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
      target_grid_file: "/grids/target_1deg_scrip.nc"
      regrid_method: "conserve"
      output_file: "sst_regridded.nc"
      output_frequency: 3600
      output_fields:
        - "sea_surface_temperature"
        - "ice_fraction"
```

## Troubleshooting

### Grid file fails to load

If TIDE reports that both SCRIP and GRIDSPEC parsing failed, verify that:

- The file path is correct and the file is readable by all MPI ranks.
- The file is a valid SCRIP or GRIDSPEC NetCDF file (use `ncdump -h` to
  inspect).
- For SCRIP files, the required variables (`grid_center_lat`, `grid_center_lon`,
  `grid_corner_lat`, `grid_corner_lon`, `grid_dims`) are present.

### Conservative regridding produces unexpected values

Conservative remapping requires cell corner (boundary) coordinates. If the
target grid file does not include corner information, ESMF may silently fall
back to less accurate weight computation. Ensure the grid file contains corner
stagger data.

### Source mesh and target grid are incompatible

If `ESMF_FieldRegridStore` fails during initialization, the source mesh and
target grid may not overlap spatially, or one of them may have degenerate cells.
Check the logged coordinate ranges for both grids to confirm they cover the
expected domain.

## See Also

- [YAML Configuration](yaml-configuration.md) — full schema reference including
  the `output` block.
- [Data Streams](data-streams.md) — stream lifecycle and the `map_algo` setting
  for input-side regridding.
- [Output](output.md) — output file configuration and write frequency.
