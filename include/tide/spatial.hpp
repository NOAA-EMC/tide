/**
 * @file spatial.hpp
 * @brief TIDE Atlas Spatial Regridding Engine — cached horizontal regridding.
 *
 * Provides the AtlasRegridder class which computes and caches remapping
 * weights using ECMWF Atlas interpolation methods, then applies them to
 * source fields to produce target-grid fields.
 *
 * Supported interpolation methods:
 * - finite-element (barycentric on triangulation)
 * - k-nearest-neighbours (inverse-distance weighted)
 * - nearest-neighbour (single closest point)
 * - structured-linear2D (bilinear on structured grids)
 * - structured-cubic2D (16-point stencil)
 * - structured-quasicubic2D (12-point stencil)
 * - conservative (area-intersection preserving integrals)
 *
 * Supported source grid types:
 * - Regular lat-lon, Gaussian, Reduced Gaussian
 * - Curvilinear (CF 2D coordinate arrays → quadrilateral Mesh)
 * - Unstructured (UGRID → explicit Mesh)
 * - PointCloud (discrete points → KD-Tree assignment)
 *
 * Missing data handling uses Atlas non-linear modes:
 * - missing-if-any-missing
 * - missing-if-all-missing
 * - missing-if-heaviest-missing (default)
 *
 * Weight matrices are cached as CSR (Compressed Sparse Row) and reused
 * across time steps for the same source+target+method combination.
 *
 * All Atlas operations are configured for deterministic (B4B reproducible)
 * weight computation independent of MPI decomposition.
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 300 (ErrorCode::WeightComputationFailed): Atlas failed to compute weights
 * - 301 (ErrorCode::NonOverlappingGrids): Source and target grids do not overlap
 *
 * Validates Requirements: 3.1, 3.2, 3.3, 3.4, 3.5, 3.6, 3.7, 3.8, 3.9,
 *                         3.10, 3.11, 3.12, 13.1, 13.2
 */

#ifndef TIDE_SPATIAL_HPP
#define TIDE_SPATIAL_HPP

#include <cstddef>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <string_view>

#include <tide/scrip.hpp>
#include <tide/types.hpp>

namespace tide::spatial {

/**
 * @brief Available Atlas interpolation methods.
 *
 * Each method maps to a specific Atlas interpolation scheme with its own
 * accuracy and performance characteristics. The choice of method depends
 * on the field type: conservative for mass/energy quantities, bilinear
 * for smooth fields, nearest-neighbour for categorical data.
 */
enum class InterpMethod {
    FiniteElement,          ///< Barycentric coordinates on mesh triangulation
    KNearestNeighbours,    ///< Inverse-distance weighted k nearest points
    NearestNeighbour,      ///< Single nearest point (k=1)
    StructuredLinear2D,    ///< Bilinear on structured grids
    StructuredCubic2D,     ///< Fully cubic on structured grids (16-point stencil)
    StructuredQuasicubic2D,///< Quasi-cubic on structured grids (12-point stencil)
    Conservative           ///< Area-intersection conservative remapping
};

/**
 * @brief Non-linear missing data treatment modes for Atlas interpolation.
 *
 * Controls how missing/fill values in source fields affect the interpolated
 * output. These correspond to Atlas's non-linear weight adjustment modes.
 */
enum class MissingDataMode {
    MissingIfAnyMissing,       ///< Output missing if any input point is missing
    MissingIfAllMissing,       ///< Output missing only if all input points are missing
    MissingIfHeaviestMissing   ///< Output missing if highest-weight input is missing (default)
};

/**
 * @brief Convert a string configuration value to an InterpMethod enum.
 *
 * Accepted string values (case-sensitive):
 * - "finite-element"
 * - "k-nearest-neighbours"
 * - "nearest-neighbour"
 * - "structured-linear2D"
 * - "structured-cubic2D"
 * - "structured-quasicubic2D"
 * - "conservative"
 *
 * @param name Configuration string for the interpolation method.
 * @return The corresponding InterpMethod, or FiniteElement if unrecognized.
 */
[[nodiscard]] InterpMethod interp_method_from_string(std::string_view name) noexcept;

/**
 * @brief Convert an InterpMethod enum to its string representation.
 *
 * @param method The interpolation method enum value.
 * @return String representation matching configuration file format.
 */
[[nodiscard]] std::string_view interp_method_to_string(InterpMethod method) noexcept;

/**
 * @brief Convert a string configuration value to a MissingDataMode enum.
 *
 * Accepted string values (case-sensitive):
 * - "missing-if-any-missing"
 * - "missing-if-all-missing"
 * - "missing-if-heaviest-missing"
 *
 * @param name Configuration string for the missing data mode.
 * @return The corresponding MissingDataMode, or MissingIfHeaviestMissing if unrecognized.
 */
[[nodiscard]] MissingDataMode missing_mode_from_string(std::string_view name) noexcept;

/**
 * @brief Cached horizontal regridding via ECMWF Atlas.
 *
 * The AtlasRegridder computes interpolation weights during initialization
 * (build_weights) and caches them as a sparse (CSR) matrix for efficient
 * reuse across multiple time steps. The apply() method performs the sparse
 * matrix-vector multiplication to produce target-grid fields.
 *
 * Key features:
 * - Supports all major Atlas interpolation methods
 * - Handles curvilinear grids by constructing quadrilateral meshes
 * - Handles PointCloud sources via KD-Tree point search
 * - Infers cell bounds via midpoint extrapolation when absent
 * - Configures Atlas for B4B reproducible weight computation
 *
 * @note Thread safety: a single AtlasRegridder instance must not be used
 *       concurrently from multiple threads. Each stream should own its own
 *       AtlasRegridder instance (achieved via stream isolation).
 */
class AtlasRegridder {
public:
    /**
     * @brief Construct an empty regridder (no weights computed).
     */
    AtlasRegridder();

    /**
     * @brief Destructor — releases Atlas resources.
     */
    ~AtlasRegridder();

    /// @brief Move constructor.
    AtlasRegridder(AtlasRegridder&&) noexcept;

    /// @brief Move assignment.
    AtlasRegridder& operator=(AtlasRegridder&&) noexcept;

    // Non-copyable (Atlas objects are heavyweight)
    AtlasRegridder(const AtlasRegridder&) = delete;
    AtlasRegridder& operator=(const AtlasRegridder&) = delete;

    /**
     * @brief Build and cache remapping weights for the specified method.
     *
     * Computes the interpolation weight matrix using Atlas for the given
     * source and target grid pair. The weights are cached internally for
     * reuse by apply().
     *
     * For curvilinear grids, the source is converted to an atlas::Mesh
     * constructed from quadrilateral cell vertices.
     *
     * For PointCloud sources, uses atlas::PointCloud and KD-Tree point
     * search for discrete point-to-cell assignment.
     *
     * For conservative regridding on rectilinear grids lacking explicit
     * bounds variables, cell edges are inferred via midpoint extrapolation.
     *
     * Atlas is configured for deterministic (B4B reproducible) weight
     * computation that is independent of MPI decomposition.
     *
     * @param source       Source grid specification.
     * @param target       Target grid specification.
     * @param method       Interpolation method to use (default: FiniteElement).
     * @param missing_mode How to handle missing values (default: MissingIfHeaviestMissing).
     * @return 0 on success, 300 on weight computation failure,
     *         301 if grids do not overlap.
     *
     * @pre source.num_cells > 0 and target.num_cols > 0
     * @post has_weights() returns true on success
     */
    auto build_weights(const SourceGrid& source,
                       const TargetGrid& target,
                       InterpMethod method = InterpMethod::FiniteElement,
                       MissingDataMode missing_mode = MissingDataMode::MissingIfHeaviestMissing) -> int;

    /**
     * @brief Apply cached weights to a source field.
     *
     * Performs sparse matrix-vector multiplication using the cached weight
     * matrix to interpolate the source field onto the target grid.
     *
     * Missing values in the source field are handled according to the mode
     * specified during build_weights().
     *
     * @param source_field Input field on source grid (num_cells elements).
     * @param target_field Output field on target grid (num_cols elements, pre-allocated).
     * @param missing_value Sentinel value indicating missing data in source
     *                      (default: std::numeric_limits<double>::max()).
     * @return 0 on success, non-zero if weights have not been computed.
     *
     * @pre has_weights() == true
     * @pre source_field.size() == source grid num_cells from build_weights()
     * @pre target_field.size() == target grid num_cols from build_weights()
     */
    auto apply(std::span<const double> source_field,
               std::span<double> target_field,
               double missing_value = std::numeric_limits<double>::max()) -> int;

    /**
     * @brief Check if interpolation weights have been computed.
     * @return true if build_weights() has been called successfully.
     */
    [[nodiscard]] bool has_weights() const noexcept;

    /**
     * @brief Get the interpolation method used for the cached weights.
     * @return The InterpMethod used in the last successful build_weights() call.
     * @pre has_weights() == true
     */
    [[nodiscard]] InterpMethod method() const noexcept;

    /**
     * @brief Get the number of source grid cells for the cached weights.
     * @return Number of source cells, or 0 if no weights computed.
     */
    [[nodiscard]] std::size_t source_size() const noexcept;

    /**
     * @brief Get the number of target grid columns for the cached weights.
     * @return Number of target columns, or 0 if no weights computed.
     */
    [[nodiscard]] std::size_t target_size() const noexcept;

    /**
     * @brief Extract the internal weight matrix as a CSR matrix for export.
     *
     * Reconstructs the sparse weight matrix by probing the Atlas interpolation
     * with unit basis vectors (one per source cell). For each unit vector e_j,
     * the interpolation produces a column of the weight matrix, from which
     * non-zero entries are collected to build the CSR representation.
     *
     * This operation is expensive (O(n_src × cost_of_apply)) and should only
     * be called when the weights need to be exported. Results are NOT cached
     * internally — the caller should cache the returned matrix if needed.
     *
     * For identity regridding (source == target), returns an identity CSR
     * matrix with one non-zero per row.
     *
     * @return CsrMatrix on success, or an empty matrix if weights not computed.
     *
     * @pre has_weights() == true
     */
    [[nodiscard]] auto extract_csr() const -> scrip::CsrMatrix;

private:
    /// @brief PImpl for Atlas internals (avoids Atlas headers in public API).
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * @brief Infer cell bounds via midpoint extrapolation from 1D coordinate arrays.
 *
 * When a rectilinear grid lacks explicit cell-boundary (bounds) variables,
 * this function computes cell edges from coordinate centres. Interior edges
 * are placed at midpoints between adjacent centres; boundary edges are
 * extrapolated by half-interval from the outermost centre.
 *
 * @param coords 1D array of cell-centre coordinates (ascending or descending).
 * @return Vector of cell boundary values (size = coords.size() + 1).
 *
 * @pre coords.size() >= 2
 * @post result.size() == coords.size() + 1
 * @post For ascending coords: result[i] < coords[i] < result[i+1]
 */
[[nodiscard]] std::vector<double> infer_cell_bounds(std::span<const double> coords);

} // namespace tide::spatial

#endif // TIDE_SPATIAL_HPP
