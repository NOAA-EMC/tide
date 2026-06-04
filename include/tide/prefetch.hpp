/**
 * @file prefetch.hpp
 * @brief TIDE Async I/O Prefetch Coordinator.
 *
 * Provides the PrefetchManager class which coordinates asynchronous
 * read-ahead of upcoming time levels. When enabled, the manager issues
 * async read requests through an AmioReader and tracks their completion.
 * When disabled (the default), all operations are no-ops for backward
 * compatibility.
 *
 * The manager implements a fallback strategy: if an async prefetch fails,
 * it falls back to a synchronous read and logs a diagnostic warning.
 *
 * @section error_codes Prefetch Error Codes
 * - 0: Success
 * - 950 (ErrorCode::PrefetchFailed): Prefetch request failed (non-fatal)
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#ifndef TIDE_PREFETCH_HPP
#define TIDE_PREFETCH_HPP

#include <cstddef>
#include <memory>
#include <span>
#include <string_view>

#include "tide/io.hpp"

namespace tide::prefetch {

/**
 * @brief Configuration for async prefetch behavior.
 *
 * Controls whether a stream pre-reads upcoming time levels
 * asynchronously via AMIO, and how many levels to read ahead.
 */
struct PrefetchConfig {
    /// @brief Whether async prefetch is active for this stream.
    bool enabled = false;

    /// @brief Number of time levels to read ahead (default: 1).
    int depth = 1;
};

/**
 * @brief Manages async prefetch for a single stream's ring buffer.
 *
 * The PrefetchManager coordinates asynchronous read-ahead of upcoming
 * time levels. It maintains internal state tracking whether a prefetch
 * is pending and handles completion or fallback to synchronous reads.
 *
 * When disabled (enabled = false), all operations are no-ops that return
 * success immediately, preserving backward-compatible behavior.
 *
 * The manager supports configurable prefetch depth, though the current
 * implementation tracks one outstanding request at a time (depth controls
 * how far ahead the caller may issue requests).
 *
 * @note Thread safety: each PrefetchManager instance is independent.
 *       Concurrent access to the same instance is undefined behavior.
 *
 * @par Example Usage
 * @code
 * tide::prefetch::PrefetchConfig config{.enabled = true, .depth = 1};
 * tide::prefetch::PrefetchManager mgr(config);
 *
 * if (mgr.is_enabled()) {
 *     int rc = mgr.issue_prefetch(reader, "temperature", 5, buffer);
 *     // ... do computation ...
 *     rc = mgr.wait_prefetch();
 *     if (rc != 0) {
 *         // Prefetch failed, data was read synchronously as fallback
 *     }
 * }
 * @endcode
 */
class PrefetchManager {
public:
    /**
     * @brief Construct a PrefetchManager with the given configuration.
     *
     * If config.enabled is false, the manager operates in disabled mode
     * where all operations are no-ops returning success.
     *
     * @param config Prefetch configuration (enabled flag and depth).
     */
    explicit PrefetchManager(const PrefetchConfig& config);

    /**
     * @brief Destructor — waits for any pending prefetch to complete.
     */
    ~PrefetchManager();

    /// @brief Move constructor.
    PrefetchManager(PrefetchManager&&) noexcept;

    /// @brief Move assignment operator.
    PrefetchManager& operator=(PrefetchManager&&) noexcept;

    // Non-copyable
    PrefetchManager(const PrefetchManager&) = delete;
    PrefetchManager& operator=(const PrefetchManager&) = delete;

    /**
     * @brief Issue an async read request for the next time level.
     *
     * When enabled, launches an asynchronous read of the specified field
     * and time index into the provided buffer. The read runs concurrently
     * and can be collected via wait_prefetch().
     *
     * When disabled, this is a no-op that returns 0.
     *
     * If a prefetch is already pending, this call returns an error without
     * issuing a new request (caller must wait_prefetch() first).
     *
     * @param reader The AMIO reader to issue the request through.
     * @param field_name Field variable name to prefetch.
     * @param time_index Time level index to prefetch.
     * @param buffer Destination buffer for the read (must remain valid
     *               until wait_prefetch() completes).
     * @return 0 on success (request issued or disabled no-op),
     *         non-zero (ErrorCode::PrefetchFailed) on failure.
     */
    auto issue_prefetch(io::AmioReader& reader,
                        std::string_view field_name,
                        std::size_t time_index,
                        std::span<double> buffer) -> int;

    /**
     * @brief Wait for a previously issued prefetch to complete.
     *
     * Blocks until the outstanding async read finishes. If the async
     * read failed, falls back to a synchronous read and logs a warning.
     *
     * When disabled, this is a no-op that returns 0.
     * When no prefetch is pending, returns 0 immediately.
     *
     * @return 0 if data is available (either from prefetch or fallback),
     *         non-zero (ErrorCode::PrefetchFailed) if both async and
     *         synchronous fallback failed.
     */
    auto wait_prefetch() -> int;

    /**
     * @brief Check if a prefetch is currently pending.
     *
     * @return true if an async read has been issued and not yet waited on.
     *         Always false when disabled.
     */
    [[nodiscard]] bool has_pending() const noexcept;

    /**
     * @brief Check if prefetch is enabled.
     *
     * @return true if the manager was configured with enabled = true.
     */
    [[nodiscard]] bool is_enabled() const noexcept;

    /**
     * @brief Get the configured prefetch depth.
     *
     * @return Number of time levels to read ahead.
     */
    [[nodiscard]] int depth() const noexcept;

private:
    /// @brief Forward-declared implementation (PImpl).
    struct Impl;

    /// @brief Pointer to the implementation.
    std::unique_ptr<Impl> impl_;
};

} // namespace tide::prefetch

#endif // TIDE_PREFETCH_HPP
