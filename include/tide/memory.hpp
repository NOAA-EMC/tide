/**
 * @file memory.hpp
 * @brief Memory budget computation and enforcement for the TIDE library.
 *
 * Provides the MemoryBudget class that computes per-stream buffer
 * requirements using the formula from the design specification, and
 * enforces a configurable memory budget limit across all streams.
 *
 * The per-stream buffer formula is:
 *   2 × src_cells × src_levels × 8   (ring buffer: two time levels)
 * + tgt_cols × tgt_levels × 8         (spatial output buffer)
 * + tgt_cols × tgt_levels × 8         (vertical output buffer)
 * + tgt_cols × tgt_levels × 8         (final output buffer)
 *
 * When memory_budget_mb is 0 (or unspecified), no limit is enforced.
 *
 * @see ErrorCode::MemoryBudgetExceeded (900)
 */

#ifndef TIDE_MEMORY_HPP
#define TIDE_MEMORY_HPP

#include <cstddef>
#include <expected>
#include <string>
#include <vector>

#include "tide/error.hpp"
#include "tide/types.hpp"

namespace tide {

/**
 * @brief Per-stream buffer requirement parameters.
 *
 * Captures the grid dimensions needed to compute memory requirements
 * for a single forcing stream's pipeline buffers.
 */
struct StreamBufferReq {
    /// @brief Number of horizontal cells on the source grid.
    std::size_t src_cells{0};

    /// @brief Number of vertical levels on the source grid.
    std::size_t src_levels{0};

    /// @brief Number of horizontal columns on the target grid.
    std::size_t tgt_cols{0};

    /// @brief Number of vertical levels on the target grid.
    std::size_t tgt_levels{0};

    /// @brief Stream name for diagnostic error messages.
    std::string stream_name;
};

/**
 * @brief Memory budget computation and enforcement controller.
 *
 * Tracks per-stream buffer requirements and checks the total allocation
 * against a configurable budget. Used during multi-stream initialization
 * to fail early if the combined buffer requirements would exceed available
 * memory.
 *
 * The budget acts as an init-time gate — it does not manage a runtime
 * memory pool or custom allocator.
 */
class MemoryBudget {
public:
    /**
     * @brief Construct a MemoryBudget controller.
     *
     * @param budget_mb Maximum allowed memory in megabytes.
     *                  A value of 0 means unbounded (no limit enforced).
     */
    explicit MemoryBudget(std::size_t budget_mb) noexcept;

    /**
     * @brief Add a stream's buffer requirements to the budget tracker.
     *
     * Computes the memory requirement for the given stream and records
     * it in the internal list.
     *
     * @param req Stream buffer requirement parameters.
     */
    void add_stream(const StreamBufferReq& req);

    /**
     * @brief Check if total memory usage exceeds the budget.
     *
     * If the budget is unbounded (budget_mb == 0), always returns success.
     * Otherwise, returns an error identifying the stream that caused the
     * budget to be exceeded (the last stream added that pushed total over
     * the limit).
     *
     * @return void on success, Error with MemoryBudgetExceeded on failure.
     */
    [[nodiscard]] auto check_budget() const -> std::expected<void, Error>;

    /**
     * @brief Get the total computed memory usage across all streams.
     *
     * @return Total buffer allocation in bytes.
     */
    [[nodiscard]] std::size_t total_usage() const noexcept;

    /**
     * @brief Get the memory usage of a specific stream by index.
     *
     * @param index Zero-based stream index.
     * @return Memory usage in bytes for that stream.
     * @throws std::out_of_range if index >= number of streams added.
     */
    [[nodiscard]] std::size_t stream_usage(std::size_t index) const;

    /**
     * @brief Compute the buffer memory requirement for a single stream.
     *
     * Formula: 2 × src_cells × src_levels × 8
     *        + tgt_cols × tgt_levels × 8 × 3
     *
     * @param req Stream buffer requirement parameters.
     * @return Computed memory requirement in bytes.
     */
    [[nodiscard]] static std::size_t compute_stream_bytes(
        const StreamBufferReq& req) noexcept;

    /**
     * @brief Check if the budget is unbounded (no limit).
     *
     * @return true if budget_mb was 0 at construction.
     */
    [[nodiscard]] bool is_unbounded() const noexcept;

    /**
     * @brief Get the configured budget limit in bytes.
     *
     * @return Budget limit in bytes (0 if unbounded).
     */
    [[nodiscard]] std::size_t budget_bytes() const noexcept;

    /**
     * @brief Get the number of streams added.
     *
     * @return Number of streams tracked.
     */
    [[nodiscard]] std::size_t stream_count() const noexcept;

private:
    /// @brief Budget limit in bytes (0 = unbounded).
    std::size_t budget_bytes_;

    /// @brief Per-stream memory usage in bytes.
    std::vector<std::size_t> stream_bytes_;

    /// @brief Per-stream names for error messages.
    std::vector<std::string> stream_names_;

    /// @brief Cached total usage in bytes.
    std::size_t total_bytes_{0};
};

} // namespace tide

#endif // TIDE_MEMORY_HPP
