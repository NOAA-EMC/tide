# Data Streams

A data stream is the central abstraction in TIDE for feeding time-varying input
data into a coupled model. Each stream represents an uninterrupted time series
drawn from one or more NetCDF files, where every file contains the same set of
fields on the same grid. At runtime TIDE reads the bounding time samples around
the current model date, interpolates them in time, regrids the result onto the
model mesh, and delivers the fields to the model component.

## Core Concepts

### Stream Lifecycle

A stream progresses through three phases:

1. **Initialization** — TIDE reads the stream definition (from YAML, XML, or
   inline Fortran arguments), opens the first data file, discovers the time
   coordinate, and builds the spatial regridding route handle from the stream
   mesh to the model mesh.
2. **Advance** — On each model time step TIDE locates the lower-bound (LB) and
   upper-bound (UB) time samples that bracket the current model date, reads any
   new data that is needed, and computes the temporally interpolated field
   values.
3. **Finalization** — Open file handles and ESMF objects are released.

### Time Axis and Year Alignment

Every stream carries three year parameters that control how data dates map to
model dates:

| Parameter | YAML Key | Description |
| --- | --- | --- |
| First year | `year_first` | Earliest year present in the data files. |
| Last year | `year_last` | Latest year present in the data files. |
| Alignment year | `year_align` | The model year that corresponds to `year_first`. |

When the model clock advances beyond the data year range, the **time axis mode**
(`tax_mode`) determines what happens:

| Mode | Behavior |
| --- | --- |
| `cycle` | The data time axis wraps around and repeats. This is the default. |
| `extend` | The last available time sample is held constant (requires `dt_limit` set to a large value such as `1.0e30`). |
| `limit` | An error is raised if the model date falls outside the data range. |

## Input Files

A stream references one or more NetCDF data files that together form a continuous
time series. Files are listed in the YAML configuration under `input_files`:

```yaml
input_files:
  - "sst_monthly_1990_2000.nc"
  - "sst_monthly_2001_2010.nc"
```

Each file must contain:

- A **time dimension** with date/seconds coordinate variables that TIDE can
  parse (CF-style `time` with a `units` attribute such as
  `"days since 1990-01-01"`).
- The **data variables** referenced by the stream's field mappings.
- A consistent grid — all files in a stream share the same spatial domain.

TIDE reads files using PIO (Parallel I/O) and supports both NetCDF classic and
NetCDF-4 formats. Two read modes are available:

| Read Mode | Description |
| --- | --- |
| `single` | Reads only the two time samples (LB and UB) needed for the current interpolation window. This is the default and is memory-efficient. |
| `full_file` | Reads the entire file into memory at once. |

## Field Mappings

Field mappings connect variable names in the input NetCDF files to the names
expected by the model. Each mapping is a pair of identifiers specified in the
`field_maps` list:

```yaml
field_maps:
  - { file_var: "SST",    model_var: "sea_surface_temperature" }
  - { file_var: "UWIND",  model_var: "u_wind_10m" }
```

Internally TIDE maintains two parallel name lists per stream — `fldlist_stream`
(names as they appear in the file) and `fldlist_model` (names used by the model
component). During the advance step, data is read using the file names, spatially
regridded, temporally interpolated, and then stored in ESMF field bundles keyed
by the model names.

### Multi-Level Fields

If the input data contains a vertical dimension, set `lev_dimname` to the name
of that dimension (for example `"lev"` or `"depth"`). TIDE will create fields
with an ungridded vertical dimension so that all levels are carried through the
interpolation and regridding pipeline together.

### Vector Fields

Some physical quantities (for example wind components) must be regridded as
vectors rather than independent scalars to preserve directional consistency
after rotation. The stream configuration supports a `vectors` attribute that
names the paired fields.

## Mesh Files

Each stream can specify a **mesh file** that describes the spatial grid of the
input data. The mesh file is an ESMF-format unstructured mesh file (or a
GRIDSPEC-format regular grid file) and is referenced via the `mesh_file` key:

```yaml
mesh_file: "ocean_mesh.nc"
```

TIDE uses the mesh file to:

1. Create an `ESMF_Mesh` (or `ESMF_Grid`) object representing the stream's
   spatial domain.
2. Build a route handle that maps from the stream domain to the model domain
   using the algorithm specified by `map_algo`.

If `mesh_file` is set to `"none"`, TIDE attempts to construct a grid directly
from the coordinate variables (`lon`, `lat`) found in the first data file.

### Mapping Algorithms

The `map_algo` setting controls how stream data is regridded onto the model
mesh. TIDE supports the following ESMF regridding methods:

| Algorithm | Description |
| --- | --- |
| `bilinear` | Bilinear interpolation. Default and suitable for most smooth fields. |
| `nn` | Nearest-neighbor interpolation. |
| `consf` | First-order conservative remapping (fraction-area normalization). |
| `consd` | First-order conservative remapping (destination-area normalization). |
| `redist` | Redistribution without interpolation — source and destination grids must match. |
| `none` | No spatial mapping is applied. |

## Temporal Interpolation

After spatial regridding, TIDE interpolates the data in time to the current
model date. The interpolation algorithm is set per stream via the `time_interp`
key (internally called `tInterpAlgo`). TIDE computes two weighting factors,
`f1` (weight for the lower-bound sample) and `f2` (weight for the upper-bound
sample), such that `f1 + f2 = 1`. The interpolated value at each grid point is:

```
value = f1 * data_LB + f2 * data_UB
```

### Available Interpolation Modes

TIDE provides five temporal interpolation algorithms:

#### `linear` (default)

Standard linear interpolation between the lower-bound and upper-bound time
samples. The weight for the lower bound is proportional to the time distance
from the model date to the upper bound:

```
f1 = (t_UB - t_model) / (t_UB - t_LB)
f2 = 1 - f1
```

The model date must fall between the two bounding samples. If the two samples
have identical timestamps, both receive equal weight (`f1 = f2 = 0.5`).

#### `lower`

Uses only the lower-bound (earlier) time sample. The upper-bound sample is
ignored:

```
f1 = 1,  f2 = 0
```

This is useful for step-function forcing data where the value should remain
constant until the next sample time.

#### `upper`

Uses only the upper-bound (later) time sample:

```
f1 = 0,  f2 = 1
```

#### `nearest`

Selects whichever bounding sample is closest in time to the current model date:

```
if |t_model - t_LB| <= |t_UB - t_model|:
    f1 = 1,  f2 = 0
else:
    f1 = 0,  f2 = 1
```

#### `coszen`

A specialized mode for solar radiation fields. Instead of simple time
weighting, TIDE computes a time-averaged cosine of the solar zenith angle over
the interval `[t_LB, t_UB]` using the model time step as the Riemann-sum
partition width. The cosine of the zenith angle is calculated from orbital
parameters (eccentricity, obliquity, moving vernal equinox longitude) and the
latitude/longitude of each grid point. This ensures that shortwave radiation
forcing is applied with the correct diurnal distribution.

The `coszen` mode requires that orbital parameters have been set on the stream
data object via `shr_strdata_setOrbs` before the first advance call.

### Delta-Time Limit

The `dt_limit` parameter (default `1.5`) guards against excessive time
extrapolation. It represents the maximum allowable ratio between the stream
time-step interval and the model time-step interval. If the ratio is exceeded,
TIDE raises an error. When using `extend` time-axis mode, set `dt_limit` to a
very large value (for example `1.0e30`) to allow indefinite hold of the last
sample.

### Time Offset

The `offset` parameter (default `0`) shifts the data time axis by a fixed
number of seconds. This is useful when the data timestamps represent interval
midpoints but the model expects interval-start timestamps, or vice versa.

## Putting It All Together

The following diagram summarizes how a single stream processes data at each
model time step:

```
Input Files ──► PIO Read (LB & UB samples)
                        │
                        ▼
              Stream Mesh / Grid
                        │
              map_algo (bilinear, nn, ...)
                        │
                        ▼
                Model Mesh Fields
                  (LB & UB bundles)
                        │
              time_interp (linear, coszen, ...)
                        │
                        ▼
              Interpolated Model Fields
                        │
                        ▼
              Export to Model Component
```

For a complete reference of all YAML keys that control stream behavior, see the
[YAML Configuration](yaml-configuration.md) page. For details on the spatial
regridding step, see the [Regridding](regridding.md) page.
