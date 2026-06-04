/**
 * @file manager.hpp
 * @brief TIDE Multi-Stream Manager — tide::StreamManager class.
 *
 * Provides the StreamManager class which manages lifecycle and collective
 * operations across multiple concurrent forcing streams. The manager
 * coordinates initialization (with memory budget checking), per-stream
 * error isolation during advance, performance timer tracking, prefetch
 * coordination, and bulk finalization.
 *
 * The class uses the PImpl pattern to hide internal state from the
 * public header.
 *
 * @section error_isolation Error Isolation
 * When one stream fails during advance_all(), it is marked as Failed
 * and skipped on subsequent calls. Other streams continue operating
 * independently, ensuring that a corrupted file in one stream does
 * not halt the entire forcing system.
 *
 * @section memory_budget Memory Budget
 * At initialization, the StreamManager computes total buffer requirements
 * using the MemoryBudget controller. If the configured memory_budget_mb
 * is exceeded, create() returns an error identifying which stream caused
 * the budget overflow.
 *
 * Validates Requirements: 4.1, 4.2, 4.3, 4.5, 4.6, 4.7, 8.1, 8.2, 8.3, 8.4, 8.5
 */

#ifndef TIDE_MANAGER_HPP
#define TIDE_MANAGER_HPP

#include <cstddef>
#include <expected>
#include <memory>
#include <vector>

#include <mpi.h>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/stream.hpp"
#include "tide/types.hpp"

namespace tide {

/**
 * @brief Per-stream status tracking for the StreamManager.
 *
 * Each stream maintains one of three states:
 * - Healthy: operating normally, will be advanced on next advance_all()
 * - Failed: encountered an error, skipped until explicitly retried
 * - Finalized: resources released, permanently inactive
 */
enum class StreamStatus {
    Healthy,     ///< Stream operating normally
    Failed,      ///< Stream encountered an error (advance returned non-zero)
    Finalized    ///< Stream has been finalized (resources released)
};

/**
 * @brief Manages multiple concurrent forcing streams with error isolation.
 *
 * StreamManager is a thin coordinator over multiple Stream instances.
 * Each stream maintains its own independent pipeline state (reader,
 * buffers, weights). The manager provides:
 *
 * - Bulk initialization from a TideConfig with memory budget checking
 * - advance_all() that continues healthy streams even when others fail
 * - Per-stream status tracking and direct stream access
 * - Per-stream StageTimers for performance instrumentation
 * - Per-stream PrefetchManagers for async I/O coordination
 * - Bulk finalization releasing all resources
 *
 * @par Example Usage
 * @code
 * auto config = tide::config::parse_yaml("forcing.yaml");
 * auto mgr_result = tide::StreamManager::create(config.value(), MPI_COMM_WORLD);
 * if (!mgr_result) {
 *     std::cerr << mgr_result.error().message << "\n";
 *     return;
 * }
 * auto mgr = std::move(mgr_result.value());
 *
 * auto status_codes = mgr.advance_all(3600.0);
 * for (std::size_t i = 0; i < mgr.stream_count(); ++i) {
 *     if (status_codes[i] != 0) {
 *         std::cerr << "Stream " << i << " failed: " << status_codes[i] << "\n";
 *     }
 * }
 *
 * mgr.finalize_all();
 * @endcode
 */
class StreamManager {
public:
    /**
     * @brief Initialize all streams from a parsed configuration.
     *
     * Creates one Stream per entry in config.streams, validates memory
     * budget requirements, and initializes per-stream timers and prefetch
     * managers.
     *
     * @param config Full TIDE configuration with multiple streams.
     * @param comm MPI communicator for parallel I/O.
     * @param memory_budget_mb Optional memory limit in megabytes.
     *                         If 0 (default), uses config.memory_budget_mb.
     *                         If config also specifies 0, no limit is enforced.
     * @return StreamManager on success, or Error on failure.
     *         Possible errors:
     *         - MemoryBudgetExceeded (900): total buffer requirements exceed budget
     *         - Any Stream::create() error: propagated from the failing stream
     *
     * @note If memory_budget_mb parameter is non-zero, it overrides the
     *       config.memory_budget_mb value.
     */
    [[nodiscard]] static auto create(const config::TideConfig& config,
                                     MPI_Comm comm,
                                     std::size_t memory_budget_mb = 0)
        -> std::expected<StreamManager, Error>;

    /**
     * @brief Advance all healthy streams to the target simulation time.
     *
     * Iterates all managed streams. For each stream:
     * - If status is Failed or Finalized, skips it (preserves previous error code)
     * - If status is Healthy, calls stream.advance(target_time)
     * - On success, keeps status Healthy
     * - On failure, marks status as Failed and records the error code
     *
     * Error isolation guarantees that a failure in stream N does not
     * affect the output or state of any other stream M (M ≠ N).
     *
     * @param target_time Simulation time in seconds since epoch.
     * @return Vector of per-stream status codes. Element i is 0 if stream i
     *         succeeded (or was already finalized), or a non-zero error code
     *         if it failed (either in this call or a previous call).
     */
    auto advance_all(double target_time) -> std::vector<int>;

    /**
     * @brief Get the number of managed streams.
     * @return Number of streams initialized from the configuration.
     */
    [[nodiscard]] std::size_t stream_count() const noexcept;

    /**
     * @brief Get the status of a specific stream by index.
     *
     * @param index Zero-based stream index.
     * @return StreamStatus (Healthy, Failed, or Finalized).
     * @throws std::out_of_range if index >= stream_count().
     */
    [[nodiscard]] StreamStatus stream_status(std::size_t index) const;

    /**
     * @brief Get the underlying Stream handle for direct access.
     *
     * Allows the caller to perform operations on individual streams
     * (e.g., get_field(), retry advance after transient failure).
     *
     * @param index Zero-based stream index.
     * @return Pointer to the Stream, or nullptr if index is out of range.
     */
    [[nodiscard]] Stream* stream_at(std::size_t index);

    /// @brief Const version of stream_at().
    [[nodiscard]] const Stream* stream_at(std::size_t index) const;

    /**
     * @brief Finalize all streams and release resources.
     *
     * Calls finalize() on each stream that has not already been finalized,
     * and marks all streams as Finalized. After this call, the manager
     * should not be used for advance_all().
     *
     * It is safe to call finalize_all() multiple times; subsequent calls
     * are no-ops.
     */
    void finalize_all();

    /**
     * @brief Get total memory usage across all streams (bytes).
     *
     * Returns the computed total buffer allocation using the standard
     * formula: sum over all streams of
     *   2 × src_cells × src_levels × 8 + tgt_cols × tgt_levels × 8 × 3
     *
     * This is the value computed at initialization and does not change
     * during the lifetime of the manager.
     *
     * @return Total bytes allocated for pipeline buffers.
     */
    [[nodiscard]] std::size_t total_memory_usage() const noexcept;

    /// @brief Destructor — calls finalize_all() if not already done.
    ~StreamManager();

    /// @brief Move constructor.
    StreamManager(StreamManager&&) noexcept;

    /// @brief Move assignment operator.
    StreamManager& operator=(StreamManager&&) noexcept;

    // Non-copyable
    StreamManager(const StreamManager&) = delete;
    StreamManager& operator=(const StreamManager&) = delete;

private:
    /// @brief Private constructor — use create() factory method.
    StreamManager();

    /// @brief Forward-declared implementation (PImpl).
    struct Impl;

    /// @brief Pointer to the implementation.
    std::unique_ptr<Impl> impl_;
};

} // namespace tide

#endif // TIDE_MANAGER_HPP
