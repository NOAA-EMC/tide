/**
 * @file test_memory_budget_props.cpp
 * @brief Property-based tests for the MemoryBudget class.
 *
 * Feature: tide-production-readiness, Property 8: Memory budget enforcement
 * Feature: tide-production-readiness, Property 9: Memory usage reporting matches formula
 *
 * Validates: Requirements 6.2, 6.3, 6.5
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "tide/error.hpp"
#include "tide/memory.hpp"
#include "tide/types.hpp"

#include <cstddef>
#include <numeric>
#include <vector>

namespace tide {
namespace {

// ─── Generators ──────────────────────────────────────────────────────────────

/**
 * @brief Generate a StreamBufferReq with bounded dimensions.
 *
 * Dimensions are kept moderate to avoid overflow in the formula while
 * still exercising realistic ranges. Source cells and target cols are
 * kept in [0, 10000], levels in [0, 200].
 */
rc::Gen<StreamBufferReq> genStreamBufferReq() {
    return rc::gen::apply(
        [](std::size_t src_cells, std::size_t src_levels,
           std::size_t tgt_cols, std::size_t tgt_levels, std::string name) {
            return StreamBufferReq{
                .src_cells = src_cells,
                .src_levels = src_levels,
                .tgt_cols = tgt_cols,
                .tgt_levels = tgt_levels,
                .stream_name = std::move(name),
            };
        },
        rc::gen::inRange<std::size_t>(0, 10001),
        rc::gen::inRange<std::size_t>(0, 201),
        rc::gen::inRange<std::size_t>(0, 10001),
        rc::gen::inRange<std::size_t>(0, 201),
        rc::gen::map(rc::gen::inRange(0, 1000), [](int i) {
            return "stream_" + std::to_string(i);
        }));
}

/**
 * @brief Compute expected bytes for a StreamBufferReq using the design formula.
 *
 * Formula: 2 × src_cells × src_levels × 8 + tgt_cols × tgt_levels × 8 × 3
 */
std::size_t expectedStreamBytes(const StreamBufferReq& req) {
    return 2 * req.src_cells * req.src_levels * 8 +
           req.tgt_cols * req.tgt_levels * 8 * 3;
}

// =============================================================================
// Property 8: Memory budget enforcement
// =============================================================================

/**
 * **Validates: Requirements 6.2, 6.5**
 *
 * For any multi-stream configuration where the computed total buffer
 * requirement exceeds memory_budget_mb × 1048576, initialization SHALL
 * fail with MemoryBudgetExceeded. Conversely, if within budget,
 * initialization SHALL succeed.
 */
RC_GTEST_PROP(MemoryBudgetProperty, P8_MemoryBudgetEnforcement, ()) {
    // Generate 1-8 streams
    auto num_streams = *rc::gen::inRange<std::size_t>(1, 9);
    auto streams = *rc::gen::container<std::vector<StreamBufferReq>>(
        num_streams, genStreamBufferReq());

    // Generate a budget in MB (1 to 100 MB range)
    auto budget_mb = *rc::gen::inRange<std::size_t>(1, 101);

    // Compute expected total bytes
    std::size_t expected_total = 0;
    for (const auto& s : streams) {
        expected_total += expectedStreamBytes(s);
    }

    std::size_t budget_bytes = budget_mb * std::size_t{1048576};

    // Create MemoryBudget and add all streams
    MemoryBudget budget(budget_mb);
    for (const auto& s : streams) {
        budget.add_stream(s);
    }

    auto result = budget.check_budget();

    if (expected_total > budget_bytes) {
        // Should fail with MemoryBudgetExceeded
        RC_ASSERT(!result.has_value());
        RC_ASSERT(result.error().code == to_int(ErrorCode::MemoryBudgetExceeded));
    } else {
        // Should succeed
        RC_ASSERT(result.has_value());
    }
}

/**
 * **Validates: Requirements 6.2, 6.5**
 *
 * When budget_mb is 0 (unbounded mode), check_budget() SHALL always
 * succeed regardless of total buffer requirements.
 */
RC_GTEST_PROP(MemoryBudgetProperty, P8_UnboundedBudgetAlwaysSucceeds, ()) {
    // Generate 1-8 streams with arbitrary dimensions
    auto num_streams = *rc::gen::inRange<std::size_t>(1, 9);
    auto streams = *rc::gen::container<std::vector<StreamBufferReq>>(
        num_streams, genStreamBufferReq());

    // budget_mb = 0 means unbounded
    MemoryBudget budget(0);
    for (const auto& s : streams) {
        budget.add_stream(s);
    }

    auto result = budget.check_budget();
    RC_ASSERT(result.has_value());
    RC_ASSERT(budget.is_unbounded());
}

// =============================================================================
// Property 9: Memory usage reporting matches formula
// =============================================================================

/**
 * **Validates: Requirements 6.3**
 *
 * For any initialized set of streams, total_usage() SHALL equal the sum
 * over all streams of: 2 × src_cells × src_levels × 8 + tgt_cols × tgt_levels × 8 × 3.
 */
RC_GTEST_PROP(MemoryBudgetProperty, P9_MemoryUsageReportingMatchesFormula, ()) {
    // Generate 1-10 streams
    auto num_streams = *rc::gen::inRange<std::size_t>(1, 11);
    auto streams = *rc::gen::container<std::vector<StreamBufferReq>>(
        num_streams, genStreamBufferReq());

    // Use a large budget to avoid interference with test logic
    MemoryBudget budget(100000);

    std::size_t expected_total = 0;
    for (std::size_t i = 0; i < streams.size(); ++i) {
        budget.add_stream(streams[i]);

        // Verify per-stream usage matches formula after each add
        std::size_t expected_stream = expectedStreamBytes(streams[i]);
        RC_ASSERT(budget.stream_usage(i) == expected_stream);
        RC_ASSERT(budget.stream_usage(i) ==
                  MemoryBudget::compute_stream_bytes(streams[i]));

        expected_total += expected_stream;
    }

    // Verify total_usage matches the sum of per-stream computations
    RC_ASSERT(budget.total_usage() == expected_total);
    RC_ASSERT(budget.stream_count() == streams.size());
}

/**
 * **Validates: Requirements 6.3**
 *
 * total_usage() is additive: adding streams one by one accumulates
 * consistently with the static compute_stream_bytes function.
 */
RC_GTEST_PROP(MemoryBudgetProperty, P9_TotalUsageIsAdditive, ()) {
    // Generate 2-6 streams
    auto num_streams = *rc::gen::inRange<std::size_t>(2, 7);
    auto streams = *rc::gen::container<std::vector<StreamBufferReq>>(
        num_streams, genStreamBufferReq());

    MemoryBudget budget(100000);

    std::size_t running_total = 0;
    for (const auto& s : streams) {
        std::size_t stream_bytes = MemoryBudget::compute_stream_bytes(s);
        budget.add_stream(s);
        running_total += stream_bytes;

        // After each addition, total_usage should equal running sum
        RC_ASSERT(budget.total_usage() == running_total);
    }
}

} // namespace
} // namespace tide
