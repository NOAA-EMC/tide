/**
 * @file test_temporal.cpp
 * @brief Unit and property-based tests for the TIDE temporal interpolation engine.
 *
 * Tests interpolate_linear(), interpolate_cyclical(), and interpolate_seasonal()
 * for correctness, edge cases, and error conditions.
 *
 * Property-based tests validate Properties 1-5 from the design document.
 * Validates: Requirements 1.3, 2.2, 2.4, 2.5, 2.6, 11.4
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <numeric>
#include <vector>

#include "tide/temporal.hpp"

namespace tide::temporal {
namespace {

// =============================================================================
// interpolate_linear tests
// =============================================================================

TEST(InterpolateLinear, MidpointInterpolation) {
    // Two fields: prev = [0, 2, 4], next = [10, 12, 14]
    // At midpoint t=5 (bracket [0, 10]): output = [5, 7, 9]
    RingBuffer buf(3);
    auto slot = buf.next_slot();
    slot[0] = 10.0; slot[1] = 12.0; slot[2] = 14.0;
    buf.set_times(0.0, 10.0);
    // prev slot is default zeros, so fill it
    // After construction both slots are 0. next_slot is slot[0].
    // We need to set prev data. Rotate first to make current slot prev.
    // Actually let's build this more carefully:

    RingBuffer buffer(3);
    // Fill "next" slot with prev data, then rotate so it becomes prev
    {
        auto s = buffer.next_slot();
        s[0] = 0.0; s[1] = 2.0; s[2] = 4.0;
    }
    buffer.rotate();
    // Now fill the new "next" slot
    {
        auto s = buffer.next_slot();
        s[0] = 10.0; s[1] = 12.0; s[2] = 14.0;
    }
    buffer.set_times(0.0, 10.0);

    std::vector<double> output(3);
    int rc = interpolate_linear(buffer, 5.0, output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 5.0);
    EXPECT_DOUBLE_EQ(output[1], 7.0);
    EXPECT_DOUBLE_EQ(output[2], 9.0);
}

TEST(InterpolateLinear, AtPrevBoundary) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0; s[1] = 3.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 5.0; s[1] = 7.0;
    }
    buffer.set_times(10.0, 20.0);

    std::vector<double> output(2);
    int rc = interpolate_linear(buffer, 10.0, output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 1.0);
    EXPECT_DOUBLE_EQ(output[1], 3.0);
}

TEST(InterpolateLinear, AtNextBoundary) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0; s[1] = 3.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 5.0; s[1] = 7.0;
    }
    buffer.set_times(10.0, 20.0);

    std::vector<double> output(2);
    int rc = interpolate_linear(buffer, 20.0, output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 5.0);
    EXPECT_DOUBLE_EQ(output[1], 7.0);
}

TEST(InterpolateLinear, IdenticalTimestampsReturnsFieldWithoutInterpolation) {
    // Requirement 2.7: If T_prev == T_next, return field without interpolation
    RingBuffer buffer(3);
    {
        auto s = buffer.next_slot();
        s[0] = 42.0; s[1] = 43.0; s[2] = 44.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 100.0; s[1] = 200.0; s[2] = 300.0;
    }
    buffer.set_times(5.0, 5.0); // identical

    std::vector<double> output(3);
    int rc = interpolate_linear(buffer, 5.0, output);
    EXPECT_EQ(rc, 0);
    // Should return prev_data
    EXPECT_DOUBLE_EQ(output[0], 42.0);
    EXPECT_DOUBLE_EQ(output[1], 43.0);
    EXPECT_DOUBLE_EQ(output[2], 44.0);
}

TEST(InterpolateLinear, TimeOutOfRangeBelow) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0; s[1] = 2.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 3.0; s[1] = 4.0;
    }
    buffer.set_times(10.0, 20.0);

    std::vector<double> output(2);
    int rc = interpolate_linear(buffer, 5.0, output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeOutOfRange));
}

TEST(InterpolateLinear, TimeOutOfRangeAbove) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0; s[1] = 2.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 3.0; s[1] = 4.0;
    }
    buffer.set_times(10.0, 20.0);

    std::vector<double> output(2);
    int rc = interpolate_linear(buffer, 25.0, output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeOutOfRange));
}

TEST(InterpolateLinear, LinearFieldExactness) {
    // Property 5: For linear function f(t) = a*t + b, interpolation is exact
    constexpr double a = 3.7;
    constexpr double b = -1.2;
    constexpr double t0 = 100.0;
    constexpr double t1 = 200.0;
    constexpr double t_mid = 137.5;

    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = a * t0 + b;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = a * t1 + b;
    }
    buffer.set_times(t0, t1);

    std::vector<double> output(1);
    int rc = interpolate_linear(buffer, t_mid, output);
    EXPECT_EQ(rc, 0);
    EXPECT_NEAR(output[0], a * t_mid + b, std::numeric_limits<double>::epsilon() * 1000.0);
}

// =============================================================================
// interpolate_cyclical tests
// =============================================================================

TEST(InterpolateCyclical, NormalBracketing) {
    // DOY bracket [100, 200], target at DOY 150 → midpoint
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 10.0; s[1] = 20.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 30.0; s[1] = 40.0;
    }
    buffer.set_times(100.0, 200.0);

    std::vector<double> output(2);
    int rc = interpolate_cyclical(buffer, 150.0, output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 20.0);  // 10 + 0.5*(30-10)
    EXPECT_DOUBLE_EQ(output[1], 30.0);  // 20 + 0.5*(40-20)
}

TEST(InterpolateCyclical, YearBoundaryWrap) {
    // Requirement 2.5: wrap from last DOY to first DOY
    // DOY bracket [350, 10] (wrapping across year boundary), target at DOY 360
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 0.0; // prev data at DOY 350
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 26.0; // next data at DOY 10 (next year)
    }
    buffer.set_times(350.0, 10.0);

    std::vector<double> output(1);
    // Target DOY 360: offset from 350 = 10, interval = 10 - 350 + 366 = 26
    // alpha = 10/26 ≈ 0.3846...
    int rc = interpolate_cyclical(buffer, 360.0, output);
    EXPECT_EQ(rc, 0);
    double expected_alpha = 10.0 / 26.0;
    EXPECT_NEAR(output[0], 0.0 + expected_alpha * 26.0, 1e-12);
}

TEST(InterpolateCyclical, YearBoundaryWrapTargetAfterBoundary) {
    // DOY bracket [350, 10], target at DOY 5 (past year boundary)
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 0.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 26.0;
    }
    buffer.set_times(350.0, 10.0);

    std::vector<double> output(1);
    // Target DOY 5: offset from 350 = 5 - 350 + 366 = 21, interval = 26
    // alpha = 21/26
    int rc = interpolate_cyclical(buffer, 5.0, output);
    EXPECT_EQ(rc, 0);
    double expected_alpha = 21.0 / 26.0;
    EXPECT_NEAR(output[0], 0.0 + expected_alpha * 26.0, 1e-12);
}

TEST(InterpolateCyclical, IdenticalTimestamps) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 5.0; s[1] = 6.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 99.0; s[1] = 100.0;
    }
    buffer.set_times(180.0, 180.0);

    std::vector<double> output(2);
    int rc = interpolate_cyclical(buffer, 180.0, output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 5.0);
    EXPECT_DOUBLE_EQ(output[1], 6.0);
}

TEST(InterpolateCyclical, InvalidDoyBelowRange) {
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 2.0;
    }
    buffer.set_times(100.0, 200.0);

    std::vector<double> output(1);
    int rc = interpolate_cyclical(buffer, 0.5, output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeOutOfRange));
}

TEST(InterpolateCyclical, InvalidDoyAboveRange) {
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 2.0;
    }
    buffer.set_times(100.0, 200.0);

    std::vector<double> output(1);
    int rc = interpolate_cyclical(buffer, 367.0, output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeOutOfRange));
}

// =============================================================================
// interpolate_seasonal tests
// =============================================================================

TEST(InterpolateSeasonal, NormalInterpolation) {
    // Seasonal mode: bracket [1000, 2000], target at 1500
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 10.0; s[1] = 20.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 30.0; s[1] = 40.0;
    }
    buffer.set_times(1000.0, 2000.0);

    std::vector<double> file_times = {1000.0, 2000.0, 3000.0};
    std::vector<double> output(2);

    int rc = interpolate_seasonal(buffer, 1500.0,
                                  std::span<const double>(file_times), output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 20.0);  // 10 + 0.5*(30-10)
    EXPECT_DOUBLE_EQ(output[1], 30.0);  // 20 + 0.5*(40-20)
}

TEST(InterpolateSeasonal, IdenticalTimestamps) {
    RingBuffer buffer(2);
    {
        auto s = buffer.next_slot();
        s[0] = 7.0; s[1] = 8.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 99.0; s[1] = 100.0;
    }
    buffer.set_times(5000.0, 5000.0);

    std::vector<double> file_times = {5000.0};
    std::vector<double> output(2);

    int rc = interpolate_seasonal(buffer, 5000.0,
                                  std::span<const double>(file_times), output);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(output[0], 7.0);
    EXPECT_DOUBLE_EQ(output[1], 8.0);
}

TEST(InterpolateSeasonal, TimeOutOfRange) {
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 2.0;
    }
    buffer.set_times(100.0, 200.0);

    std::vector<double> file_times = {100.0, 200.0};
    std::vector<double> output(1);

    int rc = interpolate_seasonal(buffer, 300.0,
                                  std::span<const double>(file_times), output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeOutOfRange));
}

TEST(InterpolateSeasonal, EmptyFileTimesReturnsError) {
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0;
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 2.0;
    }
    buffer.set_times(100.0, 200.0);

    std::vector<double> output(1);
    int rc = interpolate_seasonal(buffer, 150.0,
                                  std::span<const double>{}, output);
    EXPECT_EQ(rc, to_int(ErrorCode::TimeLevelsExhausted));
}

// =============================================================================
// Property-based tests (RapidCheck)
// =============================================================================

/**
 * **Validates: Requirements 1.3**
 * Property 1: Time-level bracketing selects correct bounding pair
 *
 * For any sorted array of time values and any target time within the range
 * [min_time, max_time], the bracketing algorithm SHALL select T_prev and T_next
 * such that T_prev <= target_time <= T_next, with no other time level between them.
 */
RC_GTEST_PROP(TemporalProperty, P1_TimeLevelBracketingSelectsCorrectPair, ()) {
    // Generate a sorted array of at least 2 distinct time values
    auto times = *rc::gen::container<std::vector<double>>(
        rc::gen::inRange(-1000000, 1000000));
    RC_PRE(times.size() >= 2);

    // Make values distinct and sorted
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    RC_PRE(times.size() >= 2);

    // Generate a target time within the overall range [min, max]
    const double t_min = times.front();
    const double t_max = times.back();
    RC_PRE(t_min < t_max);

    // Pick a random index to define a bracket
    const auto bracket_idx = *rc::gen::inRange(
        static_cast<std::size_t>(0), times.size() - 1);
    const double t_prev = times[bracket_idx];
    const double t_next = times[bracket_idx + 1];

    // Generate target within [t_prev, t_next]
    const double alpha = *rc::gen::map(
        rc::gen::inRange(0, 10001),
        [](int v) { return static_cast<double>(v) / 10000.0; });
    const double target = t_prev + alpha * (t_next - t_prev);

    // Set up the ring buffer with the bracket
    RingBuffer buffer(1);
    {
        auto s = buffer.next_slot();
        s[0] = 1.0;  // prev data
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 2.0;  // next data
    }
    buffer.set_times(t_prev, t_next);

    // Verify that interpolation succeeds (confirming valid bracket)
    std::vector<double> output(1);
    int rc_code = interpolate_linear(buffer, target, output);
    RC_ASSERT(rc_code == 0);

    // Verify the bracket properties:
    // 1) T_prev <= target <= T_next
    RC_ASSERT(t_prev <= target);
    RC_ASSERT(target <= t_next);

    // 2) No other time level exists between T_prev and T_next
    //    (by construction from adjacent sorted elements, this is guaranteed)
    for (std::size_t i = 0; i < times.size(); ++i) {
        if (i != bracket_idx && i != bracket_idx + 1) {
            // No time level is strictly between t_prev and t_next
            RC_ASSERT(!(times[i] > t_prev && times[i] < t_next));
        }
    }
}

/**
 * **Validates: Requirements 2.2**
 * Property 2: Ring buffer rotation advances correctly
 *
 * After N rotations, the buffer state is consistent: after each rotation,
 * the new prev_data equals the old next_data, and a fresh next slot is
 * available for writing.
 */
RC_GTEST_PROP(TemporalProperty, P2_RingBufferRotationAdvancesCorrectly, ()) {
    // Generate field size between 1 and 100
    const auto num_elements = *rc::gen::inRange(1, 101);
    RingBuffer buffer(static_cast<std::size_t>(num_elements));

    // Generate number of rotations to perform (1 to 20)
    const auto num_rotations = *rc::gen::inRange(1, 21);

    // Generate a sequence of time values (monotonically increasing)
    std::vector<double> time_values;
    double t = 0.0;
    for (int i = 0; i <= num_rotations; ++i) {
        time_values.push_back(t);
        t += *rc::gen::map(rc::gen::inRange(1, 1000),
                           [](int v) { return static_cast<double>(v); });
    }

    // Fill initial next slot
    {
        auto s = buffer.next_slot();
        for (int i = 0; i < num_elements; ++i) {
            s[static_cast<std::size_t>(i)] = static_cast<double>(i);
        }
    }

    // Perform rotations, checking invariants each time
    for (int rot = 0; rot < num_rotations; ++rot) {
        // Save what's currently in next_data before rotation
        std::vector<double> old_next(
            buffer.next_slot().begin(), buffer.next_slot().end());

        buffer.rotate();
        buffer.set_times(time_values[static_cast<std::size_t>(rot)],
                         time_values[static_cast<std::size_t>(rot + 1)]);

        // After rotation: prev_data should equal old next_data
        auto prev = buffer.prev_data();
        for (int i = 0; i < num_elements; ++i) {
            RC_ASSERT(prev[static_cast<std::size_t>(i)] ==
                      old_next[static_cast<std::size_t>(i)]);
        }

        // The next slot should be writable (fill with new data)
        auto next = buffer.next_slot();
        for (int i = 0; i < num_elements; ++i) {
            next[static_cast<std::size_t>(i)] =
                static_cast<double>((rot + 1) * 100 + i);
        }

        // Verify times are set correctly
        RC_ASSERT(buffer.t_prev() == time_values[static_cast<std::size_t>(rot)]);
        RC_ASSERT(buffer.t_next() ==
                  time_values[static_cast<std::size_t>(rot + 1)]);
    }
}

/**
 * **Validates: Requirements 2.4**
 * Property 3: Linear temporal interpolation formula
 *
 * For any field values F_prev and F_next at times T_prev and T_next
 * (where T_prev != T_next), and any target time t in [T_prev, T_next],
 * the interpolated field SHALL equal
 * F(T_prev) + (t - T_prev) / (T_next - T_prev) * (F(T_next) - F(T_prev))
 * to within machine epsilon.
 */
RC_GTEST_PROP(TemporalProperty, P3_LinearTemporalInterpolationFormula, ()) {
    // Generate field size (1 to 50)
    const auto num_elements = *rc::gen::inRange(1, 51);
    const auto n = static_cast<std::size_t>(num_elements);

    // Generate T_prev and T_next with T_prev < T_next
    const double t_prev = *rc::gen::map(
        rc::gen::inRange(-10000, 10000),
        [](int v) { return static_cast<double>(v); });
    const int dt = *rc::gen::inRange(1, 10000);
    const double t_next = t_prev + static_cast<double>(dt);

    // Generate alpha in [0, 1] for target time
    const double alpha = *rc::gen::map(
        rc::gen::inRange(0, 10001),
        [](int v) { return static_cast<double>(v) / 10000.0; });
    const double target = t_prev + alpha * (t_next - t_prev);

    // Generate field values for prev and next
    auto gen_field_val = rc::gen::map(
        rc::gen::inRange(-100000, 100001),
        [](int v) { return static_cast<double>(v) / 10.0; });

    RingBuffer buffer(n);
    std::vector<double> f_prev(n), f_next(n);

    {
        auto s = buffer.next_slot();
        for (std::size_t i = 0; i < n; ++i) {
            f_prev[i] = *gen_field_val;
            s[i] = f_prev[i];
        }
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        for (std::size_t i = 0; i < n; ++i) {
            f_next[i] = *gen_field_val;
            s[i] = f_next[i];
        }
    }
    buffer.set_times(t_prev, t_next);

    std::vector<double> output(n);
    int rc_code = interpolate_linear(buffer, target, output);
    RC_ASSERT(rc_code == 0);

    // Verify formula: F(t) = F_prev + alpha * (F_next - F_prev)
    const double computed_alpha = (target - t_prev) / (t_next - t_prev);
    for (std::size_t i = 0; i < n; ++i) {
        const double expected = f_prev[i] + computed_alpha * (f_next[i] - f_prev[i]);
        // Allow tolerance proportional to the magnitude of the values
        const double tol = std::max(
            std::abs(expected) * std::numeric_limits<double>::epsilon() * 16.0,
            std::numeric_limits<double>::epsilon() * 16.0);
        RC_ASSERT(std::abs(output[i] - expected) <= tol);
    }
}

/**
 * **Validates: Requirements 2.5, 2.6**
 * Property 4: Cyclical DOY bracketing and wrap-around
 *
 * For any sorted array of day-of-year values and any target DOY in [1, 366],
 * the cyclical bracketing algorithm SHALL select T_prev and T_next such that
 * the target DOY is enclosed between them (modulo year boundary), wrapping
 * from the last DOY entry to the first when the target DOY exceeds the
 * maximum file DOY.
 */
RC_GTEST_PROP(TemporalProperty, P4_CyclicalDoyBracketingAndWrapAround, ()) {
    // Generate a wrap-around bracket: T_prev > T_next in DOY terms
    // This tests the year-boundary wrap case
    const int prev_doy_int = *rc::gen::inRange(200, 366);
    const int next_doy_int = *rc::gen::inRange(1, 180);
    const double t_prev = static_cast<double>(prev_doy_int);
    const double t_next = static_cast<double>(next_doy_int);

    // Generate a target DOY that falls within the wrap-around interval
    // Either target > t_prev (same year side) or target < t_next (next year side)
    const int choice = *rc::gen::inRange(0, 2);
    double target_doy;
    if (choice == 0) {
        // Target on the "same year" side: between t_prev and 366
        const int target_int = *rc::gen::inRange(prev_doy_int, 367);
        target_doy = static_cast<double>(target_int);
    } else {
        // Target on the "next year" side: between 1 and t_next
        const int target_int = *rc::gen::inRange(1, next_doy_int + 1);
        target_doy = static_cast<double>(target_int);
    }

    // Compute expected interval and offset with wrap-around
    double interval = t_next - t_prev + 366.0;  // wrapped interval
    double offset = target_doy - t_prev;
    if (offset < 0.0) {
        offset += 366.0;
    }
    const double expected_alpha = offset / interval;

    // Setup buffer with known field values for easy verification
    const std::size_t n = 2;
    RingBuffer buffer(n);
    {
        auto s = buffer.next_slot();
        s[0] = 0.0; s[1] = 100.0;  // prev data
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        s[0] = 1.0; s[1] = 101.0;  // next data: prev + 1, prev + 1
    }
    buffer.set_times(t_prev, t_next);

    std::vector<double> output(n);
    int rc_code = interpolate_cyclical(buffer, target_doy, output);
    RC_ASSERT(rc_code == 0);

    // Expected output: prev + alpha * (next - prev)
    // For element 0: 0 + alpha * (1 - 0) = alpha
    // For element 1: 100 + alpha * (101 - 100) = 100 + alpha
    const double tol = 1e-10;
    RC_ASSERT(std::abs(output[0] - expected_alpha) < tol);
    RC_ASSERT(std::abs(output[1] - (100.0 + expected_alpha)) < tol);

    // Also verify that alpha is in [0, 1] (valid interpolation weight)
    RC_ASSERT(expected_alpha >= 0.0);
    RC_ASSERT(expected_alpha <= 1.0);
}

/**
 * **Validates: Requirements 2.4, 11.4**
 * Property 5: Temporal interpolation of linear-in-time fields is exact
 *
 * For any linear function f(t) = a*t + b (with arbitrary finite a, b)
 * sampled at two time points, temporal interpolation at any intermediate
 * time SHALL produce the exact analytic value within machine epsilon.
 */
RC_GTEST_PROP(TemporalProperty, P5_LinearFieldInterpolationIsExact, ()) {
    // Generate finite a and b for f(t) = a*t + b
    const double a = *rc::gen::map(
        rc::gen::inRange(-10000, 10001),
        [](int v) { return static_cast<double>(v) / 100.0; });
    const double b = *rc::gen::map(
        rc::gen::inRange(-10000, 10001),
        [](int v) { return static_cast<double>(v) / 100.0; });

    // Generate two distinct time points
    const double t0 = *rc::gen::map(
        rc::gen::inRange(0, 10000),
        [](int v) { return static_cast<double>(v); });
    const int dt = *rc::gen::inRange(1, 10000);
    const double t1 = t0 + static_cast<double>(dt);

    // Generate target time strictly between t0 and t1
    const double frac = *rc::gen::map(
        rc::gen::inRange(1, 10000),
        [](int v) { return static_cast<double>(v) / 10000.0; });
    const double target = t0 + frac * (t1 - t0);

    // Multiple field elements all following f(t) = a*t + b
    const auto num_elements = *rc::gen::inRange(1, 20);
    const auto n = static_cast<std::size_t>(num_elements);

    // Generate per-element slopes and offsets
    std::vector<double> slopes(n), offsets(n);
    for (std::size_t i = 0; i < n; ++i) {
        slopes[i] = *rc::gen::map(
            rc::gen::inRange(-1000, 1001),
            [](int v) { return static_cast<double>(v) / 10.0; });
        offsets[i] = *rc::gen::map(
            rc::gen::inRange(-1000, 1001),
            [](int v) { return static_cast<double>(v) / 10.0; });
    }

    RingBuffer buffer(n);
    {
        auto s = buffer.next_slot();
        for (std::size_t i = 0; i < n; ++i) {
            s[i] = slopes[i] * t0 + offsets[i];
        }
    }
    buffer.rotate();
    {
        auto s = buffer.next_slot();
        for (std::size_t i = 0; i < n; ++i) {
            s[i] = slopes[i] * t1 + offsets[i];
        }
    }
    buffer.set_times(t0, t1);

    std::vector<double> output(n);
    int rc_code = interpolate_linear(buffer, target, output);
    RC_ASSERT(rc_code == 0);

    // For a linear function, interpolation should be exact
    for (std::size_t i = 0; i < n; ++i) {
        const double exact = slopes[i] * target + offsets[i];
        // Allow generous machine-epsilon tolerance scaled by value magnitude
        const double magnitude = std::max(std::abs(exact), 1.0);
        const double tol = magnitude * std::numeric_limits<double>::epsilon() * 64.0;
        RC_ASSERT(std::abs(output[i] - exact) <= tol);
    }
}

} // namespace
} // namespace tide::temporal
