# Stream Modules

The stream modules manage input data stream reading, temporal interpolation, and
helper methods for ESMF field bundle operations. They provide the infrastructure
for reading time-series data from NetCDF files, computing interpolation factors
between bounding time samples, and performing field-level operations on ESMF
states and field bundles.

## dshr_strdata_mod

Stream data manager module. Defines the `shr_strdata_type` that holds per-stream
state (mesh, field bundles, time bounds, route handles) and model domain metadata.
Provides initialization from configuration files or inline Fortran arguments
(`shr_strdata_init_from_config`, `shr_strdata_init_from_inline`), time-stepping
via `shr_strdata_advance` (which reads bounding data, performs spatial regridding,
and applies temporal interpolation), orbital parameter setup for cosine-zenith
interpolation (`shr_strdata_setOrbs`), and accessors for stream field bundle
pointers and stream counts.

::: doxy.tide.Class
    name: dshr_strdata_mod

## dshr_stream_mod

Stream file sequence module. Defines the `shr_stream_streamType` for managing an
ordered sequence of input data files that form a continuous time series on a single
grid. Provides stream initialization from XML, ESMF config, or inline Fortran
(`shr_stream_init_from_xml`, `shr_stream_init_from_esmfconfig`,
`shr_stream_init_from_inline`), time-bound lookup (`shr_stream_findBounds`),
file navigation (`shr_stream_getNextFileName`, `shr_stream_getPrevFileName`),
field name accessors, calendar queries, PIO-based data reading, and restart I/O.
Defines constants for time-axis modes (cycle, extend, limit), interpolation
algorithms (lower, upper, nearest, linear, coszen), and mapping methods (bilinear,
nearest-neighbor, conservative).

::: doxy.tide.Class
    name: dshr_stream_mod

## dshr_tinterp_mod

Temporal interpolation module. Provides `shr_tInterp_getFactors` to compute
interpolation weights between lower-bound and upper-bound time samples using
selectable algorithms (lower, upper, nearest, linear). Provides
`shr_tInterp_getAvgCosz` to compute time-averaged cosine of solar zenith angle
over an interval using Riemann summation, and `shr_tInterp_getCosz` to compute
instantaneous cosine of solar zenith angle from orbital parameters and geographic
coordinates.

::: doxy.tide.Class
    name: dshr_tinterp_mod

## dshr_methods_mod

Shared helper methods module. Provides ESMF state and field bundle utility routines
including `dshr_state_getfldptr` (retrieve pointers from ESMF states),
`dshr_fldbun_GetFldPtr` (retrieve field data pointers from field bundles),
`dshr_fldbun_regrid` (apply ESMF regridding across all fields in a bundle),
`dshr_fldbun_FldChk` (check field presence), `dshr_fldbun_diagnose` and
`dshr_state_diagnose` (log min/max/sum diagnostics), and `chkerr` (ESMF error
checking). Also provides `dshr_cal_aligndow` for aligning dates to a target day
of week.

::: doxy.tide.Class
    name: dshr_methods_mod
