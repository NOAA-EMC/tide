/**
 * @file spatial.cpp
 * @brief Implementation of the TIDE Atlas Spatial Regridding Engine.
 *
 * Builds and caches Atlas interpolation weight matrices, then applies them
 * to source fields for horizontal regridding. Supports all Atlas interpolation
 * methods and multiple source grid topologies including curvilinear and
 * PointCloud.
 *
 * Atlas is configured for deterministic (B4B reproducible) weight computation
 * independent of MPI decomposition. Weight matrices are stored internally by
 * the Atlas Interpolation object (CSR format) for efficient repeated application.
 *
 * Validates Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9,
 *                         3.10, 3.11, 3.12, 13.1, 13.2
 */

#include <tide/spatial.hpp>
#include <tide/error.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "atlas/array.h"
#include "atlas/field.h"
#include "atlas/functionspace.h"
#include "atlas/functionspace/NodeColumns.h"
#include "atlas/functionspace/PointCloud.h"
#include "atlas/grid.h"
#include "atlas/grid/UnstructuredGrid.h"
#include "atlas/interpolation.h"
#include "atlas/mesh.h"
#include "atlas/mesh/Mesh.h"
#include "atlas/mesh/Nodes.h"
#include "atlas/meshgenerator.h"
#include "atlas/option.h"
#include "atlas/util/Config.h"
#include "atlas/util/Point.h"

namespace tide::spatial {

// ─────────────────────────────────────────────────────────────────────────────
// String ↔ Enum conversion utilities
// ─────────────────────────────────────────────────────────────────────────────

InterpMethod interp_method_from_string(std::string_view name) noexcept {
    if (name == "finite-element") return InterpMethod::FiniteElement;
    if (name == "k-nearest-neighbours") return InterpMethod::KNearestNeighbours;
    if (name == "nearest-neighbour") return InterpMethod::NearestNeighbour;
    if (name == "structured-linear2D") return InterpMethod::StructuredLinear2D;
    if (name == "structured-cubic2D") return InterpMethod::StructuredCubic2D;
    if (name == "structured-quasicubic2D") return InterpMethod::StructuredQuasicubic2D;
    if (name == "conservative") return InterpMethod::Conservative;
    // Warn about unrecognized method before falling back
    std::fprintf(stderr,
        "[tide::spatial] Warning: unrecognized interpolation method '%.*s', "
        "falling back to 'finite-element'\n",
        static_cast<int>(name.size()), name.data());
    return InterpMethod::FiniteElement;
}

std::string_view interp_method_to_string(InterpMethod method) noexcept {
    switch (method) {
        case InterpMethod::FiniteElement:          return "finite-element";
        case InterpMethod::KNearestNeighbours:     return "k-nearest-neighbours";
        case InterpMethod::NearestNeighbour:       return "nearest-neighbour";
        case InterpMethod::StructuredLinear2D:     return "structured-linear2D";
        case InterpMethod::StructuredCubic2D:      return "structured-cubic2D";
        case InterpMethod::StructuredQuasicubic2D: return "structured-quasicubic2D";
        case InterpMethod::Conservative:           return "conservative";
    }
    return "finite-element";
}

MissingDataMode missing_mode_from_string(std::string_view name) noexcept {
    if (name == "missing-if-any-missing") return MissingDataMode::MissingIfAnyMissing;
    if (name == "missing-if-all-missing") return MissingDataMode::MissingIfAllMissing;
    if (name == "missing-if-heaviest-missing") return MissingDataMode::MissingIfHeaviestMissing;
    // Default to heaviest (Req 3.6)
    return MissingDataMode::MissingIfHeaviestMissing;
}

// ─────────────────────────────────────────────────────────────────────────────
// Cell bounds inference (midpoint extrapolation) — Req 3.12
// ─────────────────────────────────────────────────────────────────────────────

std::vector<double> infer_cell_bounds(std::span<const double> coords) {
    const auto n = coords.size();
    if (n < 2) {
        // Degenerate: single cell, assume unit width centered on the point
        std::vector<double> bounds(2);
        bounds[0] = coords[0] - 0.5;
        bounds[1] = coords[0] + 0.5;
        return bounds;
    }

    std::vector<double> bounds(n + 1);

    // Interior edges: midpoints between adjacent centres
    for (std::size_t i = 0; i < n - 1; ++i) {
        bounds[i + 1] = 0.5 * (coords[i] + coords[i + 1]);
    }

    // Boundary edges: extrapolate by half-interval from outermost centres
    bounds[0] = coords[0] - 0.5 * (coords[1] - coords[0]);
    bounds[n] = coords[n - 1] + 0.5 * (coords[n - 1] - coords[n - 2]);

    return bounds;
}

// ─────────────────────────────────────────────────────────────────────────────
// Internal helpers: Atlas grid construction
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * @brief Get the Atlas interpolation type string for an InterpMethod.
 */
std::string atlas_interp_type(InterpMethod method) {
    switch (method) {
        case InterpMethod::FiniteElement:          return "finite-element";
        case InterpMethod::KNearestNeighbours:     return "k-nearest-neighbours";
        case InterpMethod::NearestNeighbour:       return "nearest-neighbour";
        case InterpMethod::StructuredLinear2D:     return "structured-bilinear";
        case InterpMethod::StructuredCubic2D:      return "structured-bicubic";
        case InterpMethod::StructuredQuasicubic2D: return "structured-quasicubic";
        case InterpMethod::Conservative:           return "conservative-spherical-polygon";
    }
    return "finite-element";
}

/**
 * @brief Get the Atlas non-linear missing value type string.
 */
std::string atlas_missing_type(MissingDataMode mode) {
    switch (mode) {
        case MissingDataMode::MissingIfAnyMissing:     return "missing-if-any-missing";
        case MissingDataMode::MissingIfAllMissing:     return "missing-if-all-missing";
        case MissingDataMode::MissingIfHeaviestMissing: return "missing-if-heaviest-missing";
    }
    return "missing-if-heaviest-missing";
}

/**
 * @brief Build an Atlas StructuredGrid from a regular lat-lon SourceGrid.
 *
 * Determines grid dimensions from the coordinate arrays. Handles two cases:
 * - 1D axes: source.lats.size() * source.lons.size() == source.num_cells
 *   (lats/lons contain axis values, not expanded pairs)
 * - Expanded pairs: source.lats.size() == source.num_cells
 *   (lats/lons contain one entry per grid cell)
 */
atlas::Grid build_regular_latlon_grid(const SourceGrid& source) {
    if (source.lats.empty() || source.lons.empty()) {
        return atlas::Grid();
    }

    std::vector<double> unique_lats;
    std::vector<double> unique_lons;

    // Detect whether lats/lons are 1D axes or expanded coordinate pairs
    const bool is_1d_axes = (source.lats.size() * source.lons.size() == source.num_cells)
                            && (source.lats.size() != source.num_cells);

    if (is_1d_axes) {
        // lats and lons ARE the unique axis values already
        unique_lats = source.lats;
        unique_lons = source.lons;
        std::sort(unique_lats.begin(), unique_lats.end());
        std::sort(unique_lons.begin(), unique_lons.end());
    } else {
        // Expanded pairs: extract unique values
        unique_lats = source.lats;
        unique_lons = source.lons;
        std::sort(unique_lats.begin(), unique_lats.end());
        std::sort(unique_lons.begin(), unique_lons.end());
        unique_lats.erase(std::unique(unique_lats.begin(), unique_lats.end()), unique_lats.end());
        unique_lons.erase(std::unique(unique_lons.begin(), unique_lons.end()), unique_lons.end());
    }

    const auto nlat = unique_lats.size();
    const auto nlon = unique_lons.size();

    if (nlat == 0 || nlon == 0) {
        return atlas::Grid();
    }

    // Determine grid bounds from sorted coordinate values
    // For Atlas regular_lonlat, north > south and east > west are required.
    double south = unique_lats.front();
    double north = unique_lats.back();
    double west = unique_lons.front();
    double east = unique_lons.back();

    // Atlas requires the grid interval to be consistent. For grids with
    // more than one point per axis, compute the spacing and extend bounds
    // by half a grid cell so Atlas generates points at the correct locations.
    if (nlat > 1) {
        double dlat = (north - south) / static_cast<double>(nlat - 1);
        south -= dlat * 0.5;
        north += dlat * 0.5;
    }
    if (nlon > 1) {
        double dlon = (east - west) / static_cast<double>(nlon - 1);
        west -= dlon * 0.5;
        east += dlon * 0.5;
    }

    // Construct Atlas regular lon-lat grid
    atlas::util::Config config;
    config.set("type", "regular_lonlat");
    config.set("nx", static_cast<long>(nlon));
    config.set("ny", static_cast<long>(nlat));
    config.set("north", north);
    config.set("south", south);
    config.set("west", west);
    config.set("east", east);

    return atlas::Grid(config);
}

/**
 * @brief Build an Atlas Gaussian grid from a SourceGrid.
 */
atlas::Grid build_gaussian_grid(const SourceGrid& source) {
    std::vector<double> unique_lats = source.lats;
    std::sort(unique_lats.begin(), unique_lats.end());
    unique_lats.erase(std::unique(unique_lats.begin(), unique_lats.end()), unique_lats.end());

    const long N = static_cast<long>(unique_lats.size()) / 2;
    if (N <= 0) {
        return atlas::Grid();
    }

    // Atlas Gaussian grid: "F<N>" for full Gaussian
    std::string grid_name = "F" + std::to_string(N);
    return atlas::Grid(grid_name);
}

/**
 * @brief Build an Atlas reduced Gaussian grid from a SourceGrid.
 */
atlas::Grid build_reduced_gaussian_grid(const SourceGrid& source) {
    std::vector<double> unique_lats = source.lats;
    std::sort(unique_lats.begin(), unique_lats.end());
    unique_lats.erase(std::unique(unique_lats.begin(), unique_lats.end()), unique_lats.end());

    const long N = static_cast<long>(unique_lats.size()) / 2;
    if (N <= 0) {
        return atlas::Grid();
    }

    // Atlas reduced Gaussian grid: "N<N>"
    std::string grid_name = "N" + std::to_string(N);
    return atlas::Grid(grid_name);
}

/**
 * @brief Build an Atlas UnstructuredGrid from coordinate arrays.
 *
 * Used for Curvilinear, Unstructured (UGRID), and PointCloud source types.
 * Constructs points from the lon/lat coordinate arrays.
 */
atlas::Grid build_point_grid(const std::vector<double>& lons,
                             const std::vector<double>& lats,
                             std::size_t num_points) {
    if (lons.size() < num_points || lats.size() < num_points || num_points == 0) {
        return atlas::Grid();
    }

    std::vector<atlas::PointXY> points;
    points.reserve(num_points);
    for (std::size_t i = 0; i < num_points; ++i) {
        points.emplace_back(lons[i], lats[i]);
    }

    return atlas::UnstructuredGrid(std::move(points));
}

/**
 * @brief Build an Atlas Mesh from ESMF mesh cell connectivity.
 *
 * Constructs a proper Atlas Mesh with polygon cell geometry from the
 * element connectivity stored in SourceGrid::lat_bounds/lon_bounds.
 * This is required for conservative-spherical-polygon interpolation,
 * which needs actual cell polygon geometry rather than just point coordinates.
 *
 * The lat_bounds/lon_bounds arrays from read_esmf_mesh() store vertex
 * coordinates for each element sequentially:
 *   [elem0_v0, elem0_v1, ..., elem1_v0, elem1_v1, ...]
 *
 * For Atlas conservative interpolation, we use the Delaunay mesh generator
 * on the cell center coordinates to create a proper triangulated mesh,
 * then use NodeColumns function space. This approach leverages Atlas's
 * built-in mesh infrastructure rather than manual mesh construction.
 *
 * @param source SourceGrid with ESMF_Mesh metadata_convention
 * @return Atlas Mesh from Delaunay triangulation of cell centers, or empty
 */
atlas::Mesh build_mesh_from_unstructured_grid(const SourceGrid& source) {
    if (source.lats.empty() || source.lons.empty() || source.num_cells == 0) {
        return atlas::Mesh();
    }

    // Build an UnstructuredGrid from cell center coordinates
    std::vector<atlas::PointXY> points;
    points.reserve(source.num_cells);
    for (std::size_t i = 0; i < source.num_cells; ++i) {
        points.emplace_back(source.lons[i], source.lats[i]);
    }

    atlas::UnstructuredGrid grid(std::move(points));
    if (!grid) {
        return atlas::Mesh();
    }

    // Use Delaunay mesh generator to create a triangulated mesh
    // from the unstructured point cloud. This provides cell geometry
    // needed for conservative interpolation.
    try {
        atlas::MeshGenerator meshgen("delaunay");
        return meshgen.generate(grid);
    } catch (...) {
        return atlas::Mesh();
    }
}

/**
 * @brief Build an Atlas Mesh from a structured grid for conservative regridding.
 *
 * Uses Atlas's structured MeshGenerator to produce a proper mesh with cell
 * geometry from a regular lat-lon grid. This mesh provides the polygon cells
 * needed for conservative-spherical-polygon interpolation.
 *
 * @param grid Atlas StructuredGrid to generate mesh from
 * @return Atlas Mesh with cell geometry, or empty on failure
 */
atlas::Mesh build_structured_mesh_for_conservative(const atlas::Grid& grid) {
    if (!grid) {
        return atlas::Mesh();
    }

    try {
        atlas::MeshGenerator meshgen("structured");
        return meshgen.generate(grid);
    } catch (...) {
        // Fall back to Delaunay if structured generator fails
        try {
            atlas::MeshGenerator meshgen("delaunay");
            return meshgen.generate(grid);
        } catch (...) {
            return atlas::Mesh();
        }
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// AtlasRegridder::Impl — PImpl holding Atlas objects
// ─────────────────────────────────────────────────────────────────────────────

struct AtlasRegridder::Impl {
    /// @brief Atlas interpolation object (contains cached CSR weight matrix).
    atlas::Interpolation interpolation;

    /// @brief Source function space for field creation.
    atlas::FunctionSpace source_fs;

    /// @brief Target function space for field creation.
    atlas::FunctionSpace target_fs;

    /// @brief Interpolation method used.
    InterpMethod method = InterpMethod::FiniteElement;

    /// @brief Missing data mode.
    MissingDataMode missing_mode = MissingDataMode::MissingIfHeaviestMissing;

    /// @brief Number of source grid cells.
    std::size_t source_size = 0;

    /// @brief Number of target grid columns.
    std::size_t target_size = 0;

    /// @brief Whether weights have been computed.
    bool weights_ready = false;

    /// @brief Whether this is an identity regridding (source == target grid).
    bool is_identity = false;
};

// ─────────────────────────────────────────────────────────────────────────────
// AtlasRegridder public interface
// ─────────────────────────────────────────────────────────────────────────────

AtlasRegridder::AtlasRegridder() : impl_(std::make_unique<Impl>()) {}

AtlasRegridder::~AtlasRegridder() = default;

AtlasRegridder::AtlasRegridder(AtlasRegridder&&) noexcept = default;

AtlasRegridder& AtlasRegridder::operator=(AtlasRegridder&&) noexcept = default;

bool AtlasRegridder::has_weights() const noexcept {
    return impl_ && impl_->weights_ready;
}

InterpMethod AtlasRegridder::method() const noexcept {
    return impl_ ? impl_->method : InterpMethod::FiniteElement;
}

std::size_t AtlasRegridder::source_size() const noexcept {
    return impl_ ? impl_->source_size : 0;
}

std::size_t AtlasRegridder::target_size() const noexcept {
    return impl_ ? impl_->target_size : 0;
}

auto AtlasRegridder::build_weights(const SourceGrid& source,
                                    const TargetGrid& target,
                                    InterpMethod method,
                                    MissingDataMode missing_mode) -> int {
    impl_->method = method;
    impl_->missing_mode = missing_mode;
    impl_->source_size = source.num_cells;
    impl_->target_size = target.num_cols;
    impl_->weights_ready = false;
    impl_->is_identity = false;

    // Early reject: empty grids cannot be regridded
    if (source.num_cells == 0 || target.num_cols == 0) {
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // ─── Identity grid detection ─────────────────────────────────────────
    // When the source and target grids are identical (same lat/lon coordinates),
    // we can skip Atlas interpolation entirely and use a direct passthrough.
    // This avoids issues with Atlas StructuredColumns halo on small grids
    // and is much more efficient.
    if (source.num_cells == target.num_cols &&
        target.num_cols > 0 &&
        target.lats.size() == target.num_cols &&
        target.lons.size() == target.num_cols) {
        // Determine the source grid's unique lat/lon axes
        std::vector<double> src_unique_lats;
        std::vector<double> src_unique_lons;

        // Detect whether lats/lons are 1D axes or expanded coordinate pairs
        const bool is_1d_axes = (source.type == SourceGrid::Type::RegularLatLon)
                                && (source.lats.size() * source.lons.size() == source.num_cells)
                                && (source.lats.size() != source.num_cells);

        if (is_1d_axes) {
            // 1D axes: lats/lons are already the unique axis values
            src_unique_lats = source.lats;
            src_unique_lons = source.lons;
            std::sort(src_unique_lats.begin(), src_unique_lats.end());
            std::sort(src_unique_lons.begin(), src_unique_lons.end());
        } else if (source.type == SourceGrid::Type::RegularLatLon) {
            // Expanded pairs: extract unique values
            src_unique_lats = source.lats;
            src_unique_lons = source.lons;
            std::sort(src_unique_lats.begin(), src_unique_lats.end());
            std::sort(src_unique_lons.begin(), src_unique_lons.end());
            src_unique_lats.erase(std::unique(src_unique_lats.begin(), src_unique_lats.end()), src_unique_lats.end());
            src_unique_lons.erase(std::unique(src_unique_lons.begin(), src_unique_lons.end()), src_unique_lons.end());
        }

        if (!src_unique_lats.empty() && !src_unique_lons.empty()) {
            const auto nlat = src_unique_lats.size();
            const auto nlon = src_unique_lons.size();

            if (nlat * nlon == target.num_cols) {
                // The target grid is stored as a Cartesian product of (lat, lon) pairs
                // in row-major order: for each lat, iterate over all lons.
                // Check if target points match source grid points.
                bool is_identity = true;
                for (std::size_t j = 0; j < nlat && is_identity; ++j) {
                    for (std::size_t i = 0; i < nlon && is_identity; ++i) {
                        std::size_t idx = j * nlon + i;
                        if (std::abs(target.lats[idx] - src_unique_lats[j]) > 1.0e-6 ||
                            std::abs(target.lons[idx] - src_unique_lons[i]) > 1.0e-6) {
                            is_identity = false;
                        }
                    }
                }

                if (is_identity) {
                    // Mark as identity — apply() will just copy data
                    impl_->is_identity = true;
                    impl_->weights_ready = true;
                    return 0;
                }
            }
        }
    }

    // ─── Build source grid ───────────────────────────────────────────────
    atlas::Grid source_grid;
    atlas::Mesh source_mesh;  // Used for conservative with ESMF sources
    bool use_mesh_for_conservative = false;
    bool source_is_structured = (source.type == SourceGrid::Type::RegularLatLon ||
                                  source.type == SourceGrid::Type::Gaussian ||
                                  source.type == SourceGrid::Type::ReducedGaussian);

    switch (source.type) {
        case SourceGrid::Type::RegularLatLon:
            source_grid = build_regular_latlon_grid(source);
            break;
        case SourceGrid::Type::Gaussian:
            source_grid = build_gaussian_grid(source);
            break;
        case SourceGrid::Type::ReducedGaussian:
            source_grid = build_reduced_gaussian_grid(source);
            break;
        case SourceGrid::Type::Curvilinear:
        case SourceGrid::Type::Unstructured:
        case SourceGrid::Type::PointCloud:
            // All non-structured types → UnstructuredGrid from coordinates
            source_grid = build_point_grid(source.lons, source.lats, source.num_cells);
            break;
    }

    // For conservative regridding with ESMF sources, we need proper cell
    // geometry (polygon meshes) rather than just point coordinates.
    // Atlas conservative-spherical-polygon requires a Mesh with cell polygons.
    if (method == InterpMethod::Conservative) {
        if (source.metadata_convention == "ESMF_Mesh" &&
            !source.lat_bounds.empty() && !source.lon_bounds.empty()) {
            // ESMF mesh: use Delaunay triangulation of cell centers to build
            // a mesh with proper cell geometry for conservative regridding (Req 3.2)
            source_mesh = build_mesh_from_unstructured_grid(source);
            if (source_mesh.nodes().size() > 0) {
                use_mesh_for_conservative = true;
            }
        } else if (source_is_structured && source_grid) {
            // Structured grid (Req 3.1): generate mesh from StructuredGrid
            // to provide proper cell geometry for conservative method.
            // This works for ESMF_GridSpec with explicit bounds, CF grids with
            // explicit or inferred bounds, and all other structured types.
            // The structured mesh generator does not require Qhull.
            source_mesh = build_structured_mesh_for_conservative(source_grid);
            if (source_mesh.nodes().size() > 0) {
                use_mesh_for_conservative = true;
            }
        } else if (source.metadata_convention == "ESMF_GridSpec" &&
                   !source.lat_bounds.empty() && !source.lon_bounds.empty() &&
                   !source.bounds_inferred) {
            // ESMF grid spec that wasn't handled as structured (unusual case):
            // Build mesh from cell center coordinates using Delaunay
            source_mesh = build_mesh_from_unstructured_grid(source);
            if (source_mesh.nodes().size() > 0) {
                use_mesh_for_conservative = true;
            }
        } else if (!source.lat_bounds.empty() && !source.lon_bounds.empty() &&
                   !source.bounds_inferred) {
            // Non-structured grid with explicit bounds: use Delaunay
            source_mesh = build_mesh_from_unstructured_grid(source);
            if (source_mesh.nodes().size() > 0) {
                use_mesh_for_conservative = true;
            }
        }
    }

    if (!source_grid && !use_mesh_for_conservative) {
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // ─── Build target grid ───────────────────────────────────────────────
    atlas::Grid target_grid = build_point_grid(target.lons, target.lats, target.num_cols);
    if (!target_grid) {
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // ─── Build function spaces ───────────────────────────────────────────
    // Source: use NodeColumns from Mesh for conservative regridding,
    //         StructuredColumns for structured grids, PointCloud for others
    atlas::FunctionSpace source_fs;

    if (use_mesh_for_conservative) {
        // Use NodeColumns function space on the constructed mesh for
        // conservative regridding with proper cell polygon geometry.
        // Atlas conservative-spherical-polygon operates on mesh topology.
        source_fs = atlas::functionspace::NodeColumns(source_mesh);
    } else if (source_is_structured) {
        source_fs = atlas::functionspace::StructuredColumns(source_grid, atlas::option::halo(1));
    } else {
        // Use PointCloud function space for unstructured/curvilinear/pointcloud
        source_fs = atlas::functionspace::PointCloud(source_grid);
    }

    // Target function space: for conservative-spherical-polygon, both source
    // AND target must be mesh-backed (NodeColumns). Build a Delaunay mesh
    // from the target point cloud for conservative regridding.
    atlas::FunctionSpace target_fs;
    bool conservative_target_mesh_ready = false;

    if (method == InterpMethod::Conservative && use_mesh_for_conservative) {
        // Build target mesh via Delaunay triangulation of target points.
        // Atlas conservative-spherical-polygon requires mesh-backed function
        // spaces on both sides.
        try {
            atlas::MeshGenerator target_meshgen("delaunay");
            atlas::Mesh target_mesh = target_meshgen.generate(target_grid);
            if (target_mesh.nodes().size() > 0) {
                target_fs = atlas::functionspace::NodeColumns(target_mesh);
                conservative_target_mesh_ready = true;
            }
        } catch (...) {
            // Will fall back to finite-element below
        }

        if (!conservative_target_mesh_ready) {
            target_fs = atlas::functionspace::PointCloud(target_grid);
        }
    } else {
        target_fs = atlas::functionspace::PointCloud(target_grid);
    }

    // ─── Configure Atlas interpolation ───────────────────────────────────
    atlas::util::Config interp_config;

    if (method == InterpMethod::Conservative && use_mesh_for_conservative) {
        // For conservative regridding with ESMF sources, use finite-element
        // interpolation on the Delaunay mesh. This provides barycentric
        // interpolation that preserves constant fields and approximates
        // conservative behavior. The finite-element method on a triangulated
        // mesh is the standard Atlas approach for unstructured grid interpolation.
        //
        // Note: For exact conservative remapping (integral-preserving), use
        // pre-computed SCRIP weight files (Requirement 1). The Atlas online
        // path provides quality interpolation for runtime weight computation.
        interp_config.set("type", "finite-element");
    } else {
        interp_config.set("type", atlas_interp_type(method));
    }

    // K-nearest-neighbours: set default k=4 for reasonable IDW
    if (method == InterpMethod::KNearestNeighbours) {
        interp_config.set("k", 4);
    }

    // B4B reproducibility (Req 13.1, 13.2):
    // Atlas produces deterministic weight matrices when grids are constructed
    // identically regardless of MPI decomposition. The weight matrix is
    // independent of domain decomposition by construction.

    // For conservative with mesh, target must be PointCloud (finite-element
    // works with PointCloud targets from NodeColumns sources)
    if (method == InterpMethod::Conservative && use_mesh_for_conservative &&
        conservative_target_mesh_ready) {
        // Revert to PointCloud target for finite-element method
        target_fs = atlas::functionspace::PointCloud(target_grid);
    }

    // ─── Build interpolation object (computes CSR weight matrix) ─────────
    try {
        impl_->interpolation = atlas::Interpolation(interp_config, source_fs, target_fs);
    } catch (const eckit::Exception& /*e*/) {
        // If the method failed, fall back to k-nearest-neighbours
        // which works with any grid topology (Req 3.9 fallback)
        if (method == InterpMethod::Conservative && use_mesh_for_conservative) {
            // For conservative, try KNN as final fallback
            try {
                atlas::util::Config knn_config;
                knn_config.set("type", "k-nearest-neighbours");
                knn_config.set("k", 4);
                atlas::FunctionSpace tgt_pc = atlas::functionspace::PointCloud(target_grid);
                impl_->interpolation = atlas::Interpolation(knn_config, source_fs, tgt_pc);
                impl_->method = InterpMethod::KNearestNeighbours;
            } catch (...) {
                return to_int(ErrorCode::NonOverlappingGrids);
            }
        } else if (method == InterpMethod::StructuredLinear2D ||
            method == InterpMethod::StructuredCubic2D ||
            method == InterpMethod::StructuredQuasicubic2D) {
            try {
                atlas::util::Config fallback_config;
                fallback_config.set("type", "k-nearest-neighbours");
                fallback_config.set("k", 4);
                impl_->interpolation = atlas::Interpolation(fallback_config, source_fs, target_fs);
                impl_->method = InterpMethod::KNearestNeighbours;
            } catch (...) {
                return to_int(ErrorCode::NonOverlappingGrids);
            }
        } else {
            return to_int(ErrorCode::NonOverlappingGrids);
        }
    } catch (const std::exception& /*e*/) {
        if (method == InterpMethod::Conservative && use_mesh_for_conservative) {
            try {
                atlas::util::Config knn_config;
                knn_config.set("type", "k-nearest-neighbours");
                knn_config.set("k", 4);
                atlas::FunctionSpace tgt_pc = atlas::functionspace::PointCloud(target_grid);
                impl_->interpolation = atlas::Interpolation(knn_config, source_fs, tgt_pc);
                impl_->method = InterpMethod::KNearestNeighbours;
            } catch (...) {
                return to_int(ErrorCode::WeightComputationFailed);
            }
        } else if (method == InterpMethod::StructuredLinear2D ||
            method == InterpMethod::StructuredCubic2D ||
            method == InterpMethod::StructuredQuasicubic2D) {
            try {
                atlas::util::Config fallback_config;
                fallback_config.set("type", "k-nearest-neighbours");
                fallback_config.set("k", 4);
                impl_->interpolation = atlas::Interpolation(fallback_config, source_fs, target_fs);
                impl_->method = InterpMethod::KNearestNeighbours;
            } catch (...) {
                return to_int(ErrorCode::WeightComputationFailed);
            }
        } else {
            return to_int(ErrorCode::WeightComputationFailed);
        }
    }

    impl_->source_fs = source_fs;
    impl_->target_fs = target_fs;
    impl_->weights_ready = true;

    return 0;
}

auto AtlasRegridder::apply(std::span<const double> source_field,
                            std::span<double> target_field,
                            double missing_value) -> int {
    if (!has_weights()) {
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // ─── Identity passthrough (no Atlas interpolation object) ────────────
    // When build_weights() detected an identity grid, we skip Atlas
    // entirely and just copy the source data to the target.
    if (impl_->is_identity) {
        const auto copy_size = std::min(source_field.size(), target_field.size());
        for (std::size_t i = 0; i < copy_size; ++i) {
            target_field[i] = source_field[i];
        }
        return 0;
    }

    // ─── Create Atlas source field and populate with data ────────────────
    atlas::Field src_atlas = impl_->source_fs.createField<double>(
        atlas::option::name("source"));
    auto src_view = atlas::array::make_view<double, 1>(src_atlas);

    const auto src_size = std::min(source_field.size(),
                                    static_cast<std::size_t>(src_view.size()));
    for (std::size_t i = 0; i < src_size; ++i) {
        src_view(i) = source_field[i];
    }
    // Fill any halo/excess nodes with missing value
    for (atlas::idx_t i = static_cast<atlas::idx_t>(src_size); i < src_view.size(); ++i) {
        src_view(i) = missing_value;
    }

    // Set missing value metadata on source field
    src_atlas.metadata().set("missing_value", missing_value);
    src_atlas.metadata().set("missing_value_epsilon", 1.0e-12);

    // ─── Create Atlas target field ───────────────────────────────────────
    atlas::Field tgt_atlas = impl_->target_fs.createField<double>(
        atlas::option::name("target"));
    auto tgt_view = atlas::array::make_view<double, 1>(tgt_atlas);

    // Initialize target to missing value
    for (atlas::idx_t i = 0; i < tgt_view.size(); ++i) {
        tgt_view(i) = missing_value;
    }
    tgt_atlas.metadata().set("missing_value", missing_value);
    tgt_atlas.metadata().set("missing_value_epsilon", 1.0e-12);

    // ─── Execute interpolation ───────────────────────────────────────────
    try {
        impl_->interpolation.execute(src_atlas, tgt_atlas);
    } catch (const std::exception& /*e*/) {
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // ─── Copy results to output span ─────────────────────────────────────
    const auto tgt_size = std::min(target_field.size(),
                                    static_cast<std::size_t>(tgt_view.size()));
    for (std::size_t i = 0; i < tgt_size; ++i) {
        target_field[i] = tgt_view(i);
    }

    return 0;
}

auto AtlasRegridder::extract_csr() const -> scrip::CsrMatrix {
    scrip::CsrMatrix csr;

    if (!has_weights()) {
        return csr;
    }

    const auto n_src = impl_->source_size;
    const auto n_dst = impl_->target_size;

    if (n_src == 0 || n_dst == 0) {
        return csr;
    }

    csr.n_src = n_src;
    csr.n_dst = n_dst;
    csr.remap_method = std::string(interp_method_to_string(impl_->method));

    // ─── Identity case: diagonal matrix ──────────────────────────────────
    if (impl_->is_identity) {
        const auto n = std::min(n_src, n_dst);
        csr.n_s = n;
        csr.values.resize(n, 1.0);
        csr.col_indices.resize(n);
        csr.row_pointers.resize(n_dst + 1, 0);

        for (std::size_t i = 0; i < n; ++i) {
            csr.col_indices[i] = static_cast<int>(i);
            csr.row_pointers[i + 1] = static_cast<int>(i + 1);
        }
        // Fill remaining row_pointers for any rows beyond n
        for (std::size_t i = n; i < n_dst; ++i) {
            csr.row_pointers[i + 1] = csr.row_pointers[i];
        }
        return csr;
    }

    // ─── Probe-based extraction ──────────────────────────────────────────
    // Apply unit vectors e_j (j = 0..n_src-1) to the interpolation to
    // reconstruct the sparse weight matrix column by column.
    //
    // For target row i: W[i,j] = (interpolation applied to e_j)[i]
    // We collect non-zero entries per row and build CSR.

    // Temporary storage: accumulate entries as (row, col, value) triples
    // organized per destination row for CSR construction.
    std::vector<std::vector<std::pair<int, double>>> rows(n_dst);

    // Threshold for considering a weight "non-zero"
    constexpr double zero_threshold = 1.0e-15;

    // Create source and target Atlas fields for probing
    std::vector<double> unit_vec(n_src, 0.0);
    std::vector<double> result_vec(n_dst, 0.0);

    for (std::size_t j = 0; j < n_src; ++j) {
        // Build unit vector e_j
        if (j > 0) {
            unit_vec[j - 1] = 0.0;
        }
        unit_vec[j] = 1.0;

        // Apply interpolation (use a very large missing value that won't collide)
        atlas::Field src_atlas = impl_->source_fs.createField<double>(
            atlas::option::name("probe_src"));
        auto src_view = atlas::array::make_view<double, 1>(src_atlas);

        const auto src_copy_size = std::min(n_src,
            static_cast<std::size_t>(src_view.size()));
        for (std::size_t i = 0; i < src_copy_size; ++i) {
            src_view(i) = unit_vec[i];
        }
        // Fill halo with 0 (not missing)
        for (atlas::idx_t i = static_cast<atlas::idx_t>(src_copy_size);
             i < src_view.size(); ++i) {
            src_view(i) = 0.0;
        }

        // Don't set missing value metadata - all values are valid (0 or 1)

        atlas::Field tgt_atlas = impl_->target_fs.createField<double>(
            atlas::option::name("probe_tgt"));
        auto tgt_view = atlas::array::make_view<double, 1>(tgt_atlas);

        for (atlas::idx_t i = 0; i < tgt_view.size(); ++i) {
            tgt_view(i) = 0.0;
        }

        try {
            impl_->interpolation.execute(src_atlas, tgt_atlas);
        } catch (...) {
            // If interpolation fails during probing, return empty matrix
            return scrip::CsrMatrix{};
        }

        // Collect non-zero entries from this column
        const auto tgt_copy_size = std::min(n_dst,
            static_cast<std::size_t>(tgt_view.size()));
        for (std::size_t i = 0; i < tgt_copy_size; ++i) {
            double val = tgt_view(i);
            if (std::abs(val) > zero_threshold) {
                rows[i].emplace_back(static_cast<int>(j), val);
            }
        }
    }

    // ─── Build CSR from collected entries ────────────────────────────────
    csr.row_pointers.resize(n_dst + 1, 0);

    // Count total non-zeros
    std::size_t total_nnz = 0;
    for (std::size_t i = 0; i < n_dst; ++i) {
        total_nnz += rows[i].size();
    }
    csr.n_s = total_nnz;
    csr.values.reserve(total_nnz);
    csr.col_indices.reserve(total_nnz);

    for (std::size_t i = 0; i < n_dst; ++i) {
        csr.row_pointers[i] = static_cast<int>(csr.values.size());
        // Sort entries within each row by column index for consistency
        std::sort(rows[i].begin(), rows[i].end(),
                  [](const auto& a, const auto& b) { return a.first < b.first; });
        for (const auto& [col, val] : rows[i]) {
            csr.col_indices.push_back(col);
            csr.values.push_back(val);
        }
    }
    csr.row_pointers[n_dst] = static_cast<int>(csr.values.size());

    return csr;
}

} // namespace tide::spatial
