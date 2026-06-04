/**
 * @file reference_data.hpp
 * @brief Precomputed reference data for TIDE end-to-end integration tests.
 *
 * This header provides analytically computed reference values for the
 * synthetic forcing test dataset. The data follows the function:
 *
 *     T(t, lat, lon, lev) = 250.0 + 0.1*lat + 0.01*lon + 0.001*lev + 10.0*(t/3600.0)
 *
 * The reference output is computed at t=1800s (midpoint between t=0 and t=3600).
 *
 * Grid specification:
 *   - Latitudes:  [-60, -20, 20, 60] degrees_north
 *   - Longitudes: [0, 90, 180, 270] degrees_east
 *   - Levels:     [100000, 85000, 50000, 20000] Pa
 *   - Time:       [0, 3600] seconds since epoch
 *
 * Array layout: [level][lat][lon] (C-order, level varies slowest)
 */
#ifndef TIDE_TEST_REFERENCE_DATA_HPP
#define TIDE_TEST_REFERENCE_DATA_HPP

#include <array>
#include <cstddef>

namespace tide::test {

/// Grid dimensions
inline constexpr std::size_t kNLat = 4;
inline constexpr std::size_t kNLon = 4;
inline constexpr std::size_t kNLev = 4;
inline constexpr std::size_t kNTime = 2;
inline constexpr std::size_t kFieldSize = kNLev * kNLat * kNLon;  // 64

/// Grid coordinates
inline constexpr std::array<double, kNLat> kLatitudes  = {-60.0, -20.0, 20.0, 60.0};
inline constexpr std::array<double, kNLon> kLongitudes = {0.0, 90.0, 180.0, 270.0};
inline constexpr std::array<double, kNLev> kLevels     = {100000.0, 85000.0, 50000.0, 20000.0};
inline constexpr std::array<double, kNTime> kTimes     = {0.0, 3600.0};

/// Target time for temporal interpolation (midpoint)
inline constexpr double kTargetTime = 1800.0;

/// Relative tolerance for e2e comparison
inline constexpr double kTolerance = 1.0e-10;

/**
 * @brief Compute analytical temperature at given coordinates.
 *
 * T(t, lat, lon, lev) = 250.0 + 0.1*lat + 0.01*lon + 0.001*lev + 10.0*(t/3600.0)
 */
inline constexpr double analytical_temperature(double time_s, double lat, double lon, double lev) {
    return 250.0 + 0.1 * lat + 0.01 * lon + 0.001 * lev + 10.0 * (time_s / 3600.0);
}

/**
 * @brief Precomputed reference output at t=1800s.
 *
 * Layout: reference[level_idx * kNLat * kNLon + lat_idx * kNLon + lon_idx]
 *
 * At t=1800s, the formula simplifies to:
 *     T = 255.0 + 0.1*lat + 0.01*lon + 0.001*lev
 */
inline constexpr std::array<double, kFieldSize> compute_reference() {
    std::array<double, kFieldSize> ref{};
    for (std::size_t k = 0; k < kNLev; ++k) {
        for (std::size_t j = 0; j < kNLat; ++j) {
            for (std::size_t i = 0; i < kNLon; ++i) {
                std::size_t idx = k * kNLat * kNLon + j * kNLon + i;
                ref[idx] = analytical_temperature(kTargetTime, kLatitudes[j],
                                                  kLongitudes[i], kLevels[k]);
            }
        }
    }
    return ref;
}

/// Precomputed reference array (constexpr-evaluated at compile time)
inline constexpr auto kReferenceOutput = compute_reference();

/**
 * @brief Precomputed reference values at t=0 (first time step).
 */
inline constexpr std::array<double, kFieldSize> compute_time0() {
    std::array<double, kFieldSize> ref{};
    for (std::size_t k = 0; k < kNLev; ++k) {
        for (std::size_t j = 0; j < kNLat; ++j) {
            for (std::size_t i = 0; i < kNLon; ++i) {
                std::size_t idx = k * kNLat * kNLon + j * kNLon + i;
                ref[idx] = analytical_temperature(0.0, kLatitudes[j],
                                                  kLongitudes[i], kLevels[k]);
            }
        }
    }
    return ref;
}

/**
 * @brief Precomputed reference values at t=3600 (second time step).
 */
inline constexpr std::array<double, kFieldSize> compute_time1() {
    std::array<double, kFieldSize> ref{};
    for (std::size_t k = 0; k < kNLev; ++k) {
        for (std::size_t j = 0; j < kNLat; ++j) {
            for (std::size_t i = 0; i < kNLon; ++i) {
                std::size_t idx = k * kNLat * kNLon + j * kNLon + i;
                ref[idx] = analytical_temperature(3600.0, kLatitudes[j],
                                                  kLongitudes[i], kLevels[k]);
            }
        }
    }
    return ref;
}

inline constexpr auto kFieldAtTime0 = compute_time0();
inline constexpr auto kFieldAtTime1 = compute_time1();

/// File paths (relative to TIDE_TEST_DATA_DIR)
inline constexpr const char* kSyntheticForcingFile = "synthetic_forcing.nc";
inline constexpr const char* kReferenceOutputFile  = "reference_output.nc";
inline constexpr const char* kTestConfigFile       = "test_config.yaml";

}  // namespace tide::test

#endif  // TIDE_TEST_REFERENCE_DATA_HPP
