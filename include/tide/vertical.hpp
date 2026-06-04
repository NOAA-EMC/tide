/**
 * @file vertical.hpp
 * @brief TSPACK-based vertical interpolation engine for the TIDE library.
 *
 * Provides column-by-column vertical interpolation using tension splines
 * from the vendored TSPACK library. Supports monotonicity preservation,
 * missing value handling, boundary-gradient extrapolation, and optional
 * log-pressure coordinate transformation.
 */

#ifndef TIDE_VERTICAL_HPP
#define TIDE_VERTICAL_HPP

#include <cstddef>
#include <limits>
#include <span>

namespace tide::vertical {

/**
 * @brief Configuration for vertical interpolation.
 *
 * Controls the behavior of the TSPACK interpolation engine including
 * tolerance thresholds, extrapolation limits, and coordinate transforms.
 */
struct VerticalConfig {
    /// @brief Maximum absolute difference between source and target levels
    ///        below which interpolation is skipped (passthrough).
    double level_tolerance = 1.0e-10;

    /// @brief Maximum extrapolation distance beyond the source level range.
    ///        -1 means one source-level interval beyond the boundary (default).
    ///        0 disables extrapolation entirely.
    ///        Positive values specify absolute distance in coordinate units.
    double max_extrap_distance = -1.0;

    /// @brief Minimum number of valid (non-missing) source levels required
    ///        for interpolation. If fewer are available, the target column
    ///        is filled with missing values.
    int min_valid_levels = 3;

    /// @brief When true, perform spline fitting in ln(P) space for
    ///        pressure coordinates. Improves accuracy for exponentially
    ///        distributed pressure levels.
    bool log_pressure = false;
};

/**
 * @brief TSPACK-based vertical interpolation engine.
 *
 * Performs monotonicity-preserving tension spline interpolation on
 * vertical columns. Handles missing values by interpolating only valid
 * levels, supports boundary-gradient extrapolation, and can optionally
 * operate in log-pressure space for pressure-level data.
 *
 * Thread safety: Each TspackInterpolator instance is independent and
 * may be used concurrently from different threads without synchronization.
 * However, a single instance must not be shared across threads without
 * external locking (due to internal workspace buffers).
 */
class TspackInterpolator {
public:
    /**
     * @brief Construct with the given configuration.
     * @param config Vertical interpolation parameters.
     */
    explicit TspackInterpolator(const VerticalConfig& config = {});

    /**
     * @brief Interpolate a single vertical column.
     *
     * Fits a monotonicity-preserving tension spline through the valid
     * source levels and evaluates it at each target level. Source levels
     * must be in strictly ascending or strictly descending order (they
     * will be internally sorted to ascending if needed).
     *
     * @param src_levels Source vertical coordinates (pressure Pa or height m).
     * @param src_values Source field values at those levels.
     * @param tgt_levels Target vertical coordinates.
     * @param tgt_values Output interpolated values (pre-allocated, same size as tgt_levels).
     * @param missing_value Sentinel value indicating missing data.
     * @return 0 on success, positive value = number of target levels marked missing.
     */
    auto interpolate_column(std::span<const double> src_levels,
                            std::span<const double> src_values,
                            std::span<const double> tgt_levels,
                            std::span<double> tgt_values,
                            double missing_value = std::numeric_limits<double>::max()) -> int;

    /**
     * @brief Interpolate a full 3D field column-by-column.
     *
     * Applies interpolate_column() independently to each of the ncols
     * horizontal columns. The source field is stored as ncols contiguous
     * columns of nlevels_src elements each (column-major within each column).
     *
     * @param src_levels Source levels (nlevels_src elements).
     * @param src_field  Source field data (ncols × nlevels_src, row-major by column index).
     * @param tgt_levels Target levels (nlevels_tgt elements).
     * @param tgt_field  Output field data (ncols × nlevels_tgt, pre-allocated).
     * @param ncols      Number of horizontal columns.
     * @return 0 on success.
     */
    auto interpolate_field(std::span<const double> src_levels,
                           std::span<const double> src_field,
                           std::span<const double> tgt_levels,
                           std::span<double> tgt_field,
                           std::size_t ncols) -> int;

private:
    VerticalConfig config_;
};

} // namespace tide::vertical

#endif // TIDE_VERTICAL_HPP
