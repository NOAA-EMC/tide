/**
 * @file temporal.hpp
 * @brief Temporal interpolation engine for the TIDE library.
 *
 * Provides the RingBuffer class for managing two time-level slots and
 * interpolation functions for computing time-interpolated forcing fields.
 *
 * The ring buffer holds exactly two time levels (T_prev and T_next) and
 * supports rotation when the simulation advances past T_next. Interpolation
 * functions compute fields at arbitrary target times between the bracketing
 * time levels.
 */

#ifndef TIDE_TEMPORAL_HPP
#define TIDE_TEMPORAL_HPP

#include <array>
#include <cstddef>
#include <span>
#include <vector>

#include "tide/error.hpp"

namespace tide::temporal {

/**
 * @brief Two-slot ring buffer for time-bracketing forcing fields.
 *
 * Maintains exactly two time levels of field data: the previous (T_prev)
 * and next (T_next) levels. The I/O layer fills the "next" slot via
 * next_slot(), and when the simulation advances past T_next, rotate()
 * promotes the current next to prev and makes the other slot available
 * for new data.
 *
 * This design provides minimal memory footprint (exactly 2× field size)
 * while supporting linear interpolation between bounding time levels.
 *
 * @note Thread safety: each stream owns its own RingBuffer instance;
 *       concurrent access to different buffers is safe without synchronization.
 *       Concurrent access to the same buffer is undefined behavior.
 */
class RingBuffer {
public:
    /**
     * @brief Construct a ring buffer with the given field dimensions.
     *
     * Both slots are allocated and zero-initialized.
     *
     * @param num_elements Total number of elements per time level
     *                     (e.g., ncols × nlevels for a 2D field).
     */
    explicit RingBuffer(std::size_t num_elements);

    /**
     * @brief Get a writable span for the "next" slot.
     *
     * The I/O layer uses this span to fill the next time level's
     * field data directly into the buffer without an intermediate copy.
     *
     * @return A writable span over the next slot's storage.
     */
    [[nodiscard]] auto next_slot() -> std::span<double>;

    /**
     * @brief Rotate the buffer: current next becomes prev.
     *
     * After rotation, the slot that was "next" is now "prev" (preserving
     * its data), and the other slot becomes the new "next" ready to
     * receive fresh data from I/O.
     *
     * This operation is O(1) — it swaps an index rather than copying data.
     */
    void rotate();

    /**
     * @brief Access the previous time level data (read-only).
     *
     * @return A read-only span over the prev slot's field data.
     */
    [[nodiscard]] auto prev_data() const -> std::span<const double>;

    /**
     * @brief Access the next time level data (read-only).
     *
     * @return A read-only span over the next slot's field data.
     */
    [[nodiscard]] auto next_data() const -> std::span<const double>;

    /**
     * @brief Get the time value for the previous time level.
     *
     * @return Time value of T_prev (seconds since epoch or DOY,
     *         depending on temporal mode).
     */
    [[nodiscard]] double t_prev() const noexcept;

    /**
     * @brief Get the time value for the next time level.
     *
     * @return Time value of T_next (seconds since epoch or DOY,
     *         depending on temporal mode).
     */
    [[nodiscard]] double t_next() const noexcept;

    /**
     * @brief Set the time values for the current bracket.
     *
     * @param t_prev Time value for the previous slot.
     * @param t_next Time value for the next slot.
     */
    void set_times(double t_prev, double t_next) noexcept;

    /**
     * @brief Get the number of elements per time-level slot.
     *
     * @return The num_elements value provided at construction.
     */
    [[nodiscard]] std::size_t num_elements() const noexcept;

private:
    /// @brief Two data slots holding field values for T_prev and T_next.
    std::array<std::vector<double>, 2> slots_;

    /// @brief Index into slots_ identifying the current "next" slot.
    ///
    /// The "prev" slot is always (1 - current_next_).
    std::size_t current_next_ = 0;

    /// @brief Time value associated with the previous time level.
    double t_prev_ = 0.0;

    /// @brief Time value associated with the next time level.
    double t_next_ = 0.0;
};

// =============================================================================
// Free functions: Temporal Interpolation
// =============================================================================

/**
 * @brief Compute linear temporal interpolation between two time levels.
 *
 * Applies the formula:
 *   F(t) = F(T_prev) + (t - T_prev) / (T_next - T_prev) × (F(T_next) - F(T_prev))
 *
 * If T_prev == T_next (identical timestamps), the previous time level data
 * is copied to output without interpolation.
 *
 * @param buffer Ring buffer holding bracketing time levels and field data.
 * @param target_time Time to interpolate to (must be in [T_prev, T_next]).
 * @param output Pre-allocated output buffer (must have buffer.num_elements() capacity).
 * @return 0 on success, ErrorCode::TimeOutOfRange if target_time is outside bracket.
 */
[[nodiscard]] auto interpolate_linear(const RingBuffer& buffer,
                                      double target_time,
                                      std::span<double> output) -> int;

/**
 * @brief Compute DOY-cyclical temporal interpolation.
 *
 * Selects T_prev and T_next based on day-of-year bracketing. When the
 * target DOY exceeds the last file DOY, wraps around to the first DOY
 * (December 31 wraps to January 1), treating the year boundary as contiguous.
 *
 * If T_prev == T_next (identical timestamps), the previous time level data
 * is copied to output without interpolation.
 *
 * @param buffer Ring buffer holding DOY-bracketing field data.
 *              t_prev() and t_next() are interpreted as day-of-year values.
 * @param target_doy Day-of-year to interpolate to (range [1, 366]).
 * @param output Pre-allocated output buffer (must have buffer.num_elements() capacity).
 * @return 0 on success, ErrorCode::TimeOutOfRange if target_doy is invalid.
 */
[[nodiscard]] auto interpolate_cyclical(const RingBuffer& buffer,
                                        double target_doy,
                                        std::span<double> output) -> int;

/**
 * @brief Compute seasonal-preservation temporal interpolation.
 *
 * Snaps to the closest available year in the forcing file while strictly
 * matching the simulation's day-of-year. This preserves seasonal cycles
 * when the simulation year exceeds the data's temporal range.
 *
 * If T_prev == T_next (identical timestamps), the previous time level data
 * is copied to output without interpolation.
 *
 * @param buffer Ring buffer holding time-bracketing field data.
 * @param target_time Simulation time (seconds since epoch).
 * @param file_time_values All available time values in the forcing file (sorted).
 * @param output Pre-allocated output buffer (must have buffer.num_elements() capacity).
 * @return 0 on success, ErrorCode::TimeOutOfRange if no suitable match found.
 */
[[nodiscard]] auto interpolate_seasonal(const RingBuffer& buffer,
                                        double target_time,
                                        std::span<const double> file_time_values,
                                        std::span<double> output) -> int;

} // namespace tide::temporal

#endif // TIDE_TEMPORAL_HPP
