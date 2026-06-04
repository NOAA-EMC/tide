# TSPACK (C version)

Vendored from [noaa-oar-arl/pytspack](https://github.com/noaa-oar-arl/pytspack/tree/master/src).

This is a C translation of Robert J. Renka's TSPACK (ACM TOMS Algorithm 716) — a tension spline package for curve interpolation and smoothing that preserves monotonicity and convexity.

## Files

- `tspack.h` — Public API header (C++ compatible via `extern "C"`)
- `tspack.c` — Core implementation (interpolation, derivatives, tension factor computation)

## Key Functions for TIDE

- `tspsi()` — Compute derivatives and tension factors for an interpolatory curve
- `hval()` — Evaluate the Hermite tension spline at a single point
- `tsval1()` — Evaluate the spline at multiple points (batch)
- `ypc1()` / `ypc2()` — Compute first/second-order derivative estimates

The Python binding (`pytspack.c`) is not vendored here as TIDE uses the C API directly.
