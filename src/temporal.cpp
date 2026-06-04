/**
 * @file temporal.cpp
 * @brief Implementation of the TIDE temporal interpolation engine.
 *
 * Implements the RingBuffer class for two-slot time-level management
 * and free functions for linear, cyclical, and seasonal-preservation
 * temporal interpolation.
 */

#include "tide/temporal.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace tide::temporal {

// =============================================================================
// RingBuffer implementation
// =============================================================================

RingBuffer::RingBuffer(std::size_t num_elements) {
    slots_[0].resize(num_elements, 0.0);
    slots_[1].resize(num_elements, 0.0);
}

auto RingBuffer::next_slot() -> std::span<double> {
    return std::span<double>(slots_[current_next_]);
}

void RingBuffer::rotate() {
    // The current "next" becomes "prev" by flipping the index.
    // The old "prev" slot becomes the new "next", ready to be overwritten.
    current_next_ = 1 - current_next_;
}

auto RingBuffer::prev_data() const -> std::span<const double> {
    // prev is the slot that is NOT current_next_
    return std::span<const double>(slots_[1 - current_next_]);
}

auto RingBuffer::next_data() const -> std::span<const double> {
    return std::span<const double>(slots_[current_next_]);
}

double RingBuffer::t_prev() const noexcept {
    return t_prev_;
}

double RingBuffer::t_next() const noexcept {
    return t_next_;
}

void RingBuffer::set_times(double t_prev, double t_next) noexcept {
    t_prev_ = t_prev;
    t_next_ = t_next;
}

std::size_t RingBuffer::num_elements() const noexcept {
    return slots_[0].size();
}

// =============================================================================
// Temporal Interpolation Functions
// =============================================================================

auto interpolate_linear(const RingBuffer& buffer,
                        double target_time,
                        std::span<double> output) -> int {
    const double t_prev = buffer.t_prev();
    const double t_next = buffer.t_next();
    const std::size_t n = buffer.num_elements();

    // Handle identical timestamps: return field without interpolation
    if (t_prev == t_next) {
        auto prev = buffer.prev_data();
        std::memcpy(output.data(), prev.data(), n * sizeof(double));
        return to_int(ErrorCode::Success);
    }

    // Validate target_time is within bracket [T_prev, T_next]
    if (target_time < t_prev || target_time > t_next) {
        return to_int(ErrorCode::TimeOutOfRange);
    }

    auto prev = buffer.prev_data();
    auto next = buffer.next_data();

    // F(t) = F(T_prev) + (t - T_prev)/(T_next - T_prev) × (F(T_next) - F(T_prev))
    const double alpha = (target_time - t_prev) / (t_next - t_prev);

    for (std::size_t i = 0; i < n; ++i) {
        output[i] = prev[i] + alpha * (next[i] - prev[i]);
    }

    return to_int(ErrorCode::Success);
}

auto interpolate_cyclical(const RingBuffer& buffer,
                          double target_doy,
                          std::span<double> output) -> int {
    const double t_prev = buffer.t_prev();
    const double t_next = buffer.t_next();
    const std::size_t n = buffer.num_elements();

    // Handle identical timestamps: return field without interpolation
    if (t_prev == t_next) {
        auto prev = buffer.prev_data();
        std::memcpy(output.data(), prev.data(), n * sizeof(double));
        return to_int(ErrorCode::Success);
    }

    // Validate DOY is in valid range [1, 366]
    if (target_doy < 1.0 || target_doy > 366.0) {
        return to_int(ErrorCode::TimeOutOfRange);
    }

    auto prev = buffer.prev_data();
    auto next = buffer.next_data();

    // Compute the fractional interpolation weight.
    // For cyclical mode, t_prev and t_next are DOY values.
    // If t_next < t_prev, we have a year-boundary wrap (e.g., DOY 350 → DOY 10).
    double interval = t_next - t_prev;
    double offset = target_doy - t_prev;

    if (interval < 0.0) {
        // Year-boundary wrap: interval spans across Dec 31 → Jan 1
        // Total interval = (days remaining in year from t_prev) + t_next
        interval += 366.0;
    }

    if (offset < 0.0) {
        // Target is past the year boundary relative to t_prev
        offset += 366.0;
    }

    // Compute interpolation weight
    const double alpha = (interval > 0.0) ? (offset / interval) : 0.0;

    for (std::size_t i = 0; i < n; ++i) {
        output[i] = prev[i] + alpha * (next[i] - prev[i]);
    }

    return to_int(ErrorCode::Success);
}

auto interpolate_seasonal(const RingBuffer& buffer,
                          double target_time,
                          std::span<const double> file_time_values,
                          std::span<double> output) -> int {
    const double t_prev = buffer.t_prev();
    const double t_next = buffer.t_next();
    const std::size_t n = buffer.num_elements();

    // Handle identical timestamps: return field without interpolation
    if (t_prev == t_next) {
        auto prev = buffer.prev_data();
        std::memcpy(output.data(), prev.data(), n * sizeof(double));
        return to_int(ErrorCode::Success);
    }

    // If no file time values available, we cannot do seasonal interpolation
    if (file_time_values.empty()) {
        return to_int(ErrorCode::TimeLevelsExhausted);
    }

    // Seasonal-preservation mode:
    // We snap to the closest available year while matching simulation DOY.
    // The ring buffer already holds the two time levels that bracket the
    // target DOY (from the closest available year). The orchestrator is
    // responsible for loading the correct time levels.
    //
    // Here we perform linear interpolation between the already-loaded
    // bracketing time levels, which represent the DOY-matching entries
    // from the closest available year.

    // Validate that target_time falls within the bracket [T_prev, T_next]
    if (target_time < t_prev || target_time > t_next) {
        return to_int(ErrorCode::TimeOutOfRange);
    }

    auto prev = buffer.prev_data();
    auto next = buffer.next_data();

    // Linear interpolation between the seasonal bracket
    const double alpha = (target_time - t_prev) / (t_next - t_prev);

    for (std::size_t i = 0; i < n; ++i) {
        output[i] = prev[i] + alpha * (next[i] - prev[i]);
    }

    return to_int(ErrorCode::Success);
}

} // namespace tide::temporal
