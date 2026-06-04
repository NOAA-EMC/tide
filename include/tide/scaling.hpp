/**
 * @file scaling.hpp
 * @brief TIDE Scaling Engine — in-place linear transform Y = M*X + B.
 *
 * Provides apply_linear_transform() for in-place field scaling and
 * validate_params() for checking parameter validity. The transform
 * operates on caller-provided std::span<double> with no heap allocation.
 *
 * IEEE 754 semantics are preserved: NaN and Inf elements in the input
 * field are transformed normally (NaN stays NaN, Inf propagates per
 * arithmetic rules). Only the parameters M and B are validated for
 * finiteness; field elements are never checked.
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 500 (ErrorCode::InvalidScalingParam): M or B is NaN or Inf
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#ifndef TIDE_SCALING_HPP
#define TIDE_SCALING_HPP

#include <cmath>
#include <span>

namespace tide::scaling {

/**
 * @brief Validate that scaling parameters M and B are finite.
 *
 * Returns true if both M and B are finite (not NaN and not Inf).
 * This is a constexpr function suitable for compile-time evaluation
 * when arguments are known at compile time.
 *
 * @param M Multiplier parameter.
 * @param B Offset parameter.
 * @return true if both parameters are finite and non-NaN; false otherwise.
 */
[[nodiscard]] constexpr bool validate_params(double M, double B) noexcept {
    // std::isfinite is constexpr since C++23
    return std::isfinite(M) && std::isfinite(B);
}

/**
 * @brief Apply the linear transform Y = M*X + B in-place.
 *
 * Transforms every element of @p field according to:
 *   field[i] = M * field[i] + B
 *
 * The operation is performed in-place with no heap allocation.
 * IEEE 754 semantics are preserved for field elements: if an element
 * is NaN or Inf, the arithmetic result follows standard floating-point
 * rules (e.g., NaN * M + B = NaN, Inf * M + B depends on M's sign).
 *
 * @param field Mutable span of doubles to transform in-place.
 * @param M     Multiplier (must be finite; validated before application).
 * @param B     Offset (must be finite; validated before application).
 * @return 0 on success, 500 (ErrorCode::InvalidScalingParam) if M or B
 *         is NaN or Inf. When returning non-zero, @p field is unmodified.
 *
 * @note No heap allocation is performed by this function.
 * @note An empty span is a valid input; the function returns 0 immediately.
 */
[[nodiscard]] auto apply_linear_transform(std::span<double> field,
                                          double M, double B) noexcept -> int;

} // namespace tide::scaling

#endif // TIDE_SCALING_HPP
