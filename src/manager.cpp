/**
 * @file manager.cpp
 * @brief TIDE Multi-Stream Manager implementation.
 *
 * Implements the StreamManager class which coordinates lifecycle and
 * collective operations across multiple concurrent forcing streams.
 * Key responsibilities:
 * - Bulk initialization with memory budget validation
 * - Per-stream error isolation during advance_all()
 * - Per-stream status tracking (Healthy, Failed, Finalized)
 * - Integration of StageTimers and PrefetchManagers per stream
 * - Bulk resource finalization
 *
 * Validates Requirements: 4.1, 4.2, 4.3, 4.5, 4.6, 4.7, 8.1, 8.2, 8.3, 8.4, 8.5
 */

#include "tide/manager.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/memory.hpp"
#include "tide/perf.hpp"
#include "tide/prefetch.hpp"
#include "tide/stream.hpp"
#include "tide/types.hpp"

namespace tide {

// ─────────────────────────────────────────────────────────────────────────────
// Implementation (PImpl)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Private implementation of StreamManager.
 *
 * Holds all managed streams along with their per-stream metadata:
 * status codes, error codes, timers, and prefetch managers.
 */
struct StreamManager::Impl {
    /// @brief All managed streams.
    std::vector<std::unique_ptr<Stream>> streams;

    /// @brief Per-stream status.
    std::vector<StreamStatus> status;

    /// @brief Per-stream error codes (from last failed advance).
    std::vector<int> error_codes;

    /// @brief Per-stream timers.
    std::vector<perf::StageTimers> timers;

    /// @brief Per-stream prefetch managers.
    std::vector<prefetch::PrefetchManager> prefetchers;

    /// @brief Memory budget in bytes (0 = unlimited).
    std::size_t memory_budget_bytes{0};

    /// @brief Total allocated buffer memory (bytes), computed at init.
    std::size_t total_allocated_bytes{0};

    /// @brief Whether timers are enabled.
    bool timers_enabled{true};

    /// @brief Whether finalize_all() has been called.
    bool finalized{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction / move
// ─────────────────────────────────────────────────────────────────────────────

StreamManager::StreamManager() : impl_(std::make_unique<Impl>()) {}

StreamManager::~StreamManager() {
    if (impl_ && !impl_->finalized) {
        finalize_all();
    }
}

StreamManager::StreamManager(StreamManager&&) noexcept = default;
StreamManager& StreamManager::operator=(StreamManager&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// create() — static factory
// ─────────────────────────────────────────────────────────────────────────────

auto StreamManager::create(const config::TideConfig& config,
                           MPI_Comm comm,
                           std::size_t memory_budget_mb)
    -> std::expected<StreamManager, Error> {

    StreamManager manager;
    auto& impl = *manager.impl_;

    const std::size_t num_streams = config.streams.size();

    // Determine effective memory budget:
    // Parameter overrides config value; if both are 0, budget is unlimited.
    const std::size_t effective_budget_mb =
        (memory_budget_mb > 0) ? memory_budget_mb : config.memory_budget_mb;

    impl.memory_budget_bytes = effective_budget_mb * 1048576ULL;
    impl.timers_enabled = config.enable_timers;

    // ── Memory Budget Computation ────────────────────────────────────────
    // Compute per-stream buffer requirements before creating streams.
    // This allows early failure if budget would be exceeded.
    MemoryBudget budget(effective_budget_mb);

    for (std::size_t i = 0; i < num_streams; ++i) {
        const auto& stream_config = config.streams[i];

        // Resolve the target grid from config
        TargetGrid target_grid{};
        auto it = config.target_grids.find(stream_config.target_grid_ref);
        if (it != config.target_grids.end()) {
            target_grid = it->second;
        }

        // For budget computation, we use available grid dimensions.
        // In a real scenario these would come from file metadata, but
        // at this point we use what the config/target_grid provides.
        StreamBufferReq req{};
        req.stream_name = stream_config.name;
        // Source grid dimensions are not fully known until file is opened,
        // but target grid dimensions are available from config.
        req.tgt_cols = target_grid.num_cols;
        req.tgt_levels = target_grid.num_levels > 0 ? target_grid.num_levels : 1;
        // Source dimensions default to target if not otherwise specified
        // (conservative estimate; actual may be determined after Stream::create)
        req.src_cells = target_grid.num_cols;
        req.src_levels = target_grid.num_levels > 0 ? target_grid.num_levels : 1;

        budget.add_stream(req);
    }

    // Check memory budget
    auto budget_check = budget.check_budget();
    if (!budget_check) {
        return std::unexpected(budget_check.error());
    }

    impl.total_allocated_bytes = budget.total_usage();

    // ── Initialize Streams ───────────────────────────────────────────────
    impl.streams.reserve(num_streams);
    impl.status.resize(num_streams, StreamStatus::Healthy);
    impl.error_codes.resize(num_streams, 0);

    for (std::size_t i = 0; i < num_streams; ++i) {
        const auto& stream_config = config.streams[i];

        // Resolve target grid
        TargetGrid target_grid{};
        auto it = config.target_grids.find(stream_config.target_grid_ref);
        if (it != config.target_grids.end()) {
            target_grid = it->second;
        }

        // Create the stream
        auto stream_result = Stream::create(stream_config, target_grid, comm);
        if (!stream_result) {
            return std::unexpected(Error{
                .code = stream_result.error().code,
                .message = "Failed to create stream '" + stream_config.name +
                           "' (index " + std::to_string(i) + "): " +
                           stream_result.error().message,
                .context = "StreamManager::create"
            });
        }

        impl.streams.push_back(
            std::make_unique<Stream>(std::move(stream_result.value())));
    }

    // ── Initialize Per-Stream Timers ─────────────────────────────────────
    impl.timers.reserve(num_streams);
    for (std::size_t i = 0; i < num_streams; ++i) {
        impl.timers.emplace_back(impl.timers_enabled);
    }

    // ── Initialize Per-Stream Prefetch Managers ──────────────────────────
    impl.prefetchers.reserve(num_streams);
    for (std::size_t i = 0; i < num_streams; ++i) {
        const auto& stream_config = config.streams[i];
        prefetch::PrefetchConfig pf_config{};
        pf_config.enabled = stream_config.prefetch.enabled;
        pf_config.depth = stream_config.prefetch.depth;
        impl.prefetchers.emplace_back(pf_config);
    }

    return manager;
}

// ─────────────────────────────────────────────────────────────────────────────
// advance_all()
// ─────────────────────────────────────────────────────────────────────────────

auto StreamManager::advance_all(double target_time) -> std::vector<int> {
    if (!impl_) {
        return {};
    }

    auto& impl = *impl_;
    const std::size_t n = impl.streams.size();
    std::vector<int> results(n, 0);

    for (std::size_t i = 0; i < n; ++i) {
        // Skip streams that are already failed or finalized
        if (impl.status[i] == StreamStatus::Failed) {
            // Report the previous error code
            results[i] = impl.error_codes[i];
            continue;
        }

        if (impl.status[i] == StreamStatus::Finalized) {
            results[i] = 0;
            continue;
        }

        // Advance this stream with error isolation
        int rc = impl.streams[i]->advance(target_time);

        if (rc != 0) {
            // Mark stream as failed, record error code
            impl.status[i] = StreamStatus::Failed;
            impl.error_codes[i] = rc;
            results[i] = rc;
        } else {
            // Stream succeeded — keep healthy status
            impl.status[i] = StreamStatus::Healthy;
            impl.error_codes[i] = 0;
            results[i] = 0;
        }
    }

    return results;
}

// ─────────────────────────────────────────────────────────────────────────────
// stream_count()
// ─────────────────────────────────────────────────────────────────────────────

std::size_t StreamManager::stream_count() const noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->streams.size();
}

// ─────────────────────────────────────────────────────────────────────────────
// stream_status()
// ─────────────────────────────────────────────────────────────────────────────

StreamStatus StreamManager::stream_status(std::size_t index) const {
    if (!impl_ || index >= impl_->status.size()) {
        throw std::out_of_range(
            "StreamManager::stream_status: index " + std::to_string(index) +
            " out of range (stream_count = " +
            std::to_string(impl_ ? impl_->streams.size() : 0) + ")");
    }
    return impl_->status[index];
}

// ─────────────────────────────────────────────────────────────────────────────
// stream_at()
// ─────────────────────────────────────────────────────────────────────────────

Stream* StreamManager::stream_at(std::size_t index) {
    if (!impl_ || index >= impl_->streams.size()) {
        return nullptr;
    }
    return impl_->streams[index].get();
}

const Stream* StreamManager::stream_at(std::size_t index) const {
    if (!impl_ || index >= impl_->streams.size()) {
        return nullptr;
    }
    return impl_->streams[index].get();
}

// ─────────────────────────────────────────────────────────────────────────────
// finalize_all()
// ─────────────────────────────────────────────────────────────────────────────

void StreamManager::finalize_all() {
    if (!impl_ || impl_->finalized) {
        return;
    }

    for (std::size_t i = 0; i < impl_->streams.size(); ++i) {
        if (impl_->status[i] != StreamStatus::Finalized) {
            impl_->streams[i]->finalize();
            impl_->status[i] = StreamStatus::Finalized;
        }
    }

    impl_->finalized = true;
}

// ─────────────────────────────────────────────────────────────────────────────
// total_memory_usage()
// ─────────────────────────────────────────────────────────────────────────────

std::size_t StreamManager::total_memory_usage() const noexcept {
    if (!impl_) {
        return 0;
    }
    return impl_->total_allocated_bytes;
}

} // namespace tide
