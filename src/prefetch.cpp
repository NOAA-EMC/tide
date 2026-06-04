/**
 * @file prefetch.cpp
 * @brief Implementation of the TIDE Async I/O Prefetch Coordinator.
 *
 * Implements PrefetchManager using std::async/std::future for tracking
 * outstanding asynchronous read requests. The manager maintains a simple
 * state machine:
 *
 *   Idle → (issue_prefetch) → Pending → (wait_prefetch) → Idle
 *
 * On prefetch failure, the manager falls back to a synchronous read
 * using the stored request parameters (non-fatal, logs a warning).
 *
 * When disabled, all public methods are no-ops returning success.
 *
 * Validates Requirements: 5.1, 5.2, 5.3, 5.4, 5.5, 5.6
 */

#include "tide/prefetch.hpp"
#include "tide/error.hpp"

#include <cstdio>
#include <future>
#include <string>

namespace tide::prefetch {

/**
 * @brief Internal state for a pending prefetch request.
 *
 * Stores the parameters needed to retry the read synchronously
 * if the async operation fails (fallback strategy).
 */
struct PendingRequest {
    /// @brief The reader used for this request.
    io::AmioReader* reader = nullptr;

    /// @brief Field name for the read.
    std::string field_name;

    /// @brief Time index for the read.
    std::size_t time_index = 0;

    /// @brief Destination buffer for the read.
    std::span<double> buffer;

    /// @brief Future tracking the async read result.
    std::future<int> result;
};

/**
 * @brief PImpl implementation of PrefetchManager.
 */
struct PrefetchManager::Impl {
    /// @brief Whether prefetch is enabled.
    bool enabled = false;

    /// @brief Configured prefetch depth.
    int depth = 1;

    /// @brief Currently pending request (nullopt when idle).
    std::unique_ptr<PendingRequest> pending;

    explicit Impl(const PrefetchConfig& config)
        : enabled(config.enabled)
        , depth(config.depth)
    {
    }
};

// ─── Construction / Destruction ──────────────────────────────────────────────

PrefetchManager::PrefetchManager(const PrefetchConfig& config)
    : impl_(std::make_unique<Impl>(config))
{
}

PrefetchManager::~PrefetchManager() {
    // If a prefetch is pending, wait for it to complete before destruction
    if (impl_ && impl_->pending && impl_->pending->result.valid()) {
        try {
            impl_->pending->result.wait();
        } catch (...) {
            // Suppress exceptions during destruction
        }
    }
}

PrefetchManager::PrefetchManager(PrefetchManager&&) noexcept = default;
PrefetchManager& PrefetchManager::operator=(PrefetchManager&&) noexcept = default;

// ─── Public Interface ────────────────────────────────────────────────────────

auto PrefetchManager::issue_prefetch(io::AmioReader& reader,
                                     std::string_view field_name,
                                     std::size_t time_index,
                                     std::span<double> buffer) -> int {
    // Disabled mode: no-op
    if (!impl_->enabled) {
        return 0;
    }

    // Cannot issue if a prefetch is already pending
    if (impl_->pending) {
        std::fprintf(stderr,
            "[tide::prefetch] Warning: cannot issue prefetch while one is pending\n");
        return to_int(ErrorCode::PrefetchFailed);
    }

    // Create the pending request and launch async read
    auto request = std::make_unique<PendingRequest>();
    request->reader = &reader;
    request->field_name = std::string(field_name);
    request->time_index = time_index;
    request->buffer = buffer;

    // Launch async read via std::async
    // Capture raw pointers/values since the request outlives this scope
    io::AmioReader* reader_ptr = &reader;
    std::string fname(field_name);
    std::size_t tidx = time_index;
    std::span<double> buf = buffer;

    request->result = std::async(std::launch::async,
        [reader_ptr, fname, tidx, buf]() -> int {
            return reader_ptr->read_time_level(fname, tidx, buf);
        }
    );

    impl_->pending = std::move(request);
    return 0;
}

auto PrefetchManager::wait_prefetch() -> int {
    // Disabled mode: no-op
    if (!impl_->enabled) {
        return 0;
    }

    // No pending request: nothing to wait for
    if (!impl_->pending) {
        return 0;
    }

    auto& request = impl_->pending;
    int rc = 0;

    // Wait for the async result
    if (request->result.valid()) {
        try {
            rc = request->result.get();
        } catch (...) {
            // Async operation threw an exception — treat as failure
            rc = to_int(ErrorCode::PrefetchFailed);
        }
    }

    // If prefetch failed, fall back to synchronous read
    if (rc != 0) {
        std::fprintf(stderr,
            "[tide::prefetch] Warning: async prefetch failed (rc=%d), "
            "falling back to synchronous read for field '%s' at time index %zu\n",
            rc, request->field_name.c_str(), request->time_index);

        // Attempt synchronous fallback read
        int fallback_rc = request->reader->read_time_level(
            request->field_name, request->time_index, request->buffer);

        if (fallback_rc != 0) {
            std::fprintf(stderr,
                "[tide::prefetch] Error: synchronous fallback also failed (rc=%d)\n",
                fallback_rc);
            impl_->pending.reset();
            return to_int(ErrorCode::PrefetchFailed);
        }

        // Fallback succeeded — reset pending and return success
        impl_->pending.reset();
        return 0;
    }

    // Prefetch succeeded — clear pending state
    impl_->pending.reset();
    return 0;
}

bool PrefetchManager::has_pending() const noexcept {
    if (!impl_->enabled) {
        return false;
    }
    return impl_->pending != nullptr;
}

bool PrefetchManager::is_enabled() const noexcept {
    return impl_->enabled;
}

int PrefetchManager::depth() const noexcept {
    return impl_->depth;
}

} // namespace tide::prefetch
