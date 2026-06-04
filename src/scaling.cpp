/**
 * @file scaling.cpp
 * @brief Implementation of the TIDE Scaling Engine.
 *
 * Applies the linear transform Y = M*X + B in-place on a caller-provided
 * std::span<double>. No heap allocation is performed.
 *
 * IEEE 754 semantics are preserved for field elements — NaN and Inf values
 * in the input are transformed per standard floating-point arithmetic rules.
 * Only the parameters M and B are validated for finiteness.
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include <tide/scaling.hpp>
#include <tide/error.hpp>

namespace tide::scaling {

auto apply_linear_transform(std::span<double> field,
                            double M, double B) noexcept -> int {
    // Validate parameters: M and B must both be finite (not NaN, not Inf)
    if (!validate_params(M, B)) {
        return to_int(ErrorCode::InvalidScalingParam);
    }

    // Apply Y = M*X + B in-place — no heap allocation
    for (auto& x : field) {
        x = M * x + B;
    }

    return 0;
}

} // namespace tide::scaling
