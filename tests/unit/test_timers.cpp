/**
 * @file test_timers.cpp
 * @brief Property-based tests for TIDE StageTimers.
 *
 * Feature: tide-production-readiness, Property 10: Timer accumulation is monotonically non-decreasing
 *
 * Validates: Requirements 7.2, 7.3
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <tide/perf.hpp>

#include <array>
#include <thread>
#include <chrono>

namespace tide::perf {
namespace {

// =============================================================================
// Property 10: Timer accumulation is monotonically non-decreasing
// =============================================================================

/**
 * Property 10a: For any sequence of start/stop pairs on a StageTimers instance,
 * the cumulative elapsed time for each stage is monotonically non-decreasing
 * after each stop() call.
 *
 * **Validates: Requirements 7.2**
 */
RC_GTEST_PROP(TimerProperty, P10_MonotonicallyNonDecreasingAfterStop, ()) {
    StageTimers timers(true);

    // Generate a random number of start/stop operations (1 to 20)
    const auto num_ops = *rc::gen::inRange(1, 21);

    // Track the last-seen cumulative value for each stage
    std::array<double, 5> prev_elapsed{};
    prev_elapsed.fill(0.0);

    for (int i = 0; i < num_ops; ++i) {
        // Pick a random stage
        const auto stage_idx = *rc::gen::inRange(0, 5);
        const auto stage = static_cast<Stage>(stage_idx);

        // Start and stop timing (actual clock will advance between calls)
        timers.start(stage);
        timers.stop(stage);

        // After each stop, the elapsed value must be >= the previous value
        double current = timers.elapsed(stage);
        RC_ASSERT(current >= prev_elapsed[static_cast<std::size_t>(stage_idx)]);
        prev_elapsed[static_cast<std::size_t>(stage_idx)] = current;
    }

    // Verify via get_all too
    double all[5];
    timers.get_all(all);
    for (int i = 0; i < 5; ++i) {
        RC_ASSERT(all[i] >= 0.0);
        RC_ASSERT(all[i] == prev_elapsed[static_cast<std::size_t>(i)]);
    }
}

/**
 * Property 10b: After reset(), all cumulative timer values SHALL be zero.
 *
 * **Validates: Requirements 7.3**
 */
RC_GTEST_PROP(TimerProperty, P10_ResetSetsAllValuesToZero, ()) {
    StageTimers timers(true);

    // Generate random start/stop operations to accumulate time
    const auto num_ops = *rc::gen::inRange(1, 15);

    for (int i = 0; i < num_ops; ++i) {
        const auto stage_idx = *rc::gen::inRange(0, 5);
        const auto stage = static_cast<Stage>(stage_idx);
        timers.start(stage);
        timers.stop(stage);
    }

    // Reset
    timers.reset();

    // All values must be zero
    double all[5];
    timers.get_all(all);
    for (int i = 0; i < 5; ++i) {
        RC_ASSERT(all[i] == 0.0);
    }

    // Also verify per-stage
    RC_ASSERT(timers.elapsed(Stage::IO) == 0.0);
    RC_ASSERT(timers.elapsed(Stage::Temporal) == 0.0);
    RC_ASSERT(timers.elapsed(Stage::Spatial) == 0.0);
    RC_ASSERT(timers.elapsed(Stage::Vertical) == 0.0);
    RC_ASSERT(timers.elapsed(Stage::Scaling) == 0.0);
}

/**
 * Property 10c: When timers are disabled, all elapsed values are always zero
 * regardless of start/stop operations performed.
 *
 * **Validates: Requirements 7.2**
 */
RC_GTEST_PROP(TimerProperty, P10_DisabledTimersAlwaysReturnZero, ()) {
    StageTimers timers(false);

    RC_ASSERT(!timers.is_enabled());

    // Generate random start/stop operations
    const auto num_ops = *rc::gen::inRange(1, 20);

    for (int i = 0; i < num_ops; ++i) {
        const auto stage_idx = *rc::gen::inRange(0, 5);
        const auto stage = static_cast<Stage>(stage_idx);
        timers.start(stage);
        timers.stop(stage);

        // After every operation, elapsed must still be zero
        RC_ASSERT(timers.elapsed(stage) == 0.0);
    }

    // get_all should also return zeros
    double all[5];
    timers.get_all(all);
    for (int i = 0; i < 5; ++i) {
        RC_ASSERT(all[i] == 0.0);
    }
}

} // namespace
} // namespace tide::perf
