/**
 * @file test_prefetch.cpp
 * @brief Unit tests for the TIDE PrefetchManager.
 *
 * Tests the PrefetchManager's state machine behavior including:
 * - Disabled mode (all operations are no-ops)
 * - Enabled mode (issue/wait lifecycle)
 * - Configuration accessors (is_enabled, depth, has_pending)
 * - Fallback to synchronous read on prefetch failure
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <gtest/gtest.h>

#include "tide/prefetch.hpp"
#include "tide/error.hpp"

namespace tide::prefetch {
namespace {

// ─── Disabled Mode Tests ─────────────────────────────────────────────────────

TEST(PrefetchManagerDisabled, IsNotEnabled) {
    PrefetchConfig config{.enabled = false, .depth = 1};
    PrefetchManager mgr(config);

    EXPECT_FALSE(mgr.is_enabled());
}

TEST(PrefetchManagerDisabled, HasNoPending) {
    PrefetchConfig config{.enabled = false, .depth = 1};
    PrefetchManager mgr(config);

    EXPECT_FALSE(mgr.has_pending());
}

TEST(PrefetchManagerDisabled, WaitReturnsZero) {
    PrefetchConfig config{.enabled = false, .depth = 1};
    PrefetchManager mgr(config);

    // wait_prefetch on disabled manager should be no-op returning 0
    EXPECT_EQ(mgr.wait_prefetch(), 0);
}

TEST(PrefetchManagerDisabled, DepthReturnsConfigured) {
    PrefetchConfig config{.enabled = false, .depth = 3};
    PrefetchManager mgr(config);

    EXPECT_EQ(mgr.depth(), 3);
}

TEST(PrefetchManagerDisabled, IssueIsNoOp) {
    PrefetchConfig config{.enabled = false, .depth = 1};
    PrefetchManager mgr(config);

    // Create a dummy reader (won't be used since disabled)
    io::AmioReader reader;
    std::vector<double> buffer(100, 0.0);

    // issue_prefetch on disabled manager should return 0 without doing anything
    int rc = mgr.issue_prefetch(reader, "temperature", 0, buffer);
    EXPECT_EQ(rc, 0);
    EXPECT_FALSE(mgr.has_pending());
}

// ─── Enabled Mode Tests ──────────────────────────────────────────────────────

TEST(PrefetchManagerEnabled, IsEnabled) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    EXPECT_TRUE(mgr.is_enabled());
}

TEST(PrefetchManagerEnabled, DefaultNoPending) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    EXPECT_FALSE(mgr.has_pending());
}

TEST(PrefetchManagerEnabled, WaitWithNoPendingReturnsZero) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    // Waiting when nothing is pending should succeed immediately
    EXPECT_EQ(mgr.wait_prefetch(), 0);
}

TEST(PrefetchManagerEnabled, DepthReturnsConfigured) {
    PrefetchConfig config{.enabled = true, .depth = 4};
    PrefetchManager mgr(config);

    EXPECT_EQ(mgr.depth(), 4);
}

TEST(PrefetchManagerEnabled, IssueSetsPending) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    // The reader is uninitialized (not open), so the async read will fail.
    // But issue_prefetch should still accept the request and set pending state.
    io::AmioReader reader;
    std::vector<double> buffer(10, 0.0);

    int rc = mgr.issue_prefetch(reader, "temperature", 0, buffer);
    EXPECT_EQ(rc, 0);
    EXPECT_TRUE(mgr.has_pending());
}

TEST(PrefetchManagerEnabled, DoubleIssueFails) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    io::AmioReader reader;
    std::vector<double> buffer(10, 0.0);

    // First issue should succeed
    int rc1 = mgr.issue_prefetch(reader, "temperature", 0, buffer);
    EXPECT_EQ(rc1, 0);

    // Second issue without waiting should fail
    int rc2 = mgr.issue_prefetch(reader, "temperature", 1, buffer);
    EXPECT_NE(rc2, 0);
    EXPECT_EQ(rc2, to_int(ErrorCode::PrefetchFailed));
}

TEST(PrefetchManagerEnabled, WaitClearsPending) {
    PrefetchConfig config{.enabled = true, .depth = 1};
    PrefetchManager mgr(config);

    io::AmioReader reader;
    std::vector<double> buffer(10, 0.0);

    mgr.issue_prefetch(reader, "temperature", 0, buffer);
    EXPECT_TRUE(mgr.has_pending());

    // Wait completes (even if the underlying read fails, fallback runs)
    mgr.wait_prefetch();
    EXPECT_FALSE(mgr.has_pending());
}

// ─── Configuration Tests ─────────────────────────────────────────────────────

TEST(PrefetchConfig, DefaultValues) {
    PrefetchConfig config;
    EXPECT_FALSE(config.enabled);
    EXPECT_EQ(config.depth, 1);
}

TEST(PrefetchConfig, CustomDepth) {
    PrefetchConfig config{.enabled = true, .depth = 5};
    EXPECT_TRUE(config.enabled);
    EXPECT_EQ(config.depth, 5);
}

// ─── Move Semantics Tests ────────────────────────────────────────────────────

TEST(PrefetchManagerMove, MoveConstruction) {
    PrefetchConfig config{.enabled = true, .depth = 2};
    PrefetchManager mgr(config);

    PrefetchManager moved(std::move(mgr));
    EXPECT_TRUE(moved.is_enabled());
    EXPECT_EQ(moved.depth(), 2);
    EXPECT_FALSE(moved.has_pending());
}

TEST(PrefetchManagerMove, MoveAssignment) {
    PrefetchConfig config1{.enabled = true, .depth = 3};
    PrefetchConfig config2{.enabled = false, .depth = 1};

    PrefetchManager mgr1(config1);
    PrefetchManager mgr2(config2);

    mgr2 = std::move(mgr1);
    EXPECT_TRUE(mgr2.is_enabled());
    EXPECT_EQ(mgr2.depth(), 3);
}

} // namespace
} // namespace tide::prefetch
