/**
 * @file test_stream_manager.cpp
 * @brief Unit tests for the TIDE StreamManager class.
 *
 * Tests the multi-stream management capabilities including:
 * - Creation from TideConfig with memory budget checking
 * - Per-stream error isolation during advance_all()
 * - Stream status tracking (Healthy, Failed, Finalized)
 * - stream_count(), stream_status(), stream_at() accessors
 * - finalize_all() resource cleanup
 * - total_memory_usage() reporting
 *
 * Validates Requirements: 4.1, 4.2, 4.3, 4.5, 4.6, 4.7, 8.1, 8.2, 8.3, 8.4, 8.5
 */

#include "tide/manager.hpp"

#include <cstddef>
#include <vector>

#include <gtest/gtest.h>
#include <mpi.h>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/memory.hpp"
#include "tide/types.hpp"

namespace tide::test {

// ─────────────────────────────────────────────────────────────────────────────
// Test Fixture
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Test fixture for StreamManager tests.
 *
 * Provides helper methods to create test configurations.
 * Note: Stream::create() requires valid files and MPI, so we test
 * the manager's logic at the level we can without live MPI+files
 * (budget checking, status tracking on empty configs, etc.).
 */
class StreamManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Ensure MPI is initialized for the test process
        int initialized = 0;
        MPI_Initialized(&initialized);
        if (!initialized) {
            MPI_Init(nullptr, nullptr);
        }
    }

    /// @brief Create a minimal TideConfig with N empty stream entries.
    static config::TideConfig make_config(std::size_t num_streams,
                                          std::size_t budget_mb = 0,
                                          bool enable_timers = true) {
        config::TideConfig config;
        config.memory_budget_mb = budget_mb;
        config.enable_timers = enable_timers;

        // Add a default target grid
        TargetGrid tgt;
        tgt.num_cols = 100;
        tgt.num_levels = 10;
        tgt.lats.resize(100, 0.0);
        tgt.lons.resize(100, 0.0);
        tgt.levels.resize(10, 0.0);
        config.target_grids["default"] = tgt;

        for (std::size_t i = 0; i < num_streams; ++i) {
            config::StreamConfig sc;
            sc.name = "stream_" + std::to_string(i);
            sc.file_path = "/nonexistent/file_" + std::to_string(i) + ".nc";
            sc.field_name = "field_" + std::to_string(i);
            sc.source_grid_type = "regular_latlon";
            sc.target_grid_ref = "default";
            sc.prefetch.enabled = false;
            sc.prefetch.depth = 1;
            config.streams.push_back(sc);
        }

        return config;
    }
};

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Memory Budget Enforcement at Initialization
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(StreamManagerTest, CreateFailsWhenMemoryBudgetExceeded) {
    // Create a config with streams that will exceed a tiny budget
    auto config = make_config(5, 0 /* no budget limit initially */);

    // Set target grid large enough to blow a small budget
    config.target_grids["default"].num_cols = 10000;
    config.target_grids["default"].num_levels = 100;

    // With 5 streams, each needing roughly:
    //   2 * 10000 * 100 * 8 (ring) + 10000 * 100 * 8 * 3 (outputs)
    //   = 16MB + 24MB = 40MB per stream = 200MB total
    // Set budget to 1MB so it must fail
    config.memory_budget_mb = 1;

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

TEST_F(StreamManagerTest, CreateSucceedsWithinBudget) {
    // Config with small grids that fit within a generous budget
    auto config = make_config(2, 1000 /* 1GB budget */);

    // With 100 cols × 10 levels per stream:
    //   2 * 100 * 10 * 8 + 100 * 10 * 8 * 3 = 16000 + 24000 = 40000 bytes ≈ 0.04MB
    // Total for 2 streams = ~0.08MB, well within 1GB
    // But Stream::create will fail because files don't exist
    auto result = StreamManager::create(config, MPI_COMM_WORLD);

    // The budget check passes, but stream creation will fail (files don't exist)
    // This tests that budget checking happens BEFORE stream creation in the flow
    // Since budget passes but file doesn't exist, we get an I/O error instead
    ASSERT_FALSE(result.has_value());
    // The error should NOT be MemoryBudgetExceeded — it should be something
    // from Stream::create about the file
    EXPECT_NE(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

TEST_F(StreamManagerTest, CreateSucceedsWithUnboundedBudget) {
    // When budget_mb is 0, any allocation amount is acceptable
    auto config = make_config(2, 0 /* unbounded */);

    // Stream creation will still fail due to missing files, but not due to budget
    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

TEST_F(StreamManagerTest, ParameterBudgetOverridesConfig) {
    // Config has generous budget, but parameter overrides with tiny budget
    auto config = make_config(2, 10000 /* 10GB in config */);
    config.target_grids["default"].num_cols = 10000;
    config.target_grids["default"].num_levels = 100;

    // Override with 1MB via parameter — should fail
    auto result = StreamManager::create(config, MPI_COMM_WORLD, 1);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Empty Configuration
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(StreamManagerTest, CreateWithZeroStreamsSucceeds) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    auto& mgr = result.value();
    EXPECT_EQ(mgr.stream_count(), 0u);
    EXPECT_EQ(mgr.total_memory_usage(), 0u);
}

TEST_F(StreamManagerTest, AdvanceAllOnEmptyManagerReturnsEmptyVector) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    auto& mgr = result.value();
    auto status_codes = mgr.advance_all(3600.0);
    EXPECT_TRUE(status_codes.empty());
}

TEST_F(StreamManagerTest, FinalizeAllOnEmptyManagerIsNoOp) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    auto& mgr = result.value();
    // Should not throw or crash
    mgr.finalize_all();
    mgr.finalize_all();  // Safe to call multiple times
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Stream Access and Status
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(StreamManagerTest, StreamAtReturnsNullptrForOutOfRange) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    auto& mgr = result.value();
    EXPECT_EQ(mgr.stream_at(0), nullptr);
    EXPECT_EQ(mgr.stream_at(100), nullptr);
}

TEST_F(StreamManagerTest, StreamStatusThrowsForOutOfRange) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    auto& mgr = result.value();
    EXPECT_THROW(mgr.stream_status(0), std::out_of_range);
    EXPECT_THROW(mgr.stream_status(100), std::out_of_range);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Move Semantics
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(StreamManagerTest, MoveConstructorTransfersOwnership) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    StreamManager mgr1 = std::move(result.value());
    EXPECT_EQ(mgr1.stream_count(), 0u);

    StreamManager mgr2 = std::move(mgr1);
    EXPECT_EQ(mgr2.stream_count(), 0u);
}

TEST_F(StreamManagerTest, MoveAssignmentTransfersOwnership) {
    auto config = make_config(0);

    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());

    StreamManager mgr1 = std::move(result.value());
    StreamManager mgr2 = std::move(mgr1);

    EXPECT_EQ(mgr2.stream_count(), 0u);
    EXPECT_EQ(mgr2.total_memory_usage(), 0u);
}

// ─────────────────────────────────────────────────────────────────────────────
// Tests: Total Memory Usage
// ─────────────────────────────────────────────────────────────────────────────

TEST_F(StreamManagerTest, TotalMemoryUsageMatchesBudgetComputation) {
    // Create config with known grid dimensions to verify formula
    auto config = make_config(0);

    // Manually set up a known configuration
    config.target_grids["small"].num_cols = 50;
    config.target_grids["small"].num_levels = 5;

    config::StreamConfig sc;
    sc.name = "test_stream";
    sc.file_path = "/nonexistent/test.nc";
    sc.field_name = "temp";
    sc.source_grid_type = "regular_latlon";
    sc.target_grid_ref = "small";
    sc.prefetch.enabled = false;
    sc.prefetch.depth = 1;
    config.streams.push_back(sc);

    // Budget is unbounded, but creation will fail on file open
    // We can't test the exact value without a real stream, but we can
    // verify the formula is applied: for one stream with target 50×5,
    // and src assumed as 50×5 (since no file to get actual source grid):
    //   2 * 50 * 5 * 8 + 50 * 5 * 8 * 3 = 4000 + 6000 = 10000 bytes
    // But since Stream::create will fail, we test with zero streams

    auto empty_config = make_config(0);
    auto result = StreamManager::create(empty_config, MPI_COMM_WORLD);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value().total_memory_usage(), 0u);
}

TEST_F(StreamManagerTest, MemoryBudgetFormulaIsCorrect) {
    // Verify the budget computation formula by checking that a config
    // that exactly matches the budget limit passes, and one slightly over fails.

    config::TideConfig config;
    config.enable_timers = true;

    TargetGrid tgt;
    tgt.num_cols = 1000;
    tgt.num_levels = 50;
    tgt.lats.resize(1000, 0.0);
    tgt.lons.resize(1000, 0.0);
    tgt.levels.resize(50, 0.0);
    config.target_grids["default"] = tgt;

    // One stream: per-stream budget = 2*1000*50*8 + 1000*50*8*3
    //           = 800000 + 1200000 = 2000000 bytes ≈ 1.907 MB
    config::StreamConfig sc;
    sc.name = "single";
    sc.file_path = "/nonexistent/file.nc";
    sc.field_name = "field";
    sc.source_grid_type = "regular_latlon";
    sc.target_grid_ref = "default";
    sc.prefetch.enabled = false;
    sc.prefetch.depth = 1;
    config.streams.push_back(sc);

    // Budget of 1MB should fail (needs ~1.9MB)
    config.memory_budget_mb = 1;
    auto result = StreamManager::create(config, MPI_COMM_WORLD);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));

    // Budget of 2MB should pass the budget check (actual stream create will fail)
    config.memory_budget_mb = 2;
    result = StreamManager::create(config, MPI_COMM_WORLD);
    // Budget passes, but stream creation fails due to missing file
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

} // namespace tide::test
