/**
 * @file memory.cpp
 * @brief Implementation of the MemoryBudget class for TIDE.
 *
 * Computes per-stream buffer requirements and enforces a configurable
 * memory budget limit. See include/tide/memory.hpp for full documentation.
 */

#include "tide/memory.hpp"

#include <limits>
#include <stdexcept>
#include <string>

namespace tide {

// Conversion factor: 1 MB = 1,048,576 bytes
static constexpr std::size_t kBytesPerMB = 1048576;

MemoryBudget::MemoryBudget(std::size_t budget_mb) noexcept
    : budget_bytes_(budget_mb * kBytesPerMB) {}

void MemoryBudget::add_stream(const StreamBufferReq& req) {
    const std::size_t bytes = compute_stream_bytes(req);
    stream_bytes_.push_back(bytes);
    stream_names_.push_back(req.stream_name);
    total_bytes_ += bytes;
}

auto MemoryBudget::check_budget() const -> std::expected<void, Error> {
    // Unbounded mode: no limit enforced
    if (budget_bytes_ == 0) {
        return {};
    }

    if (total_bytes_ > budget_bytes_) {
        // Identify which stream caused the budget to be exceeded.
        // Walk streams in order and find the first one that pushes
        // the running total over the limit.
        std::size_t running = 0;
        std::string offending_stream = "unknown";
        for (std::size_t i = 0; i < stream_bytes_.size(); ++i) {
            running += stream_bytes_[i];
            if (running > budget_bytes_) {
                offending_stream = stream_names_[i];
                break;
            }
        }

        std::string msg = "Memory budget exceeded: total requirement is " +
                          std::to_string(total_bytes_) + " bytes (" +
                          std::to_string(total_bytes_ / kBytesPerMB) +
                          " MB), budget is " +
                          std::to_string(budget_bytes_ / kBytesPerMB) +
                          " MB. Stream '" + offending_stream +
                          "' caused the budget to be exceeded.";

        return std::unexpected(Error{
            .code = to_int(ErrorCode::MemoryBudgetExceeded),
            .message = std::move(msg),
            .context = "memory"});
    }

    return {};
}

std::size_t MemoryBudget::total_usage() const noexcept {
    return total_bytes_;
}

std::size_t MemoryBudget::stream_usage(std::size_t index) const {
    if (index >= stream_bytes_.size()) {
        throw std::out_of_range(
            "MemoryBudget::stream_usage: index " + std::to_string(index) +
            " out of range (stream count: " +
            std::to_string(stream_bytes_.size()) + ")");
    }
    return stream_bytes_[index];
}

std::size_t MemoryBudget::compute_stream_bytes(
    const StreamBufferReq& req) noexcept {
    // Check for potential overflow on multiplication
    const std::size_t src_product = req.src_cells * req.src_levels;
    const std::size_t tgt_product = req.tgt_cols * req.tgt_levels;

    // Guard against overflow in src_cells * src_levels
    if (req.src_cells > 0 && req.src_levels > 0 &&
        src_product / req.src_cells != req.src_levels) {
        return std::numeric_limits<std::size_t>::max();
    }
    // Guard against overflow in tgt_cols * tgt_levels
    if (req.tgt_cols > 0 && req.tgt_levels > 0 &&
        tgt_product / req.tgt_cols != req.tgt_levels) {
        return std::numeric_limits<std::size_t>::max();
    }

    // Ring buffer: 2 time levels × source cells × source levels × sizeof(double)
    const std::size_t ring_buffer = 2 * src_product * 8;

    // Check ring_buffer overflow (2 * src_product * 8)
    if (src_product > 0 && ring_buffer / src_product != 16) {
        return std::numeric_limits<std::size_t>::max();
    }

    // Target buffers: 3 buffers × target cols × target levels × sizeof(double)
    const std::size_t target_buffers = 3 * tgt_product * 8;

    // Check target_buffers overflow (3 * tgt_product * 8)
    if (tgt_product > 0 && target_buffers / tgt_product != 24) {
        return std::numeric_limits<std::size_t>::max();
    }

    // Check final addition overflow
    if (ring_buffer > std::numeric_limits<std::size_t>::max() - target_buffers) {
        return std::numeric_limits<std::size_t>::max();
    }

    return ring_buffer + target_buffers;
}

bool MemoryBudget::is_unbounded() const noexcept {
    return budget_bytes_ == 0;
}

std::size_t MemoryBudget::budget_bytes() const noexcept {
    return budget_bytes_;
}

std::size_t MemoryBudget::stream_count() const noexcept {
    return stream_bytes_.size();
}

} // namespace tide
