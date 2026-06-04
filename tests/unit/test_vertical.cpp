/**
 * @file test_vertical.cpp
 * @brief Property-based tests for the TIDE TSPACK vertical interpolation engine.
 *
 * Validates Requirements: 4.1, 4.2, 4.3, 4.5, 4.7
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <tide/vertical.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstring>
#include <limits>
#include <numeric>
#include <span>
#include <vector>

namespace tide::vertical {
namespace {

// =============================================================================
// Helper generators
// =============================================================================

/// Generate a sorted vector of unique levels (strictly increasing) in a
/// reasonable range. Levels represent pressure (Pa) or height (m).
static rc::Gen<std::vector<double>> genSortedLevels(int minSize, int maxSize) {
    return rc::gen::mapcat(
        rc::gen::inRange(minSize, maxSize + 1),
        [](int n) {
            return rc::gen::map(
                rc::gen::container<std::vector<double>>(
                    static_cast<std::size_t>(n),
                    rc::gen::map(rc::gen::inRange(0, 10000),
                                 [](int v) { return static_cast<double>(v) + 1.0; })),
                [](std::vector<double> vals) {
                    // Make strictly increasing by sorting and adding index offset
                    std::sort(vals.begin(), vals.end());
                    for (std::size_t i = 1; i < vals.size(); ++i) {
                        if (vals[i] <= vals[i - 1]) {
                            vals[i] = vals[i - 1] + 1.0;
                        }
                    }
                    return vals;
                });
        });
}

/// Generate a monotonically increasing profile of values for a given number
/// of levels. Values are in a reasonable physical range.
static rc::Gen<std::vector<double>> genMonoIncreasingValues(int n) {
    return rc::gen::map(
        rc::gen::container<std::vector<double>>(
            static_cast<std::size_t>(n),
            rc::gen::map(rc::gen::inRange(0, 1000),
                         [](int v) { return static_cast<double>(v) * 0.1; })),
        [](std::vector<double> vals) {
            std::sort(vals.begin(), vals.end());
            // Ensure strictly increasing
            for (std::size_t i = 1; i < vals.size(); ++i) {
                if (vals[i] <= vals[i - 1]) {
                    vals[i] = vals[i - 1] + 0.01;
                }
            }
            return vals;
        });
}

/// Generate a monotonically decreasing profile of values for a given number
/// of levels.
static rc::Gen<std::vector<double>> genMonoDecreasingValues(int n) {
    return rc::gen::map(genMonoIncreasingValues(n),
                        [](std::vector<double> vals) {
                            std::reverse(vals.begin(), vals.end());
                            return vals;
                        });
}

// =============================================================================
// Property 10: Vertical interpolation threshold determines passthrough vs
//              interpolation
// =============================================================================

/**
 * **Validates: Requirements 4.1, 4.3**
 * Property 10: Vertical interpolation threshold determines passthrough vs
 *              interpolation
 *
 * For any source and target level arrays, if the maximum absolute difference
 * between corresponding levels is within 1e-10, the output SHALL be bitwise
 * identical to the input. If any difference exceeds 1e-10, interpolation
 * SHALL be performed (output differs from input for non-constant profiles).
 */
RC_GTEST_PROP(VerticalProperty,
              P10_ThresholdDeterminesPassthroughVsInterpolation_Passthrough,
              ()) {
    // Generate source levels (at least 3 for valid interpolation)
    const auto src_levels = *genSortedLevels(3, 10);
    const int n = static_cast<int>(src_levels.size());

    // Generate source values (non-constant profile)
    const auto src_values = *rc::gen::container<std::vector<double>>(
        static_cast<std::size_t>(n),
        rc::gen::map(rc::gen::inRange(-1000, 1000),
                     [](int v) { return static_cast<double>(v) * 0.01; }));

    // Create target levels within 1e-10 of source levels (passthrough case)
    std::vector<double> tgt_levels(src_levels.size());
    for (std::size_t i = 0; i < src_levels.size(); ++i) {
        // Perturb by at most 1e-11 (well within tolerance)
        double perturbation =
            static_cast<double>(*rc::gen::inRange(-10, 11)) * 1.0e-12;
        tgt_levels[i] = src_levels[i] + perturbation;
    }

    std::vector<double> tgt_values(static_cast<std::size_t>(n), 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    TspackInterpolator interp(config);

    int result = interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values));

    RC_ASSERT(result == 0);

    // Output should be bitwise identical to input (passthrough)
    for (std::size_t i = 0; i < src_values.size(); ++i) {
        RC_ASSERT(std::memcmp(&tgt_values[i], &src_values[i], sizeof(double)) == 0);
    }
}

RC_GTEST_PROP(VerticalProperty,
              P10_ThresholdDeterminesPassthroughVsInterpolation_Interpolation,
              ()) {
    // Generate source levels (at least 4 to ensure non-trivial interpolation)
    const auto src_levels = *genSortedLevels(4, 10);
    const int n = static_cast<int>(src_levels.size());

    // Generate a non-constant, non-linear source profile so interpolation
    // at different levels will produce different values
    std::vector<double> src_values(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        // Quadratic profile to ensure non-linearity
        src_values[static_cast<std::size_t>(i)] =
            src_levels[static_cast<std::size_t>(i)] *
            src_levels[static_cast<std::size_t>(i)] * 0.001;
    }

    // Create target levels that differ from source by more than 1e-10
    // Use midpoints between source levels
    std::vector<double> tgt_levels;
    for (int i = 0; i < n - 1; ++i) {
        double mid = 0.5 * (src_levels[static_cast<std::size_t>(i)] +
                            src_levels[static_cast<std::size_t>(i + 1)]);
        tgt_levels.push_back(mid);
    }

    RC_PRE(!tgt_levels.empty());

    std::vector<double> tgt_values(tgt_levels.size(), 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    TspackInterpolator interp(config);

    interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values));

    // Since target levels are midpoints, at least some output values should
    // differ from any source value (non-constant quadratic profile)
    bool any_different = false;
    for (std::size_t i = 0; i < tgt_values.size(); ++i) {
        bool matches_any_source = false;
        for (std::size_t j = 0; j < src_values.size(); ++j) {
            if (tgt_values[i] == src_values[j]) {
                matches_any_source = true;
                break;
            }
        }
        if (!matches_any_source) {
            any_different = true;
            break;
        }
    }
    RC_ASSERT(any_different);
}

// =============================================================================
// Property 11: Monotonicity preservation in vertical interpolation
// =============================================================================

/**
 * **Validates: Requirements 4.2**
 * Property 11: Monotonicity preservation in vertical interpolation
 *
 * For any monotonically increasing (or decreasing) source profile with at
 * least 3 valid levels, the TSPACK-interpolated output at any set of target
 * levels SHALL also be monotonically increasing (or decreasing, respectively),
 * introducing no local extrema not present in the source.
 */
RC_GTEST_PROP(VerticalProperty,
              P11_MonotonicityPreservation_Increasing, ()) {
    // Generate source levels (4-12 levels)
    const auto src_levels = *genSortedLevels(4, 12);
    const int n = static_cast<int>(src_levels.size());

    // Generate monotonically increasing values
    const auto src_values = *genMonoIncreasingValues(n);

    // Generate target levels within the source range (dense interpolation)
    const double lo = src_levels.front();
    const double hi = src_levels.back();
    const int n_tgt = *rc::gen::inRange(3, 20);
    std::vector<double> tgt_levels(static_cast<std::size_t>(n_tgt));
    for (int i = 0; i < n_tgt; ++i) {
        // Linearly space target levels within source range
        tgt_levels[static_cast<std::size_t>(i)] =
            lo + (hi - lo) * static_cast<double>(i) /
                     static_cast<double>(n_tgt - 1);
    }

    std::vector<double> tgt_values(static_cast<std::size_t>(n_tgt), 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.max_extrap_distance = 0.0; // No extrapolation
    TspackInterpolator interp(config);

    interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values));

    // Verify monotonically increasing output
    for (int i = 1; i < n_tgt; ++i) {
        RC_ASSERT(tgt_values[static_cast<std::size_t>(i)] >=
                  tgt_values[static_cast<std::size_t>(i - 1)]);
    }
}

RC_GTEST_PROP(VerticalProperty,
              P11_MonotonicityPreservation_Decreasing, ()) {
    // Generate source levels (4-12 levels)
    const auto src_levels = *genSortedLevels(4, 12);
    const int n = static_cast<int>(src_levels.size());

    // Generate monotonically decreasing values
    const auto src_values = *genMonoDecreasingValues(n);

    // Generate target levels within the source range
    const double lo = src_levels.front();
    const double hi = src_levels.back();
    const int n_tgt = *rc::gen::inRange(3, 20);
    std::vector<double> tgt_levels(static_cast<std::size_t>(n_tgt));
    for (int i = 0; i < n_tgt; ++i) {
        tgt_levels[static_cast<std::size_t>(i)] =
            lo + (hi - lo) * static_cast<double>(i) /
                     static_cast<double>(n_tgt - 1);
    }

    std::vector<double> tgt_values(static_cast<std::size_t>(n_tgt), 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.max_extrap_distance = 0.0; // No extrapolation
    TspackInterpolator interp(config);

    interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values));

    // Verify monotonically decreasing output
    for (int i = 1; i < n_tgt; ++i) {
        RC_ASSERT(tgt_values[static_cast<std::size_t>(i)] <=
                  tgt_values[static_cast<std::size_t>(i - 1)]);
    }
}

// =============================================================================
// Property 12: Extrapolation follows boundary gradient within limit
// =============================================================================

/**
 * **Validates: Requirements 4.5**
 * Property 12: Extrapolation follows boundary gradient within limit
 *
 * For any source profile and target levels outside the source range, the
 * extrapolated values SHALL follow the direction of the nearest boundary
 * gradient and SHALL NOT extend beyond the configurable maximum extrapolation
 * distance (default: one source-level interval).
 */
RC_GTEST_PROP(VerticalProperty,
              P12_ExtrapolationBeyondLimitIsMarkedMissing, ()) {
    // Generate source levels (at least 4)
    const auto src_levels = *genSortedLevels(4, 10);
    const int n = static_cast<int>(src_levels.size());

    // Generate valid source values (finite, non-missing)
    const auto src_values = *rc::gen::container<std::vector<double>>(
        static_cast<std::size_t>(n),
        rc::gen::map(rc::gen::inRange(1, 1000),
                     [](int v) { return static_cast<double>(v) * 0.1; }));

    const double x_min = src_levels.front();
    const double x_max = src_levels.back();
    const double range = x_max - x_min;

    // Set a specific max extrapolation distance
    const double max_extrap = *rc::gen::map(
        rc::gen::inRange(1, 100),
        [&](int v) { return static_cast<double>(v) * 0.01 * range; });

    // Create target levels that are beyond the extrapolation limit
    const double far_below = x_min - max_extrap - 1.0;
    const double far_above = x_max + max_extrap + 1.0;
    std::vector<double> tgt_levels = {far_below, far_above};

    const double missing_value = -9999.0;
    std::vector<double> tgt_values(2, 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.max_extrap_distance = max_extrap;
    TspackInterpolator interp(config);

    interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values),
        missing_value);

    // Both targets should be marked as missing
    RC_ASSERT(tgt_values[0] == missing_value);
    RC_ASSERT(tgt_values[1] == missing_value);
}

RC_GTEST_PROP(VerticalProperty,
              P12_ExtrapolationWithinLimitProducesReasonableValues, ()) {
    // Generate source levels (at least 4) with well-separated values
    const auto src_levels = *genSortedLevels(4, 10);
    const int n = static_cast<int>(src_levels.size());

    // Generate valid source values with significant variation.
    // Use levels themselves scaled to produce a clear non-trivial profile
    // so the spline fit is well-conditioned.
    const double base = *rc::gen::map(
        rc::gen::inRange(1, 100),
        [](int v) { return static_cast<double>(v); });
    const double slope = *rc::gen::map(
        rc::gen::inRange(1, 50),
        [](int v) { return static_cast<double>(v) * 0.5; });

    std::vector<double> src_values(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        src_values[static_cast<std::size_t>(i)] =
            base + slope * src_levels[static_cast<std::size_t>(i)];
    }

    const double x_min = src_levels.front();
    const double x_max = src_levels.back();

    // Compute the extrapolation distance as one interval beyond boundary
    const double lower_interval = src_levels[1] - src_levels[0];
    const double upper_interval =
        src_levels[static_cast<std::size_t>(n - 1)] -
        src_levels[static_cast<std::size_t>(n - 2)];
    const double max_extrap = std::max(lower_interval, upper_interval);

    // Place target levels within extrapolation range (but outside source range)
    // Use a small fraction (1%-50%) of the allowed extrapolation distance
    const double frac = *rc::gen::map(
        rc::gen::inRange(1, 50),
        [](int v) { return static_cast<double>(v) * 0.01; });
    const double below = x_min - frac * max_extrap;
    const double above = x_max + frac * max_extrap;
    std::vector<double> tgt_levels = {below, above};

    // Use a missing value that cannot appear from the linear profile
    const double missing_value = -1.0e30;
    std::vector<double> tgt_values(2, 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.max_extrap_distance = max_extrap;
    TspackInterpolator interp(config);

    int result = interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values),
        missing_value);

    // Values within extrap limit should NOT be missing
    RC_ASSERT(result == 0);
    RC_ASSERT(tgt_values[0] != missing_value);
    RC_ASSERT(tgt_values[1] != missing_value);
    // They should be finite
    RC_ASSERT(std::isfinite(tgt_values[0]));
    RC_ASSERT(std::isfinite(tgt_values[1]));
}

// =============================================================================
// Property 13: Vertical interpolation with missing values uses only valid
//              levels
// =============================================================================

/**
 * **Validates: Requirements 4.7**
 * Property 13: Vertical interpolation with missing values uses only valid
 *              levels
 *
 * For any source column containing missing values at arbitrary positions
 * (with at least 3 valid levels remaining), interpolation SHALL use only
 * the valid levels for spline fitting, and any target level requiring
 * extrapolation beyond the valid range SHALL be marked as missing.
 */
RC_GTEST_PROP(VerticalProperty,
              P13_MissingValuesUsedOnlyValidLevels, ()) {
    // Generate source levels (at least 6, so we can remove some and still
    // have >= 3 valid)
    const auto src_levels = *genSortedLevels(6, 12);
    const int n = static_cast<int>(src_levels.size());

    // Generate monotonically increasing source values
    const auto base_values = *genMonoIncreasingValues(n);

    const double missing_value = -9999.0;

    // Insert missing values at random positions, but keep at least 3 valid
    const int max_missing = n - 3;
    RC_PRE(max_missing >= 1);
    const int n_missing = *rc::gen::inRange(1, max_missing + 1);

    // Generate unique indices to mark as missing
    auto missing_indices = *rc::gen::unique<std::vector<int>>(
        static_cast<std::size_t>(n_missing),
        rc::gen::inRange(0, n));
    RC_PRE(static_cast<int>(missing_indices.size()) == n_missing);

    std::vector<double> src_values = base_values;
    for (int idx : missing_indices) {
        src_values[static_cast<std::size_t>(idx)] = missing_value;
    }

    // Determine the valid level range (levels where values are not missing)
    double valid_min = std::numeric_limits<double>::max();
    double valid_max = std::numeric_limits<double>::lowest();
    for (int i = 0; i < n; ++i) {
        if (src_values[static_cast<std::size_t>(i)] != missing_value) {
            valid_min = std::min(valid_min, src_levels[static_cast<std::size_t>(i)]);
            valid_max = std::max(valid_max, src_levels[static_cast<std::size_t>(i)]);
        }
    }

    // Create target levels: some within valid range, some beyond
    // Place one target well beyond valid range (should be missing)
    double beyond_level = valid_max + 1000.0;  // Far beyond any extrap limit
    // Place one target within valid range (should have a value)
    double within_level = 0.5 * (valid_min + valid_max);

    std::vector<double> tgt_levels = {within_level, beyond_level};
    std::vector<double> tgt_values(2, 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.max_extrap_distance = 0.0;  // No extrapolation allowed
    TspackInterpolator interp(config);

    interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values),
        missing_value);

    // Target within valid range should not be missing
    RC_ASSERT(tgt_values[0] != missing_value);
    RC_ASSERT(std::isfinite(tgt_values[0]));

    // Target beyond valid range should be marked missing (extrapolation
    // beyond valid range with max_extrap=0)
    RC_ASSERT(tgt_values[1] == missing_value);
}

RC_GTEST_PROP(VerticalProperty,
              P13_FewerThan3ValidLevelsFillsWithMissing, ()) {
    // Generate source levels (at least 4)
    const auto src_levels = *genSortedLevels(4, 8);
    const int n = static_cast<int>(src_levels.size());

    const double missing_value = -9999.0;

    // Make almost all values missing, leaving fewer than 3 valid
    const int n_valid = *rc::gen::inRange(0, 3); // 0, 1, or 2 valid
    std::vector<double> src_values(static_cast<std::size_t>(n), missing_value);

    // Set a few values as valid
    for (int i = 0; i < n_valid && i < n; ++i) {
        src_values[static_cast<std::size_t>(i)] =
            static_cast<double>(i + 1) * 10.0;
    }

    // Target levels
    std::vector<double> tgt_levels = {src_levels[0], src_levels.back()};
    std::vector<double> tgt_values(2, 0.0);

    VerticalConfig config;
    config.level_tolerance = 1.0e-10;
    config.min_valid_levels = 3;
    TspackInterpolator interp(config);

    int result = interp.interpolate_column(
        std::span<const double>(src_levels),
        std::span<const double>(src_values),
        std::span<const double>(tgt_levels),
        std::span<double>(tgt_values),
        missing_value);

    // All targets should be filled with missing value
    RC_ASSERT(result == static_cast<int>(tgt_levels.size()));
    for (std::size_t i = 0; i < tgt_values.size(); ++i) {
        RC_ASSERT(tgt_values[i] == missing_value);
    }
}

} // anonymous namespace
} // namespace tide::vertical
