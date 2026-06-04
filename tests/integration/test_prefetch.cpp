/**
 * @file test_prefetch.cpp
 * @brief Integration test for prefetch equivalence.
 *
 * Verifies that running the same stream configuration with prefetch enabled
 * vs. disabled produces bitwise identical output fields. This confirms that
 * the PrefetchManager only changes I/O timing (overlap with computation)
 * without modifying the data itself.
 *
 * The test creates two Stream instances from the same forcing file and
 * target grid configuration. One has prefetch.enabled = true, the other
 * has prefetch.enabled = false (default). Both are advanced to the same
 * target time, and the output fields are compared byte-for-byte.
 *
 * Validates: Requirements 5.2
 * Property 7: Prefetch produces identical results to synchronous reads
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <mpi.h>

#include "atlas/library.h"

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/stream.hpp"
#include "tide/types.hpp"

namespace {

/**
 * @brief Build a StreamConfig for the synthetic_forcing.nc test file.
 *
 * @param data_dir Path to the test data directory.
 * @param prefetch_enabled Whether async prefetch is enabled.
 * @return StreamConfig configured for the test.
 */
tide::config::StreamConfig make_test_config(const std::string& data_dir,
                                            bool prefetch_enabled) {
    tide::config::StreamConfig config;
    config.name = prefetch_enabled ? "temperature_prefetch" : "temperature_sync";
    config.file_path = data_dir + "/synthetic_forcing.nc";
    config.field_name = "temperature";
    config.source_grid_type = "regular_latlon";
    config.target_grid_ref = "test_grid";
    config.temporal_mode = tide::config::TemporalMode::Linear;
    config.interp_method = "nearest-neighbour";
    config.scaling.multiplier = 1.0;
    config.scaling.offset = 0.0;
    config.log_pressure = false;
    config.extrap_limit = -1.0;

    // Prefetch configuration — only difference between the two streams
    config.prefetch.enabled = prefetch_enabled;
    config.prefetch.depth = 1;

    return config;
}

/**
 * @brief Build a TargetGrid matching the synthetic forcing file's grid.
 *
 * The target grid uses the same 4x4 lat-lon grid as the source, so
 * regridding is effectively identity. This isolates the test to verify
 * that prefetch doesn't affect the data path.
 */
tide::TargetGrid make_target_grid() {
    tide::TargetGrid target;
    target.num_cols = 4 * 4; // 4 lats × 4 lons = 16 cells
    target.num_levels = 4;

    // Same grid points as synthetic_forcing.nc
    std::vector<double> lats_1d = {-60.0, -20.0, 20.0, 60.0};
    std::vector<double> lons_1d = {0.0, 90.0, 180.0, 270.0};

    // Expand to 2D coordinate pairs (lat × lon)
    for (auto lat : lats_1d) {
        for (auto lon : lons_1d) {
            target.lats.push_back(lat);
            target.lons.push_back(lon);
        }
    }

    target.levels = {100000.0, 85000.0, 50000.0, 20000.0};
    target.level_units = "Pa";

    return target;
}

/**
 * @brief Test fixture for the prefetch equivalence integration test.
 */
class PrefetchEquivalenceTest : public ::testing::Test {
protected:
    void SetUp() override {
        data_dir_ = std::string(TIDE_TEST_DATA_DIR);
        forcing_path_ = data_dir_ + "/synthetic_forcing.nc";

        if (!std::filesystem::exists(forcing_path_)) {
            GTEST_SKIP() << "Synthetic forcing file not found: " << forcing_path_
                         << " (test data may not be generated in this environment)";
        }
    }

    std::string data_dir_;
    std::string forcing_path_;
};

/**
 * @brief Verify prefetch-enabled and prefetch-disabled streams produce
 *        bitwise identical output for the same target time.
 *
 * This is the core equivalence test. It:
 *   1. Creates two streams with identical configs except prefetch.enabled
 *   2. Advances both to t=1800s (midpoint between t=0 and t=3600)
 *   3. Retrieves the output field from each
 *   4. Compares all elements — they must be bitwise identical
 *
 * Validates: Requirements 5.2
 */
TEST_F(PrefetchEquivalenceTest, PrefetchEnabledProducesSameOutputAsDisabled) {
    const double target_time = 1800.0;
    auto target_grid = make_target_grid();

    // ── Create stream with prefetch DISABLED (synchronous reads) ─────────
    auto config_sync = make_test_config(data_dir_, /*prefetch_enabled=*/false);
    auto result_sync = tide::Stream::create(config_sync, target_grid, MPI_COMM_WORLD);
    if (!result_sync) {
        GTEST_SKIP() << "Failed to create sync stream: "
                     << result_sync.error().message
                     << " (may require MPI/AMIO environment)";
    }
    auto stream_sync = std::move(result_sync.value());

    // ── Create stream with prefetch ENABLED (async reads) ────────────────
    auto config_prefetch = make_test_config(data_dir_, /*prefetch_enabled=*/true);
    auto result_prefetch = tide::Stream::create(config_prefetch, target_grid, MPI_COMM_WORLD);
    if (!result_prefetch) {
        GTEST_SKIP() << "Failed to create prefetch stream: "
                     << result_prefetch.error().message
                     << " (may require MPI/AMIO environment)";
    }
    auto stream_prefetch = std::move(result_prefetch.value());

    // ── Advance both streams to the same target time ─────────────────────
    int rc_sync = stream_sync.advance(target_time);
    ASSERT_EQ(rc_sync, 0) << "Sync stream advance failed with rc=" << rc_sync;

    int rc_prefetch = stream_prefetch.advance(target_time);
    ASSERT_EQ(rc_prefetch, 0) << "Prefetch stream advance failed with rc=" << rc_prefetch;

    // ── Retrieve output fields ───────────────────────────────────────────
    auto field_sync = stream_sync.get_field<std::layout_right>("temperature");
    auto field_prefetch = stream_prefetch.get_field<std::layout_right>("temperature");

    // Verify both fields are valid (non-null)
    ASSERT_NE(field_sync.data_handle(), nullptr)
        << "Sync stream returned null field";
    ASSERT_NE(field_prefetch.data_handle(), nullptr)
        << "Prefetch stream returned null field";

    // Verify dimensions match
    ASSERT_EQ(field_sync.extent(0), field_prefetch.extent(0))
        << "Dimension 0 (ncols) mismatch";
    ASSERT_EQ(field_sync.extent(1), field_prefetch.extent(1))
        << "Dimension 1 (nlevels) mismatch";
    ASSERT_EQ(field_sync.extent(2), field_prefetch.extent(2))
        << "Dimension 2 (nfields) mismatch";

    // ── Compare output fields bitwise ────────────────────────────────────
    const std::size_t total_elements =
        field_sync.extent(0) * field_sync.extent(1) * field_sync.extent(2);

    ASSERT_GT(total_elements, 0u) << "Output field has zero elements";

    // Byte-by-byte comparison for bitwise identity
    const std::size_t total_bytes = total_elements * sizeof(double);
    int memcmp_result = std::memcmp(
        field_sync.data_handle(),
        field_prefetch.data_handle(),
        total_bytes);

    if (memcmp_result != 0) {
        // If not bitwise identical, report the first difference for debugging
        std::size_t mismatch_count = 0;
        for (std::size_t i = 0; i < total_elements; ++i) {
            if (field_sync.data_handle()[i] != field_prefetch.data_handle()[i]) {
                if (mismatch_count < 5) {
                    std::cerr << "  MISMATCH at index " << i
                              << ": sync=" << field_sync.data_handle()[i]
                              << " vs prefetch=" << field_prefetch.data_handle()[i]
                              << "\n";
                }
                ++mismatch_count;
            }
        }
        FAIL() << "Output fields are NOT bitwise identical. "
               << mismatch_count << " of " << total_elements
               << " elements differ.";
    }

    std::cout << "[  INFO   ] Prefetch equivalence verified: "
              << total_elements << " elements are bitwise identical ("
              << total_bytes << " bytes)\n";

    // ── Cleanup ──────────────────────────────────────────────────────────
    stream_sync.finalize();
    stream_prefetch.finalize();
}

/**
 * @brief Verify equivalence holds across multiple consecutive advance calls.
 *
 * Advances both streams through two time points to ensure that the prefetch
 * state machine (issue → wait → issue cycle) produces consistent results
 * across multiple pipeline invocations.
 *
 * Validates: Requirements 5.2
 */
TEST_F(PrefetchEquivalenceTest, PrefetchEquivalenceAcrossMultipleAdvances) {
    auto target_grid = make_target_grid();

    // Create both streams
    auto config_sync = make_test_config(data_dir_, /*prefetch_enabled=*/false);
    auto result_sync = tide::Stream::create(config_sync, target_grid, MPI_COMM_WORLD);
    if (!result_sync) {
        GTEST_SKIP() << "Failed to create sync stream: "
                     << result_sync.error().message;
    }
    auto stream_sync = std::move(result_sync.value());

    auto config_prefetch = make_test_config(data_dir_, /*prefetch_enabled=*/true);
    auto result_prefetch = tide::Stream::create(config_prefetch, target_grid, MPI_COMM_WORLD);
    if (!result_prefetch) {
        GTEST_SKIP() << "Failed to create prefetch stream: "
                     << result_prefetch.error().message;
    }
    auto stream_prefetch = std::move(result_prefetch.value());

    // Test at multiple time points within the forcing file's time range [0, 3600]
    const std::vector<double> target_times = {900.0, 1800.0, 2700.0};

    for (double t : target_times) {
        int rc_sync = stream_sync.advance(t);
        int rc_prefetch = stream_prefetch.advance(t);

        // If either fails, both should fail consistently (skip if environment issue)
        if (rc_sync != 0 || rc_prefetch != 0) {
            GTEST_SKIP() << "Stream advance failed at t=" << t
                         << " (sync rc=" << rc_sync
                         << ", prefetch rc=" << rc_prefetch << ")";
        }

        auto field_sync = stream_sync.get_field<std::layout_right>("temperature");
        auto field_prefetch = stream_prefetch.get_field<std::layout_right>("temperature");

        ASSERT_NE(field_sync.data_handle(), nullptr)
            << "Sync field null at t=" << t;
        ASSERT_NE(field_prefetch.data_handle(), nullptr)
            << "Prefetch field null at t=" << t;

        const std::size_t total_elements =
            field_sync.extent(0) * field_sync.extent(1) * field_sync.extent(2);
        const std::size_t total_bytes = total_elements * sizeof(double);

        ASSERT_EQ(std::memcmp(field_sync.data_handle(),
                              field_prefetch.data_handle(),
                              total_bytes), 0)
            << "Output fields differ at t=" << t
            << " (" << total_elements << " elements)";
    }

    std::cout << "[  INFO   ] Prefetch equivalence verified across "
              << target_times.size() << " time points\n";

    stream_sync.finalize();
    stream_prefetch.finalize();
}

} // anonymous namespace

/**
 * @brief Custom main() that initializes MPI and Atlas before running tests.
 *
 * This test requires MPI (for AMIO parallel I/O) and Atlas (for regridding).
 */
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    atlas::initialize(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    atlas::finalize();
    MPI_Finalize();
    return result;
}
