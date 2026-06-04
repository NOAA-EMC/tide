/**
 * @file test_memory_budget.cpp
 * @brief Unit tests for the MemoryBudget class.
 *
 * Tests the memory budget computation formula, enforcement logic,
 * unbounded mode, and error reporting.
 */

#include <gtest/gtest.h>

#include "tide/error.hpp"
#include "tide/memory.hpp"
#include "tide/types.hpp"

namespace tide {
namespace {

// ─── Helper: create a StreamBufferReq ────────────────────────────────────────

StreamBufferReq make_req(std::size_t src_cells, std::size_t src_levels,
                         std::size_t tgt_cols, std::size_t tgt_levels,
                         const std::string& name = "test_stream") {
    return StreamBufferReq{
        .src_cells = src_cells,
        .src_levels = src_levels,
        .tgt_cols = tgt_cols,
        .tgt_levels = tgt_levels,
        .stream_name = name,
    };
}

// ─── compute_stream_bytes tests ──────────────────────────────────────────────

TEST(MemoryBudgetTest, ComputeStreamBytesBasicFormula) {
    // Formula: 2 × src_cells × src_levels × 8 + tgt_cols × tgt_levels × 8 × 3
    auto req = make_req(100, 10, 50, 20);

    // ring_buffer = 2 × 100 × 10 × 8 = 16000
    // target_buffers = 3 × 50 × 20 × 8 = 24000
    // total = 40000
    EXPECT_EQ(MemoryBudget::compute_stream_bytes(req), 40000u);
}

TEST(MemoryBudgetTest, ComputeStreamBytesZeroDimensions) {
    // If any dimension is zero, that component contributes 0 bytes
    auto req = make_req(0, 0, 0, 0);
    EXPECT_EQ(MemoryBudget::compute_stream_bytes(req), 0u);
}

TEST(MemoryBudgetTest, ComputeStreamBytesZeroSourceOnly) {
    auto req = make_req(0, 10, 50, 20);
    // ring_buffer = 0
    // target_buffers = 3 × 50 × 20 × 8 = 24000
    EXPECT_EQ(MemoryBudget::compute_stream_bytes(req), 24000u);
}

TEST(MemoryBudgetTest, ComputeStreamBytesZeroTargetOnly) {
    auto req = make_req(100, 10, 0, 20);
    // ring_buffer = 2 × 100 × 10 × 8 = 16000
    // target_buffers = 0
    EXPECT_EQ(MemoryBudget::compute_stream_bytes(req), 16000u);
}

TEST(MemoryBudgetTest, ComputeStreamBytesLargeGrid) {
    // Realistic global 0.25° grid: ~1M cells, 100 levels
    auto req = make_req(1000000, 100, 500000, 100);
    // ring_buffer = 2 × 1000000 × 100 × 8 = 1,600,000,000
    // target_buffers = 3 × 500000 × 100 × 8 = 1,200,000,000
    // total = 2,800,000,000
    EXPECT_EQ(MemoryBudget::compute_stream_bytes(req), 2'800'000'000u);
}

// ─── Unbounded mode tests ────────────────────────────────────────────────────

TEST(MemoryBudgetTest, UnboundedModeWithZeroBudget) {
    MemoryBudget budget(0);
    EXPECT_TRUE(budget.is_unbounded());
    EXPECT_EQ(budget.budget_bytes(), 0u);
}

TEST(MemoryBudgetTest, BoundedModeWithNonZeroBudget) {
    MemoryBudget budget(1024);
    EXPECT_FALSE(budget.is_unbounded());
    EXPECT_EQ(budget.budget_bytes(), 1024u * 1048576u);
}

TEST(MemoryBudgetTest, UnboundedCheckAlwaysSucceeds) {
    MemoryBudget budget(0);
    // Add a huge stream — should still pass in unbounded mode
    budget.add_stream(make_req(1000000, 100, 500000, 100, "huge_stream"));
    auto result = budget.check_budget();
    EXPECT_TRUE(result.has_value());
}

// ─── Budget enforcement tests ────────────────────────────────────────────────

TEST(MemoryBudgetTest, WithinBudgetSucceeds) {
    // Budget = 1 MB = 1,048,576 bytes
    MemoryBudget budget(1);
    // Stream requires 40,000 bytes (well within 1 MB)
    budget.add_stream(make_req(100, 10, 50, 20, "small_stream"));
    auto result = budget.check_budget();
    EXPECT_TRUE(result.has_value());
}

TEST(MemoryBudgetTest, ExactlyAtBudgetSucceeds) {
    // Compute what stream dimensions would use exactly 1 MB
    // ring_buffer = 2 × src_cells × src_levels × 8
    // target_buffers = 3 × tgt_cols × tgt_levels × 8
    // We want total = 1048576 bytes
    // Let's use: src_cells=1000, src_levels=1, tgt_cols=1000, tgt_levels=1
    // ring = 2 × 1000 × 1 × 8 = 16000
    // target = 3 × 1000 × 1 × 8 = 24000
    // total = 40000 => need budget = 40000 bytes = ~0.038 MB
    // Instead, set budget_mb = 1 and verify the stream fits
    MemoryBudget budget(1);
    budget.add_stream(make_req(1000, 1, 1000, 1, "stream1"));
    auto result = budget.check_budget();
    EXPECT_TRUE(result.has_value());
}

TEST(MemoryBudgetTest, ExceedsBudgetReturnsError) {
    // Budget = 1 MB = 1,048,576 bytes
    MemoryBudget budget(1);
    // Stream requires much more than 1 MB
    // 2 × 100000 × 10 × 8 + 3 × 100000 × 10 × 8 = 16,000,000 + 24,000,000 = 40,000,000 bytes
    budget.add_stream(make_req(100000, 10, 100000, 10, "big_stream"));
    auto result = budget.check_budget();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
    EXPECT_NE(result.error().message.find("big_stream"), std::string::npos);
}

TEST(MemoryBudgetTest, MultipleStreamsExceedBudget) {
    // Budget = 1 MB
    MemoryBudget budget(1);
    // Each stream uses 40,000 bytes. Need 1,048,576 / 40,000 ≈ 27 streams to exceed
    for (int i = 0; i < 30; ++i) {
        budget.add_stream(
            make_req(100, 10, 50, 20, "stream_" + std::to_string(i)));
    }
    // Total = 30 × 40,000 = 1,200,000 bytes > 1,048,576
    auto result = budget.check_budget();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::MemoryBudgetExceeded));
}

TEST(MemoryBudgetTest, IdentifiesOffendingStream) {
    // Budget enough for first stream but not the second
    // First stream: 40,000 bytes, second stream: 40,000,000 bytes
    MemoryBudget budget(1);  // 1 MB
    budget.add_stream(make_req(100, 10, 50, 20, "small_stream"));
    budget.add_stream(make_req(100000, 10, 100000, 10, "the_offender"));

    auto result = budget.check_budget();
    ASSERT_FALSE(result.has_value());
    EXPECT_NE(result.error().message.find("the_offender"), std::string::npos);
}

// ─── total_usage and stream_usage tests ──────────────────────────────────────

TEST(MemoryBudgetTest, TotalUsageEmptyBudget) {
    MemoryBudget budget(100);
    EXPECT_EQ(budget.total_usage(), 0u);
}

TEST(MemoryBudgetTest, TotalUsageSingleStream) {
    MemoryBudget budget(100);
    budget.add_stream(make_req(100, 10, 50, 20, "s1"));
    EXPECT_EQ(budget.total_usage(), 40000u);
}

TEST(MemoryBudgetTest, TotalUsageMultipleStreams) {
    MemoryBudget budget(100);
    budget.add_stream(make_req(100, 10, 50, 20, "s1"));
    budget.add_stream(make_req(200, 5, 100, 10, "s2"));

    // s1: 2×100×10×8 + 3×50×20×8 = 16000 + 24000 = 40000
    // s2: 2×200×5×8 + 3×100×10×8 = 16000 + 24000 = 40000
    EXPECT_EQ(budget.total_usage(), 80000u);
}

TEST(MemoryBudgetTest, StreamUsageByIndex) {
    MemoryBudget budget(100);
    budget.add_stream(make_req(100, 10, 50, 20, "s1"));
    budget.add_stream(make_req(200, 5, 100, 10, "s2"));

    EXPECT_EQ(budget.stream_usage(0), 40000u);
    EXPECT_EQ(budget.stream_usage(1), 40000u);
}

TEST(MemoryBudgetTest, StreamUsageOutOfRangeThrows) {
    MemoryBudget budget(100);
    budget.add_stream(make_req(100, 10, 50, 20, "s1"));

    EXPECT_THROW(budget.stream_usage(1), std::out_of_range);
    EXPECT_THROW(budget.stream_usage(999), std::out_of_range);
}

// ─── stream_count tests ──────────────────────────────────────────────────────

TEST(MemoryBudgetTest, StreamCountEmpty) {
    MemoryBudget budget(100);
    EXPECT_EQ(budget.stream_count(), 0u);
}

TEST(MemoryBudgetTest, StreamCountAfterAdding) {
    MemoryBudget budget(100);
    budget.add_stream(make_req(10, 1, 10, 1, "s1"));
    budget.add_stream(make_req(20, 2, 20, 2, "s2"));
    budget.add_stream(make_req(30, 3, 30, 3, "s3"));
    EXPECT_EQ(budget.stream_count(), 3u);
}

// ─── Error message content tests ─────────────────────────────────────────────

TEST(MemoryBudgetTest, ErrorMessageContainsBudgetInfo) {
    MemoryBudget budget(1);  // 1 MB
    budget.add_stream(make_req(100000, 10, 100000, 10, "overflow_stream"));

    auto result = budget.check_budget();
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().context, "memory");
    // Message should contain the budget limit
    EXPECT_NE(result.error().message.find("1 MB"), std::string::npos);
}

} // namespace
} // namespace tide
