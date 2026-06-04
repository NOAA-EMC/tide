Here is the complete, consolidated architecture plan generated again for you:

### 2.2 Memory Management Contract

1. **Grid Ingestion:** The library reads metadata to construct `atlas::Grid`, `atlas::Mesh`, or `atlas::PointCloud`.
2. **Host Allocation:** The host model allocates the target arrays in its own memory space.
3. **The API Boundary:** Raw pointers from the host are wrapped in `std::mdspan`. For Fortran arrays, `std::layout_left` is strictly enforced to handle column-major strides automatically.
4. **Zero-Copy Handoff:** Regridded and scaled data is written directly into the `mdspan`, making it immediately available to the host model upon function return.

---

## 3. Geometry Engine (Grid & Mesh Parsing)

The "Grid Builder" layer dynamically translates file metadata into Atlas geometries.

* **Rectilinear (CF/COARDS):** Mapped to `atlas::StructuredGrid`. Strict conservative regridding requires cell edges; if explicit `bounds` variables are missing (common in COARDS), the library dynamically calculates them via midpoint extrapolation.
* **Curvilinear (CF):** Mapped to a quadrilateral `atlas::Mesh`.
* **Unstructured (UGRID):** Explicit `atlas::Mesh` generation using `node_coordinates` and `face_node_connectivity`.
* **Gaussian/Standard (GRIB2):** Maps Grid Definition Templates (GDTs) natively to `atlas::GaussianGrid` or `atlas::RegularLonLatGrid`.
* **Discrete Points:** Point-source data (e.g., smokestacks) mapped to `atlas::PointCloud`. Uses Atlas's `atlas::util::PointSearch` (KD-Tree) to map discrete points to target cells without "smearing" or collapsing them via standard interpolation.

---

## 4. Temporal Engine & Extrapolation

* **Ring Buffer:** AMIO maintains bounding time slices ($T_{prev}$, $T_{next}$) asynchronously.
* **Time Interpolation:** Linear temporal interpolation via `std::transform` execution policies.
* **Out-of-Bounds Logic:** When the requested model time exceeds the dataset's available bounds (e.g., running a 2026 simulation with 2024 emissions), the library calculates an `effective_time` via user configuration:
1. **Strict:** Abort simulation.
2. **Seasonal Preservation:** Snap to the closest available year but strictly match the model's Day of Year (DOY) to preserve seasonal cycles.
3. **Cyclic:** Loop the dataset endlessly.



---

## 5. Multi-Language Bindings

To guarantee broad usability across ESMs, the library strictly avoids external parallel framework dependencies (like Kokkos) at the API level.

* **Native C++ API:** Accepts `std::mdspan` directly.
* **C-API:** A flat `extern "C"` interface accepts raw pointers and dimension sizes.
* **Fortran API:** An Object-Oriented Fortran module utilizes `iso_c_binding` to call the C-API. It passes Fortran arrays as raw pointers, which the C++ core wraps in `std::mdspan<..., std::layout_left>` to guarantee correct memory traversal.

---

## 6. Scientific Capabilities & Phasing

To fully replace legacy couplers, the library must guarantee physical correctness. Features are strictly prioritized.

### MVP (Initial Release & Strict Requirements)

#### 6.1 Bit-for-Bit (B4B) Reproducibility (Core Requirement)

In Earth System Modeling, differing core counts must yield identical results. The underlying `atlas::Interpolation` matrix multiplications and all C++ global sums will be strictly configured for reproducible arithmetic, guaranteeing bit-for-bit integrity regardless of MPI domain decomposition.

#### 6.2 High-Fidelity 3D Vertical Interpolation (TSPACK)

Mapping from source pressure/sigma levels to the host model's vertical levels using standard methods often introduces unphysical overshoots (negative concentrations).

* **Implementation:** The library invokes the C-version of `tspack` (Tension Spline Package). `tspack` computes a monotonicity-preserving tension spline for each vertical column, guaranteeing smooth, physically bounded profiles (configurable to linear or $\ln(P)$ space).

#### 6.3 Inline Unit Scaling

A lightweight C++ execution loop applied directly to the `mdspan` views to apply $Y = M*X + B$ scale factors and offsets defined in the stream configuration, standardizing units before handoff.

### Phase 2 (Deferred Extensions)

#### 6.4 Masking and Fractional Area Regridding

**Goal:** Prevent the "smearing" of continental data into ocean grid cells during conservative interpolation.
**Plan:** Ingest CF `land_area_fraction` or `missing_value` masks and configure `atlas::Interpolation` to perform *fractional* conservative interpolation based on cell sub-areas.

#### 6.5 Vector Field Rotation

**Goal:** Mathematically correct interpolation of wind vectors onto complex grids.
**Plan:** Variables tagged as vector pairs (U/V) will use Atlas's tangent-plane rotation capabilities to automatically rotate vectors into the target mesh's local coordinate system (e.g., Cubed-Sphere local north/east) during regridding.

```

```
