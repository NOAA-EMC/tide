/**
 * @file perf.cpp
 * @brief Implementation of TIDE performance instrumentation (StageTimers).
 *
 * Uses std::chrono::steady_clock for monotonic, portable wall-clock timing.
 * When disabled, all operations are no-ops with minimal branch overhead.
 */

#include "tide/perf.hpp"

namespace tide::perf {

StageTimers::StageTimers(bool enabled)
    : enabled_(enabled), cumulative_{}, start_points_{} {}

void StageTimers::start(Stage stage) noexcept {
    if (!enabled_) return;
    const auto idx = static_cast<std::size_t>(stage);
    start_points_[idx] = std::chrono::steady_clock::now();
}

void StageTimers::stop(Stage stage) noexcept {
    if (!enabled_) return;
    const auto now = std::chrono::steady_clock::now();
    const auto idx = static_cast<std::size_t>(stage);
    // Guard: only accumulate if start() was called (start_point != epoch)
    if (start_points_[idx].time_since_epoch().count() == 0) return;
    const std::chrono::duration<double> elapsed = now - start_points_[idx];
    cumulative_[idx] += elapsed.count();
    // Reset start point to prevent double-stop accumulation
    start_points_[idx] = std::chrono::steady_clock::time_point{};
}

double StageTimers::elapsed(Stage stage) const noexcept {
    if (!enabled_) return 0.0;
    return cumulative_[static_cast<std::size_t>(stage)];
}

void StageTimers::get_all(double out[5]) const noexcept {
    for (std::size_t i = 0; i < 5; ++i) {
        out[i] = enabled_ ? cumulative_[i] : 0.0;
    }
}

void StageTimers::reset() noexcept {
    cumulative_.fill(0.0);
    start_points_.fill(std::chrono::steady_clock::time_point{});
}

bool StageTimers::is_enabled() const noexcept {
    return enabled_;
}

} // namespace tide::perf
