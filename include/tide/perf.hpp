/**
 * @file perf.hpp
 * @brief TIDE performance instrumentation — per-stage timing.
 *
 * Provides a lightweight timer class that records cumulative wall-clock
 * elapsed time for each pipeline stage (I/O, Temporal, Spatial, Vertical,
 * Scaling). Timers can be globally disabled for zero-overhead production
 * runs.
 *
 * @see Requirement 7: Performance Instrumentation
 */

#ifndef TIDE_PERF_HPP
#define TIDE_PERF_HPP

#include <array>
#include <chrono>

namespace tide::perf {

/**
 * @brief Pipeline stage identifiers for performance instrumentation.
 *
 * Each enumerator corresponds to one stage of the TIDE forcing pipeline.
 * NumStages is a sentinel value equal to the number of real stages.
 */
enum class Stage : int {
    IO = 0,        ///< File I/O (AMIO read or prefetch completion)
    Temporal = 1,  ///< Temporal interpolation
    Spatial = 2,   ///< Spatial regridding (Atlas or SCRIP weights)
    Vertical = 3,  ///< Vertical interpolation (TSPACK)
    Scaling = 4,   ///< Linear scaling (M*x + B)
    NumStages = 5  ///< Sentinel — number of pipeline stages
};

/**
 * @brief Per-stage cumulative wall-clock timers.
 *
 * Records elapsed time for each pipeline stage using
 * std::chrono::steady_clock for monotonic, portable measurement.
 *
 * When disabled (constructed with enabled=false), all methods are
 * effectively no-ops: start()/stop() do nothing and elapsed() returns 0.0.
 * This ensures zero overhead in production runs where timing is not needed.
 *
 * Usage:
 * @code
 *   tide::perf::StageTimers timers(true);
 *   timers.start(tide::perf::Stage::IO);
 *   // ... do I/O work ...
 *   timers.stop(tide::perf::Stage::IO);
 *   double io_seconds = timers.elapsed(tide::perf::Stage::IO);
 * @endcode
 *
 * Multiple start/stop pairs accumulate elapsed time for the same stage.
 */
class StageTimers {
public:
    /**
     * @brief Construct timers in enabled or disabled mode.
     * @param enabled If false, all measurement is skipped (no-op mode).
     */
    explicit StageTimers(bool enabled = true);

    /**
     * @brief Start timing a pipeline stage.
     *
     * Records the current time point. If timers are disabled, this is a no-op.
     *
     * @param stage The pipeline stage to start timing.
     */
    void start(Stage stage) noexcept;

    /**
     * @brief Stop timing a pipeline stage and accumulate elapsed time.
     *
     * Computes the duration since the corresponding start() call and adds
     * it to the cumulative total for this stage. If timers are disabled,
     * this is a no-op.
     *
     * @param stage The pipeline stage to stop timing.
     */
    void stop(Stage stage) noexcept;

    /**
     * @brief Get cumulative elapsed time for a stage.
     *
     * @param stage The pipeline stage to query.
     * @return Cumulative elapsed seconds, or 0.0 if disabled.
     */
    [[nodiscard]] double elapsed(Stage stage) const noexcept;

    /**
     * @brief Get cumulative elapsed times for all stages.
     *
     * Fills the output array with cumulative seconds for each stage,
     * indexed by Stage enum value (0..4).
     *
     * @param out Array of at least 5 doubles to receive the results.
     */
    void get_all(double out[5]) const noexcept;

    /**
     * @brief Reset all cumulative timers to zero.
     */
    void reset() noexcept;

    /**
     * @brief Check whether timing is enabled.
     * @return true if timers are active, false if in no-op mode.
     */
    [[nodiscard]] bool is_enabled() const noexcept;

private:
    /// @brief Whether measurement is active.
    bool enabled_;

    /// @brief Cumulative elapsed seconds per stage.
    std::array<double, 5> cumulative_{};

    /// @brief Start time points for in-progress measurements.
    std::array<std::chrono::steady_clock::time_point, 5> start_points_{};
};

} // namespace tide::perf

#endif // TIDE_PERF_HPP
