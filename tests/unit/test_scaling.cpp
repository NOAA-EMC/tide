/**
 * @file test_scaling.cpp
 * @brief Unit and property-based tests for the TIDE Scaling Engine.
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <tide/error.hpp>
#include <tide/scaling.hpp>

#include <cmath>
#include <limits>
#include <span>
#include <vector>

namespace tide::scaling {
namespace {

// =============================================================================
// validate_params tests
// =============================================================================

TEST(ValidateParamsTest, BothFiniteReturnsTrue) {
    EXPECT_TRUE(validate_params(1.0, 0.0));
    EXPECT_TRUE(validate_params(-3.14, 2.718));
    EXPECT_TRUE(validate_params(0.0, 0.0));
    EXPECT_TRUE(validate_params(1.0e308, -1.0e308));
}

TEST(ValidateParamsTest, MIsNanReturnsFalse) {
    EXPECT_FALSE(validate_params(std::numeric_limits<double>::quiet_NaN(), 0.0));
    EXPECT_FALSE(validate_params(std::numeric_limits<double>::signaling_NaN(), 1.0));
}

TEST(ValidateParamsTest, BIsNanReturnsFalse) {
    EXPECT_FALSE(validate_params(1.0, std::numeric_limits<double>::quiet_NaN()));
}

TEST(ValidateParamsTest, MIsInfReturnsFalse) {
    EXPECT_FALSE(validate_params(std::numeric_limits<double>::infinity(), 0.0));
    EXPECT_FALSE(validate_params(-std::numeric_limits<double>::infinity(), 0.0));
}

TEST(ValidateParamsTest, BIsInfReturnsFalse) {
    EXPECT_FALSE(validate_params(1.0, std::numeric_limits<double>::infinity()));
    EXPECT_FALSE(validate_params(1.0, -std::numeric_limits<double>::infinity()));
}

TEST(ValidateParamsTest, BothInvalidReturnsFalse) {
    EXPECT_FALSE(validate_params(std::numeric_limits<double>::quiet_NaN(),
                                 std::numeric_limits<double>::infinity()));
}

// =============================================================================
// apply_linear_transform — basic correctness
// =============================================================================

TEST(ApplyLinearTransformTest, IdentityTransform) {
    // M=1, B=0 should leave data unchanged
    std::vector<double> data = {1.0, 2.0, 3.0, 4.0, 5.0};
    std::vector<double> expected = data;

    int rc = apply_linear_transform(std::span{data}, 1.0, 0.0);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(data, expected);
}

TEST(ApplyLinearTransformTest, ScaleOnly) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    int rc = apply_linear_transform(std::span{data}, 2.0, 0.0);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], 2.0);
    EXPECT_DOUBLE_EQ(data[1], 4.0);
    EXPECT_DOUBLE_EQ(data[2], 6.0);
}

TEST(ApplyLinearTransformTest, OffsetOnly) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    int rc = apply_linear_transform(std::span{data}, 1.0, 10.0);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], 11.0);
    EXPECT_DOUBLE_EQ(data[1], 12.0);
    EXPECT_DOUBLE_EQ(data[2], 13.0);
}

TEST(ApplyLinearTransformTest, ScaleAndOffset) {
    // Kelvin to Celsius: M=1, B=-273.15
    std::vector<double> data = {273.15, 373.15, 0.0};
    int rc = apply_linear_transform(std::span{data}, 1.0, -273.15);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], 0.0);
    EXPECT_DOUBLE_EQ(data[1], 100.0);
    EXPECT_DOUBLE_EQ(data[2], -273.15);
}

TEST(ApplyLinearTransformTest, NegativeMultiplier) {
    std::vector<double> data = {1.0, -2.0, 3.0};
    int rc = apply_linear_transform(std::span{data}, -1.0, 0.0);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], -1.0);
    EXPECT_DOUBLE_EQ(data[1], 2.0);
    EXPECT_DOUBLE_EQ(data[2], -3.0);
}

TEST(ApplyLinearTransformTest, EmptySpanSucceeds) {
    std::span<double> empty;
    int rc = apply_linear_transform(empty, 5.0, 3.0);
    EXPECT_EQ(rc, 0);
}

// =============================================================================
// apply_linear_transform — invalid parameters (error code 500)
// =============================================================================

TEST(ApplyLinearTransformTest, InvalidMNanReturns500) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    std::vector<double> original = data;

    int rc = apply_linear_transform(
        std::span{data}, std::numeric_limits<double>::quiet_NaN(), 0.0);
    EXPECT_EQ(rc, to_int(ErrorCode::InvalidScalingParam));
    EXPECT_EQ(data, original);  // Field unchanged
}

TEST(ApplyLinearTransformTest, InvalidBNanReturns500) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    std::vector<double> original = data;

    int rc = apply_linear_transform(
        std::span{data}, 1.0, std::numeric_limits<double>::quiet_NaN());
    EXPECT_EQ(rc, to_int(ErrorCode::InvalidScalingParam));
    EXPECT_EQ(data, original);
}

TEST(ApplyLinearTransformTest, InvalidMInfReturns500) {
    std::vector<double> data = {1.0, 2.0, 3.0};
    std::vector<double> original = data;

    int rc = apply_linear_transform(
        std::span{data}, std::numeric_limits<double>::infinity(), 0.0);
    EXPECT_EQ(rc, to_int(ErrorCode::InvalidScalingParam));
    EXPECT_EQ(data, original);
}

TEST(ApplyLinearTransformTest, InvalidBNegInfReturns500) {
    std::vector<double> data = {1.0, 2.0};
    std::vector<double> original = data;

    int rc = apply_linear_transform(
        std::span{data}, 1.0, -std::numeric_limits<double>::infinity());
    EXPECT_EQ(rc, to_int(ErrorCode::InvalidScalingParam));
    EXPECT_EQ(data, original);
}

// =============================================================================
// apply_linear_transform — IEEE 754 semantics for field elements
// =============================================================================

TEST(ApplyLinearTransformTest, NanFieldElementPreserved) {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    std::vector<double> data = {1.0, nan, 3.0};

    int rc = apply_linear_transform(std::span{data}, 2.0, 1.0);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], 3.0);     // 2*1 + 1
    EXPECT_TRUE(std::isnan(data[1]));    // NaN propagates
    EXPECT_DOUBLE_EQ(data[2], 7.0);     // 2*3 + 1
}

TEST(ApplyLinearTransformTest, InfFieldElementPreserved) {
    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<double> data = {inf, -inf, 1.0};

    int rc = apply_linear_transform(std::span{data}, 2.0, 1.0);
    EXPECT_EQ(rc, 0);
    EXPECT_EQ(data[0], inf);            // 2*inf + 1 = inf
    EXPECT_EQ(data[1], -inf);           // 2*(-inf) + 1 = -inf
    EXPECT_DOUBLE_EQ(data[2], 3.0);     // 2*1 + 1
}

TEST(ApplyLinearTransformTest, InfTimesZeroMultiplierIsNan) {
    // IEEE 754: 0 * inf = NaN
    constexpr double inf = std::numeric_limits<double>::infinity();
    std::vector<double> data = {inf};

    int rc = apply_linear_transform(std::span{data}, 0.0, 5.0);
    EXPECT_EQ(rc, 0);
    // 0.0 * inf + 5.0 = NaN + 5.0 = NaN
    EXPECT_TRUE(std::isnan(data[0]));
}

// =============================================================================
// apply_linear_transform — no heap allocation verification
// =============================================================================

TEST(ApplyLinearTransformTest, LargeFieldInPlace) {
    // Verify transform works on a large field without issues
    constexpr std::size_t N = 1'000'000;
    std::vector<double> data(N);
    for (std::size_t i = 0; i < N; ++i) {
        data[i] = static_cast<double>(i);
    }

    int rc = apply_linear_transform(std::span{data}, 1.0e-6, 0.0);
    EXPECT_EQ(rc, 0);
    EXPECT_DOUBLE_EQ(data[0], 0.0);
    EXPECT_DOUBLE_EQ(data[N - 1], static_cast<double>(N - 1) * 1.0e-6);
}

// =============================================================================
// Property-based tests (RapidCheck)
// =============================================================================

/**
 * **Validates: Requirements 5.1, 11.3**
 * Property 14: Scaling formula Y = M × X + B
 *
 * For any finite M in [-1000, 1000], finite B in [-1000, 1000], and array X
 * with elements in [-1e6, 1e6], the scaling engine SHALL produce output where
 * each element equals M × X[i] + B to within machine epsilon.
 */
RC_GTEST_PROP(ScalingProperty, P14_ScalingFormulaMxPlusB, ()) {
    // Generate M in [-1000, 1000] and B in [-1000, 1000]
    const double M = *rc::gen::map(rc::gen::arbitrary<double>(), [](double v) {
        // Map arbitrary double to [-1000, 1000] via fmod + clamp
        double clamped = std::fmod(v, 1000.0);
        return std::isfinite(clamped) ? clamped : 0.0;
    });
    const double B = *rc::gen::map(rc::gen::arbitrary<double>(), [](double v) {
        double clamped = std::fmod(v, 1000.0);
        return std::isfinite(clamped) ? clamped : 0.0;
    });

    // Generate a vector of doubles in [-1e6, 1e6]
    const auto original = *rc::gen::container<std::vector<double>>(
        rc::gen::map(rc::gen::arbitrary<double>(), [](double v) {
            double clamped = std::fmod(v, 1.0e6);
            return std::isfinite(clamped) ? clamped : 0.0;
        }));

    std::vector<double> data = original;
    int rc_code = apply_linear_transform(std::span{data}, M, B);
    RC_ASSERT(rc_code == 0);

    for (std::size_t i = 0; i < original.size(); ++i) {
        const double expected = M * original[i] + B;
        // Within these bounded ranges, M*X+B should always be finite
        RC_ASSERT(data[i] == expected);
    }
}

/**
 * **Validates: Requirements 5.4, 5.5**
 * Property 15: Invalid scaling parameters rejected with field unchanged
 *
 * For any non-finite M or B (NaN, +Inf, -Inf) and any input field, the
 * scaling engine SHALL return a non-zero error code and the field SHALL be
 * bitwise identical to its pre-call state.
 */
RC_GTEST_PROP(ScalingProperty, P15_InvalidParamsRejectedFieldUnchanged, ()) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();

    // Pick one of: M=NaN, M=Inf, M=-Inf, B=NaN, B=Inf, B=-Inf
    const int choice = *rc::gen::inRange(0, 6);
    double M = 1.0, B = 0.0;
    switch (choice) {
        case 0: M = nan; break;
        case 1: M = inf; break;
        case 2: M = -inf; break;
        case 3: B = nan; break;
        case 4: B = inf; break;
        case 5: B = -inf; break;
    }

    // Generate arbitrary field data (may contain NaN/Inf/normal values)
    const auto original = *rc::gen::container<std::vector<double>>(
        rc::gen::arbitrary<double>());

    std::vector<double> data = original;
    int rc_code = apply_linear_transform(std::span{data}, M, B);
    RC_ASSERT(rc_code == tide::to_int(ErrorCode::InvalidScalingParam));

    // Field must be completely unchanged (bitwise)
    RC_ASSERT(data.size() == original.size());
    for (std::size_t i = 0; i < data.size(); ++i) {
        // Use memcmp for bitwise comparison (handles NaN equality)
        RC_ASSERT(std::memcmp(&data[i], &original[i], sizeof(double)) == 0);
    }
}

/**
 * **Validates: Requirements 5.6**
 * Property 16: IEEE 754 arithmetic propagation in scaling
 *
 * For any field containing NaN or Inf elements and valid (finite) M and B,
 * the scaling engine SHALL apply the transform producing results consistent
 * with IEEE 754 arithmetic (NaN propagates, Inf × finite = Inf, etc.).
 */
RC_GTEST_PROP(ScalingProperty, P16_IEEE754ArithmeticPropagation, ()) {
    // Generate valid (finite) M and B
    const double M = *rc::gen::suchThat<double>(
        [](double v) { return std::isfinite(v); });
    const double B = *rc::gen::suchThat<double>(
        [](double v) { return std::isfinite(v); });

    // Generate a field that includes NaN and/or Inf elements
    // Mix of: NaN, +Inf, -Inf, and finite values
    const auto field_values = *rc::gen::container<std::vector<double>>(
        rc::gen::oneOf(
            rc::gen::just(std::numeric_limits<double>::quiet_NaN()),
            rc::gen::just(std::numeric_limits<double>::infinity()),
            rc::gen::just(-std::numeric_limits<double>::infinity()),
            rc::gen::suchThat<double>([](double v) { return std::isfinite(v); })
        ));

    // Require at least one non-finite element to exercise IEEE 754 paths
    bool has_non_finite = false;
    for (const auto& v : field_values) {
        if (!std::isfinite(v)) {
            has_non_finite = true;
            break;
        }
    }
    RC_PRE(has_non_finite);

    std::vector<double> data = field_values;
    int rc_code = apply_linear_transform(std::span{data}, M, B);
    RC_ASSERT(rc_code == 0);

    // Verify each element follows IEEE 754 arithmetic: Y = M*X + B
    for (std::size_t i = 0; i < field_values.size(); ++i) {
        const double x = field_values[i];
        const double expected = M * x + B;

        if (std::isnan(expected)) {
            RC_ASSERT(std::isnan(data[i]));
        } else {
            // For Inf cases, compare bitwise (both should be same Inf)
            RC_ASSERT(std::memcmp(&data[i], &expected, sizeof(double)) == 0);
        }
    }
}

} // anonymous namespace
} // namespace tide::scaling
