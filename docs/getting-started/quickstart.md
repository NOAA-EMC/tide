# Quickstart

This guide walks through a minimal example of using TIDE to read and interpolate a data stream in a Fortran application.

## 1. Create a YAML Configuration

TIDE streams are configured via a YAML file. Each stream defines input files, field mappings, and temporal interpolation settings.

Create a file called `tide_config.yaml`:

```yaml
streams:
  - name: sst_stream
    mesh_file: "input_mesh.nc"
    tax_mode: "cycle"
    time_interp: "linear"
    map_algo: "bilinear"
    year_first: 2000
    year_last: 2010
    year_align: 2000
    input_files:
      - "sst_data.nc"
    field_maps:
      - { file_var: "SST", model_var: "sea_surface_temperature" }
```

Key fields:

- `mesh_file` — the SCRIP or ESMF mesh describing the input data grid.
- `tax_mode` — time axis mode (`cycle` to loop over the year range, `limit` to clamp).
- `time_interp` — interpolation method between time slices (`linear`, `lower`, `upper`, `nearest`, `coszen`).
- `map_algo` — spatial regridding algorithm (`bilinear`, `patch`, `nearestdtos`, `nearestdtod`, `conserve`).
- `field_maps` — maps each variable name in the input file (`file_var`) to the name your model uses (`model_var`).

## 2. Initialize, Advance, and Finalize

The host model interacts with TIDE through three subroutines: `tide_init`, `tide_advance`, and `tide_finalize`.

```fortran
program example
  use tide_mod
  use ESMF
  implicit none

  type(tide_type) :: tide
  type(ESMF_Mesh) :: model_mesh
  type(ESMF_Clock) :: clock
  real(8), pointer :: sst_ptr(:,:)
  integer :: rc

  ! ... set up ESMF, create model_mesh and clock ...

  ! 1. Initialize TIDE from the YAML configuration
  call tide_init(tide, "tide_config.yaml", model_mesh, clock, rc)
  if (rc /= ESMF_SUCCESS) stop "tide_init failed"

  ! 2. Advance streams to the current clock time (call each timestep)
  call tide_advance(tide, clock, rc)
  if (rc /= ESMF_SUCCESS) stop "tide_advance failed"

  ! 3. Retrieve interpolated field data
  call tide_get_ptr(tide, "sea_surface_temperature", sst_ptr, rc)
  ! sst_ptr now contains the interpolated SST field

  ! 4. Clean up
  call tide_finalize(tide, rc)

end program example
```

### Call sequence summary

| Subroutine | Purpose |
| --- | --- |
| `tide_init(tide, config_yaml, model_mesh, clock, rc)` | Parse the YAML file and initialize all streams, regridding, and output. |
| `tide_advance(tide, clock, rc)` | Read and interpolate stream data to the current model time. Call once per timestep. |
| `tide_get_ptr(tide, field_name, ptr, rc)` | Get a pointer to the interpolated data for a named field. |
| `tide_finalize(tide, rc)` | Release all TIDE resources. |

## Next Steps

- See the [YAML Configuration](../user-guide/yaml-configuration.md) guide for the full stream schema reference.
- See the [Data Streams](../user-guide/data-streams.md) guide for details on temporal interpolation and field mapping.
- See the [API Reference](../api/core-modules.md) for complete subroutine signatures and type definitions.
