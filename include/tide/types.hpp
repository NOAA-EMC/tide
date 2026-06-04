/**
 * @file types.hpp
 * @brief Core data types for the TIDE library.
 *
 * Defines the fundamental structures used throughout the TIDE pipeline:
 * - SourceGrid: grid metadata extracted from input files
 * - TargetGrid: host model grid specification
 * - Error: structured error information
 * - FieldBuffer: contiguous storage with mdspan view accessors
 */

#ifndef TIDE_TYPES_HPP
#define TIDE_TYPES_HPP

#include <cstddef>
#include <version>
#if defined(__cpp_lib_mdspan) && __cpp_lib_mdspan >= 202207L
#include <mdspan>
#elif __has_include("atlas/util/mdspan.h")
// Use Atlas's bundled mdspan implementation when available.
// Atlas defines mdspan in the 'atlas' namespace, so we pull it into std.
#include "atlas/util/mdspan.h"
namespace std {
using atlas::mdspan;
using atlas::extents;
using atlas::dextents;
using atlas::layout_left;
using atlas::layout_right;
}
#elif __has_include(<experimental/mdspan>)
#include <experimental/mdspan>
namespace std {
using std::experimental::mdspan;
using std::experimental::extents;
using std::experimental::dextents;
using std::experimental::layout_left;
using std::experimental::layout_right;
}
#endif
#include <string>
#include <vector>

namespace tide {

/**
 * @brief Source grid metadata extracted from a forcing data file.
 *
 * Represents the native grid of an input data file, including coordinate
 * arrays, cell boundary arrays, grid topology type, and the metadata
 * convention used for discovery (CF, COARDS, or UGRID).
 *
 * The grid type determines how Atlas constructs the interpolation mesh:
 * - RegularLatLon: structured latitude-longitude grid
 * - Gaussian: full Gaussian grid (regular in longitude, Gaussian in latitude)
 * - ReducedGaussian: reduced Gaussian grid with variable points per latitude
 * - Curvilinear: CF-convention with 2D latitude/longitude coordinate arrays
 * - Unstructured: arbitrary unstructured mesh (UGRID convention)
 * - PointCloud: discrete geographic points (e.g., point-source emissions)
 */
struct SourceGrid {
    /**
     * @brief Enumeration of supported source grid topologies.
     */
    enum class Type {
        RegularLatLon,    ///< Structured latitude-longitude grid
        Gaussian,         ///< Full Gaussian grid
        ReducedGaussian,  ///< Reduced Gaussian grid
        Curvilinear,      ///< CF-convention with 2D lat/lon coordinate arrays
        Unstructured,     ///< Unstructured mesh (UGRID)
        PointCloud        ///< Discrete geographic points mapped to atlas::PointCloud
    };

    /// @brief Grid topology type.
    Type type{};

    /// @brief Total number of horizontal grid cells.
    std::size_t num_cells{0};

    /// @brief Latitude coordinates (degrees North) for each grid cell.
    std::vector<double> lats;

    /// @brief Longitude coordinates (degrees East) for each grid cell.
    std::vector<double> lons;

    /// @brief Cell latitude boundaries for conservative regridding.
    ///
    /// For rectilinear grids, contains the cell edge latitudes.
    /// May be populated from file metadata or inferred via midpoint
    /// extrapolation when absent.
    std::vector<double> lat_bounds;

    /// @brief Cell longitude boundaries for conservative regridding.
    ///
    /// For rectilinear grids, contains the cell edge longitudes.
    /// May be populated from file metadata or inferred via midpoint
    /// extrapolation when absent.
    std::vector<double> lon_bounds;

    /// @brief Vertical level coordinates (pressure in Pa or height in m).
    std::vector<double> levels;

    /// @brief Units of the vertical coordinate: "Pa" (pressure) or "m" (height).
    std::string level_units;

    /// @brief Metadata convention used to discover grid info: "CF", "COARDS", or "UGRID".
    std::string metadata_convention;

    /// @brief True if cell bounds were dynamically inferred via midpoint extrapolation.
    ///
    /// When a rectilinear source grid lacks explicit cell-boundary (bounds)
    /// variables, TIDE infers cell edges from coordinate arrays. This flag
    /// indicates that the lat_bounds and lon_bounds were computed rather than
    /// read directly from file metadata.
    bool bounds_inferred{false};
};

/**
 * @brief Target grid specification provided by the host model.
 *
 * Defines the grid onto which forcing data is regridded. The host model
 * supplies coordinate arrays and dimension sizes; TIDE uses these to
 * compute Atlas remapping weights during initialization.
 *
 * Target grids can also be defined via ESMF mesh or grid spec files,
 * in which case the esmf_mesh or esmf_grid_spec path is populated
 * and coordinates are loaded from the file at initialization time.
 */
struct TargetGrid {
    /// @brief Number of horizontal columns (grid points) on the target grid.
    std::size_t num_cols{0};

    /// @brief Number of vertical levels on the target grid.
    std::size_t num_levels{0};

    /// @brief Latitude coordinates (degrees North) for each target column.
    std::vector<double> lats;

    /// @brief Longitude coordinates (degrees East) for each target column.
    std::vector<double> lons;

    /// @brief Vertical level coordinates on the target grid.
    std::vector<double> levels;

    /// @brief Units of the vertical coordinate: "Pa" (pressure) or "m" (height).
    std::string level_units;

    /// @brief Optional path to an ESMF mesh file defining this target grid.
    ///
    /// When specified, the target grid geometry is loaded from this
    /// unstructured mesh file (nodeCoords, elementConn, numElementConn).
    std::string esmf_mesh;

    /// @brief Optional path to an ESMF grid spec file defining this target grid.
    ///
    /// When specified, the target grid geometry is loaded from this
    /// structured grid spec file (grid_center_lat/lon, grid_corner_lat/lon).
    std::string esmf_grid_spec;
};

/**
 * @brief Structured error information for the TIDE library.
 *
 * Carries a numeric error code, a human-readable message, and the name
 * of the component (context) that generated the error. Used as the error
 * type in std::expected return values throughout the C++ API.
 *
 * Error codes are defined in @ref ErrorCode (see error.hpp) and organized
 * into category ranges for quick identification of the originating subsystem.
 */
struct Error {
    /// @brief Non-zero error code (see ErrorCode enum in error.hpp).
    int code{0};

    /// @brief Human-readable description of the error.
    std::string message;

    /// @brief Component or subsystem that generated the error (e.g., "io", "config").
    std::string context;
};

/**
 * @brief Contiguous field buffer with mdspan view accessors.
 *
 * Stores pipeline output as a flat contiguous array and provides
 * zero-copy mdspan views in both C-order (row-major, layout_right)
 * and Fortran-order (column-major, layout_left).
 *
 * The 3D logical shape is (ncols x nlevels x nfields), where:
 * - ncols:   number of horizontal columns on the target grid
 * - nlevels: number of vertical levels (1 for 2D surface fields)
 * - nfields: number of fields stored in this buffer (typically 1 per stream)
 *
 * @note Memory ownership is held by this struct via the `data` vector.
 *       The mdspan views are non-owning and valid only while this struct
 *       (and its `data` member) remain alive and unmodified.
 */
struct FieldBuffer {
    /// @brief Contiguous storage for all field elements.
    std::vector<double> data;

    /// @brief Number of horizontal columns (first dimension).
    std::size_t ncols{0};

    /// @brief Number of vertical levels (second dimension, 1 for 2D fields).
    std::size_t nlevels{0};

    /// @brief Number of fields (third dimension, typically 1 per stream).
    std::size_t nfields{0};

    /**
     * @brief Get a 3D mdspan view in C-order (row-major / layout_right).
     *
     * The view has shape (ncols, nlevels, nfields) with the last index
     * (nfields) varying fastest in memory.
     *
     * @return A read-only mdspan over the buffer data.
     *
     * @pre data.size() == ncols * nlevels * nfields
     */
    [[nodiscard]] auto view_3d() const {
        return std::mdspan<const double, std::dextents<std::size_t, 3>>(
            data.data(),
            std::dextents<std::size_t, 3>{ncols, nlevels, nfields});
    }

    /**
     * @brief Get a mutable 3D mdspan view in C-order (row-major / layout_right).
     *
     * The view has shape (ncols, nlevels, nfields) with the last index
     * (nfields) varying fastest in memory.
     *
     * @return A mutable mdspan over the buffer data.
     *
     * @pre data.size() == ncols * nlevels * nfields
     */
    [[nodiscard]] auto view_3d_mut() {
        return std::mdspan<double, std::dextents<std::size_t, 3>>(
            data.data(),
            std::dextents<std::size_t, 3>{ncols, nlevels, nfields});
    }

    /**
     * @brief Get a 3D mdspan view in Fortran-order (column-major / layout_left).
     *
     * The view has shape (ncols, nlevels, nfields) with the first index
     * (ncols) varying fastest in memory — matching Fortran array storage.
     *
     * @return A read-only column-major mdspan over the buffer data.
     *
     * @pre data.size() == ncols * nlevels * nfields
     */
    [[nodiscard]] auto view_3d_fortran() const {
        return std::mdspan<const double, std::dextents<std::size_t, 3>,
                           std::layout_left>(
            data.data(),
            std::dextents<std::size_t, 3>{ncols, nlevels, nfields});
    }

    /**
     * @brief Get a mutable 3D Fortran-order view.
     *
     * @return A mutable column-major mdspan over the buffer data.
     *
     * @pre data.size() == ncols * nlevels * nfields
     */
    [[nodiscard]] auto view_3d_fortran_mut() {
        return std::mdspan<double, std::dextents<std::size_t, 3>,
                           std::layout_left>(
            data.data(),
            std::dextents<std::size_t, 3>{ncols, nlevels, nfields});
    }

    /**
     * @brief Resize the buffer to match the specified dimensions.
     *
     * Allocates (or reallocates) the data vector to hold
     * ncols * nlevels * nfields elements, initialised to zero.
     *
     * @param cols   Number of horizontal columns.
     * @param levels Number of vertical levels.
     * @param fields Number of fields.
     */
    void resize(std::size_t cols, std::size_t levels, std::size_t fields) {
        ncols = cols;
        nlevels = levels;
        nfields = fields;
        data.assign(ncols * nlevels * nfields, 0.0);
    }

    /**
     * @brief Get the total number of elements in the buffer.
     * @return ncols * nlevels * nfields
     */
    [[nodiscard]] std::size_t size() const noexcept {
        return ncols * nlevels * nfields;
    }

    /**
     * @brief Check if the buffer is empty (no data allocated).
     * @return true if data vector is empty.
     */
    [[nodiscard]] bool empty() const noexcept { return data.empty(); }
};

} // namespace tide

#endif // TIDE_TYPES_HPP
