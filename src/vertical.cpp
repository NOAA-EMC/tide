/**
 * @file vertical.cpp
 * @brief Implementation of the TSPACK vertical interpolation engine.
 *
 * Uses the vendored TSPACK C library for tension spline fitting with
 * monotonicity preservation. Handles missing values, level-tolerance
 * passthrough, boundary-gradient extrapolation, and optional log-pressure
 * coordinate transformation.
 */

#include "tide/vertical.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <vector>

extern "C" {
#include "tspack.h"
}

namespace tide::vertical {

TspackInterpolator::TspackInterpolator(const VerticalConfig& config)
    : config_(config) {}

/**
 * @brief Check if all source levels match target levels within tolerance.
 *
 * Returns true if source and target have the same number of levels and
 * each corresponding pair differs by at most level_tolerance.
 *
 * @param src_levels Source vertical coordinates.
 * @param tgt_levels Target vertical coordinates.
 * @param tolerance  Maximum allowable absolute difference.
 * @return true if passthrough is appropriate.
 */
static bool levels_match(std::span<const double> src_levels,
                         std::span<const double> tgt_levels,
                         double tolerance) {
    if (src_levels.size() != tgt_levels.size()) {
        return false;
    }
    for (std::size_t i = 0; i < src_levels.size(); ++i) {
        if (std::abs(src_levels[i] - tgt_levels[i]) > tolerance) {
            return false;
        }
    }
    return true;
}

auto TspackInterpolator::interpolate_column(
    std::span<const double> src_levels,
    std::span<const double> src_values,
    std::span<const double> tgt_levels,
    std::span<double> tgt_values,
    double missing_value) -> int {

    const auto n_src = src_levels.size();
    const auto n_tgt = tgt_levels.size();

    // --- Passthrough check: skip if levels match within tolerance ---
    if (levels_match(src_levels, tgt_levels, config_.level_tolerance)) {
        for (std::size_t i = 0; i < n_tgt; ++i) {
            tgt_values[i] = src_values[i];
        }
        return 0;
    }

    // --- Filter out missing values, keeping only valid levels ---
    std::vector<double> valid_levels;
    std::vector<double> valid_values;
    valid_levels.reserve(n_src);
    valid_values.reserve(n_src);

    for (std::size_t i = 0; i < n_src; ++i) {
        if (src_values[i] != missing_value &&
            std::isfinite(src_values[i]) &&
            std::isfinite(src_levels[i])) {
            valid_levels.push_back(src_levels[i]);
            valid_values.push_back(src_values[i]);
        }
    }

    const int n_valid = static_cast<int>(valid_levels.size());

    // --- Minimum valid levels check ---
    if (n_valid < config_.min_valid_levels) {
        // Fill target with missing values and warn
        for (std::size_t i = 0; i < n_tgt; ++i) {
            tgt_values[i] = missing_value;
        }
        std::fprintf(stderr,
                     "[tide::vertical] Warning: column has %d valid levels "
                     "(minimum %d required). Filling with missing values.\n",
                     n_valid, config_.min_valid_levels);
        return static_cast<int>(n_tgt);
    }

    // --- Sort levels into ascending order (required by TSPACK) ---
    // Build index array for sorting
    std::vector<std::size_t> sort_idx(static_cast<std::size_t>(n_valid));
    std::iota(sort_idx.begin(), sort_idx.end(), 0);
    std::sort(sort_idx.begin(), sort_idx.end(),
              [&](std::size_t a, std::size_t b) {
                  return valid_levels[a] < valid_levels[b];
              });

    std::vector<double> x(static_cast<std::size_t>(n_valid));  // sorted levels (coordinates)
    std::vector<double> y(static_cast<std::size_t>(n_valid));  // sorted values

    for (int i = 0; i < n_valid; ++i) {
        x[static_cast<std::size_t>(i)] = valid_levels[sort_idx[static_cast<std::size_t>(i)]];
        y[static_cast<std::size_t>(i)] = valid_values[sort_idx[static_cast<std::size_t>(i)]];
    }

    // --- Optional log-pressure transformation ---
    // Transform coordinates to ln(P) space if configured and coordinates are
    // positive (pressure in Pa).
    std::vector<double> tgt_coords(n_tgt);
    bool applied_log = false;

    if (config_.log_pressure) {
        // Verify all source coordinates are positive
        bool all_positive = true;
        for (int i = 0; i < n_valid; ++i) {
            if (x[static_cast<std::size_t>(i)] <= 0.0) {
                all_positive = false;
                break;
            }
        }
        // Verify all target coordinates are positive
        if (all_positive) {
            for (std::size_t i = 0; i < n_tgt; ++i) {
                if (tgt_levels[i] <= 0.0) {
                    all_positive = false;
                    break;
                }
            }
        }

        if (all_positive) {
            // Apply ln(P) transform to source coordinates
            for (int i = 0; i < n_valid; ++i) {
                x[static_cast<std::size_t>(i)] =
                    std::log(x[static_cast<std::size_t>(i)]);
            }
            // Apply ln(P) transform to target coordinates
            for (std::size_t i = 0; i < n_tgt; ++i) {
                tgt_coords[i] = std::log(tgt_levels[i]);
            }
            applied_log = true;
        }
    }

    if (!applied_log) {
        // Use linear coordinates (no transform)
        for (std::size_t i = 0; i < n_tgt; ++i) {
            tgt_coords[i] = tgt_levels[i];
        }
    }

    (void)applied_log; // Suppress unused-variable warning after this point

    // --- Compute extrapolation bounds ---
    const double x_min = x[0];
    const double x_max = x[static_cast<std::size_t>(n_valid - 1)];

    double max_extrap = 0.0;
    if (config_.max_extrap_distance < 0.0) {
        // Default: one source-level interval beyond boundary
        if (n_valid >= 2) {
            const double lower_interval = x[1] - x[0];
            const double upper_interval =
                x[static_cast<std::size_t>(n_valid - 1)] -
                x[static_cast<std::size_t>(n_valid - 2)];
            max_extrap = std::max(lower_interval, upper_interval);
        } else {
            max_extrap = 0.0;
        }
    } else {
        max_extrap = config_.max_extrap_distance;
    }

    // --- TSPACK spline setup using tspsi ---
    // tspsi parameters:
    //   n:      number of data points
    //   x:      abscissae (strictly increasing)
    //   y:      ordinates
    //   ncd:    number of continuous derivatives (1 = C1 monotone, 2 = C2)
    //   iendc:  end condition type (0 = free, 1 = yp specified)
    //   per:    periodic flag (0 = not periodic)
    //   unifrm: uniform tension flag (0 = adaptive)
    //   lwk:    length of work array (>= n_valid)
    //   wk:     work array
    //   yp:     output first derivatives
    //   sigma:  output tension factors

    const int ncd = 1;     // Monotonicity-preserving (C1 continuity)
    const int iendc = 0;   // Free end conditions
    const int per = 0;     // Not periodic
    const int unifrm = 0;  // Adaptive (non-uniform) tension

    const int lwk = std::max(n_valid, 2);
    std::vector<double> wk(static_cast<std::size_t>(lwk), 0.0);
    std::vector<double> yp(static_cast<std::size_t>(n_valid), 0.0);
    std::vector<double> sigma(static_cast<std::size_t>(n_valid), 0.0);
    int ier = 0;

    tspsi(n_valid, x.data(), y.data(), ncd, iendc, per, unifrm, lwk,
          wk.data(), yp.data(), sigma.data(), &ier);

    if (ier < 0) {
        // TSPACK setup failed — fill with missing
        for (std::size_t i = 0; i < n_tgt; ++i) {
            tgt_values[i] = missing_value;
        }
        std::fprintf(stderr,
                     "[tide::vertical] Warning: TSPACK tspsi failed with "
                     "ier=%d. Filling column with missing values.\n",
                     ier);
        return static_cast<int>(n_tgt);
    }

    // --- Evaluate spline at each target level ---
    int n_missing = 0;

    for (std::size_t i = 0; i < n_tgt; ++i) {
        const double t = tgt_coords[i];

        // Check if target is within interpolation range or within
        // extrapolation bounds
        if (t < x_min - max_extrap || t > x_max + max_extrap) {
            // Beyond extrapolation limit — mark as missing
            tgt_values[i] = missing_value;
            ++n_missing;
            continue;
        }

        // For targets within the valid source range or within extrap bounds,
        // check if a source column had missing values that cause this target
        // to require extrapolation beyond valid data range
        if (t < x_min || t > x_max) {
            // We're extrapolating — if max_extrap is 0, mark missing
            if (max_extrap == 0.0) {
                tgt_values[i] = missing_value;
                ++n_missing;
                continue;
            }
        }

        int eval_ier = 0;
        double val = hval(t, n_valid, x.data(), y.data(), yp.data(),
                          sigma.data(), &eval_ier);

        if (eval_ier < 0) {
            // Negative ier from hval indicates a true error (e.g., invalid data)
            tgt_values[i] = missing_value;
            ++n_missing;
        } else {
            // ier == 0: normal interpolation
            // ier == 1: extrapolation performed (valid result)
            tgt_values[i] = val;
        }
    }

    return n_missing;
}

auto TspackInterpolator::interpolate_field(
    std::span<const double> src_levels,
    std::span<const double> src_field,
    std::span<const double> tgt_levels,
    std::span<double> tgt_field,
    std::size_t ncols) -> int {

    const auto n_src_levels = src_levels.size();
    const auto n_tgt_levels = tgt_levels.size();

    for (std::size_t col = 0; col < ncols; ++col) {
        // Extract column from source field (row-major: col * n_src_levels)
        std::span<const double> col_src(
            src_field.data() + col * n_src_levels, n_src_levels);

        // Output column in target field
        std::span<double> col_tgt(
            tgt_field.data() + col * n_tgt_levels, n_tgt_levels);

        interpolate_column(src_levels, col_src, tgt_levels, col_tgt);
    }

    return 0;
}

} // namespace tide::vertical
