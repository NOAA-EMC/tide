Architecture Plan: AMIO + Atlas Data Forcing Library

1. Executive Summary & Vision

The objective is to build a high-performance, plug-and-play Data Forcing Library that feeds static or transient data streams (e.g., emissions, atmospheric states, boundary conditions) directly into an active Earth System Model (ESM) or standalone component.

By replacing legacy, heavy-weight couplers (like ESMF/CDEPS) with a pure C++20 engine, this library provides zero-copy memory handoffs, high-fidelity interpolation, and seamless multi-language integration.

Core Technologies

AMIO: High-performance, MPI-parallel reading of NetCDF and GRIB2 files.

ECMWF Atlas: Advanced mesh generation, KD-Tree spatial searches, and strict conservative horizontal regridding.

TSPACK (C-version): Monotonicity-preserving tension splines for high-fidelity vertical interpolation.

std::mdspan: The universal, zero-copy memory API boundary bridging C++ and Fortran.

2. System Architecture & Data Pipeline

The library operates as a pure pipeline, executing highly optimized transformations before handing data back to the host model's native memory space.

2.1 The Data Flow Diagram

[Input Files: NetCDF (CF/COARDS/UGRID) or GRIB2]
       │
       ▼ (1. AMIO Parallel Read & Grid Builder)
[Memory Ring Buffer: T_prev, T_next arrays]
       │
       ▼ (2. Temporal Engine: DOY matching & linear time interp)
[Time-Interpolated Source Field]
       │
       ▼ (3. Atlas Spatial Engine: Conservative horizontal regridding)
[Target Horizontal Field]
       │
       ▼ (4. TSPACK Vertical Engine: 1D Tension Spline interp to host levels)
[Target 3D Field]
       │
       ▼ (5. Scaling Engine: Inline unit conversion Y = MX + B)
[Final std::mdspan View] <--- (Zero-Copy API Boundary)
       │
       ▼
[Host Model Native Memory (C++ or Fortran)]
