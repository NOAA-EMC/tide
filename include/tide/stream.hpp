/**
 * @file stream.hpp
 * @brief TIDE Pipeline Orchestrator — tide::Stream class.
 *
 * Provides the Stream class which manages the full forcing pipeline
 * lifecycle: initialization, time advancement through the five-stage
 * pipeline (read → temporal interp → spatial regrid → vertical interp
 * → scaling), field retrieval as zero-copy mdspan views, and resource
 * finalization.
 *
 * The class uses the PImpl pattern to hide all internal state (AMIO
 * reader, ring buffer, Atlas regridder, TSPACK interpolator, and output
 * buffer) from the public header.
 *
 * @section thread_safety Thread Safety
 * Each Stream instance is fully self-contained with its own buffers,
 * file handles, and cached weights. Thread safety is achieved through
 * isolation rather than synchronization:
 * - Concurrent advance() calls on different stream handles are safe.
 * - Concurrent access to the same stream handle is undefined behavior.
 * - No global mutable state exists in the library.
 *
 * @section memory_lifetime Memory Lifetime
 * The mdspan views returned by get_field() reference memory owned by the
 * Stream's internal buffer. The memory remains valid from the point of
 * return until the next call to advance(). After advance() returns,
 * previously issued views are invalidated.
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 1-99: I/O errors (file not found, unreadable, etc.)
 * - 200-299: Temporal errors (time out of range, exhausted)
 * - 300-399: Spatial errors (weight computation failed)
 * - 400-499: Vertical errors (insufficient levels)
 * - 500-599: Scaling errors (invalid parameters)
 * - 600-699: Lifecycle errors (uninitialized, already finalized)
 * - 700-799: Field errors (field not found, not computed)
 *
 * Validates Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8,
 *                         6.1, 6.2, 6.3, 6.4, 6.6
 */

#ifndef TIDE_STREAM_HPP
#define TIDE_STREAM_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <string_view>

#include <mpi.h>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/perf.hpp"
#include "tide/types.hpp"

namespace tide {

/**
 * @brief Pipeline orchestrator managing the full forcing data lifecycle.
 *
 * Stream is the central class of the TIDE library. It coordinates the
 * five-stage pipeline:
 *   1. AMIO parallel I/O (file reading into ring buffer)
 *   2. Temporal interpolation (linear, cyclical, or seasonal)
 *   3. Atlas horizontal regridding (cached CSR weight matrix)
 *   4. TSPACK vertical interpolation (monotonicity-preserving)
 *   5. Scaling engine (Y = M*X + B)
 *
 * The class is non-copyable but movable. Construction is via the static
 * factory method create(), which performs full initialization including
 * file opening, grid metadata extraction, and Atlas weight computation.
 *
 * @par Example Usage
 * @code
 * auto result = tide::Stream::create(stream_config, target_grid, MPI_COMM_WORLD);
 * if (!result) {
 *     std::cerr << result.error().message << "\n";
 *     return;
 * }
 * auto stream = std::move(result.value());
 *
 * int rc = stream.advance(3600.0); // advance to t=3600s
 * if (rc != 0) { handle_error(rc); }
 *
 * auto field = stream.get_field<std::layout_right>("temperature");
 * // field is a 3D mdspan (ncols x nlevels x 1)
 *
 * stream.finalize();
 * @endcode
 */
class Stream {
public:
    /**
     * @brief Initialize a stream from a parsed configuration.
     *
     * Performs complete initialization of the pipeline:
     * - Opens the forcing file via AMIO with MPI-parallel decomposition
     * - Extracts source grid metadata from file
     * - Computes and caches Atlas remapping weights
     * - Allocates internal buffers (ring buffer + output)
     *
     * @param config Parsed stream configuration (file path, field name,
     *              grid types, interpolation method, scaling, etc.)
     * @param target Target grid specification from the host model.
     * @param comm   MPI communicator for parallel I/O decomposition.
     * @return Stream on success, or Error with descriptive message on failure.
     *
     * @note The returned Stream takes ownership of all allocated resources.
     *       On failure, no persistent resources are allocated.
     */
    [[nodiscard]] static auto create(const config::StreamConfig& config,
                                     const TargetGrid& target,
                                     MPI_Comm comm)
        -> std::expected<Stream, Error>;

    /**
     * @brief Initialize a stream with explicit timer control.
     *
     * Same as create(config, target, comm) but allows the caller to
     * specify whether per-stage performance timers are enabled. This
     * overload is used when the global TideConfig.enable_timers flag
     * should be passed through to the stream.
     *
     * @param config Parsed stream configuration.
     * @param target Target grid specification from the host model.
     * @param comm   MPI communicator for parallel I/O decomposition.
     * @param enable_timers Whether per-stage timers are active.
     * @return Stream on success, or Error with descriptive message on failure.
     *
     * Validates Requirements: 7.4
     */
    [[nodiscard]] static auto create(const config::StreamConfig& config,
                                     const TargetGrid& target,
                                     MPI_Comm comm,
                                     bool enable_timers)
        -> std::expected<Stream, Error>;

    /**
     * @brief Advance the pipeline to a target simulation time.
     *
     * Executes the full five-stage pipeline:
     *   1. Reads bounding time levels into the ring buffer (if needed)
     *   2. Computes temporally interpolated source field
     *   3. Applies cached horizontal regridding weights
     *   4. Performs column-by-column vertical interpolation
     *   5. Applies linear scaling transform
     *
     * The first error in any stage halts the pipeline and propagates
     * the error code to the caller. The output buffer is not modified
     * if an error occurs.
     *
     * After successful return, the output field is available via
     * get_field(). Previously returned mdspan views are invalidated.
     *
     * @param target_time Simulation time in seconds since epoch.
     * @return 0 on success, or a non-zero error code from the failing stage.
     */
    auto advance(double target_time) -> int;

    /**
     * @brief Retrieve a computed field as a zero-copy mdspan view (C-layout).
     *
     * Returns a 3D mdspan view over the pipeline's output buffer with
     * shape (ncols, nlevels, 1) in row-major (layout_right) order.
     *
     * The returned view references memory owned by this Stream instance.
     * The memory remains valid until the next call to advance() or
     * finalize().
     *
     * @tparam Layout Memory layout: std::layout_right (C, default) or
     *                std::layout_left (Fortran column-major).
     * @param field_name Name of the field to retrieve. Must match the
     *                   field_name in the stream's configuration.
     * @return mdspan view with shape (ncols, nlevels, 1), or a null view
     *         (data pointer null, all extents zero) if the field name is
     *         unknown or has not been computed in the current cycle.
     */
    template <typename Layout = std::layout_right>
    [[nodiscard]] auto get_field(std::string_view field_name) const
        -> std::mdspan<const double, std::dextents<std::size_t, 3>, Layout>;

    /**
     * @brief Export the cached weight matrix to a SCRIP NetCDF file.
     *
     * Writes the internal remapping weight matrix to the specified path in
     * SCRIP format. Works with both SCRIP-loaded weights and Atlas-computed
     * weights (once converted to CSR format).
     *
     * @param output_path Path for the output SCRIP NetCDF file.
     * @return 0 on success, non-zero if weights are not available.
     *
     * @retval 0   Success — SCRIP file written.
     * @retval 300 Weights not available (not computed or not exportable).
     *
     * @pre is_valid() == true
     * @pre Weights must have been computed (via build_weights during create())
     *      or loaded from a SCRIP weight file.
     *
     * Validates Requirements: 9.1, 9.4
     */
    [[nodiscard]] auto export_weights(const std::filesystem::path& output_path) -> int;

    /**
     * @brief Get the per-stage performance timers for this stream.
     *
     * Returns a reference to the internal StageTimers object which
     * accumulates wall-clock elapsed time for each pipeline stage
     * during advance() calls.
     *
     * @return Reference to the stream's StageTimers.
     *
     * Validates Requirements: 7.1, 7.2
     */
    [[nodiscard]] perf::StageTimers& get_timers() noexcept;

    /**
     * @brief Get the per-stage performance timers (const).
     * @return Const reference to the stream's StageTimers.
     */
    [[nodiscard]] const perf::StageTimers& get_timers() const noexcept;

    /**
     * @brief Release all internal resources.
     *
     * Closes the AMIO file handle, deallocates all buffers, and releases
     * Atlas interpolation objects. After this call, the stream is in an
     * invalid state and must not be used for advance() or get_field().
     *
     * It is safe to call finalize() multiple times; subsequent calls are
     * no-ops.
     */
    void finalize();

    /**
     * @brief Check whether this stream has been initialized and not finalized.
     * @return true if the stream is ready for advance()/get_field() calls.
     */
    [[nodiscard]] bool is_valid() const noexcept;

    /**
     * @brief Get the last error message from this stream.
     *
     * Returns the error message from the most recent failed operation.
     * The pointer is valid until the next API call on this stream.
     *
     * @return Null-terminated error message string, or empty string if
     *         no error has occurred.
     */
    [[nodiscard]] const char* last_error() const noexcept;

    /// @brief Destructor — calls finalize() if not already done.
    ~Stream();

    /// @brief Move constructor.
    Stream(Stream&&) noexcept;

    /// @brief Move assignment operator.
    Stream& operator=(Stream&&) noexcept;

    // Non-copyable
    Stream(const Stream&) = delete;
    Stream& operator=(const Stream&) = delete;

private:
    /// @brief Private constructor — use create() factory method.
    Stream();

    /**
     * @brief Internal helper to retrieve field data pointer and dimensions.
     *
     * Used by the get_field() template to access PImpl data without
     * exposing the Impl struct in the header.
     *
     * @param field_name Name of the field to look up.
     * @param[out] data_ptr Receives pointer to the field data, or nullptr.
     * @param[out] ncols    Receives the number of horizontal columns.
     * @param[out] nlevels  Receives the number of vertical levels.
     * @param[out] nfields  Receives the number of fields (typically 1).
     */
    void get_field_data(std::string_view field_name,
                        const double*& data_ptr,
                        std::size_t& ncols,
                        std::size_t& nlevels,
                        std::size_t& nfields) const;

    /// @brief Forward-declared implementation (PImpl).
    struct Impl;

    /// @brief Pointer to the implementation.
    std::unique_ptr<Impl> impl_;
};

// ─────────────────────────────────────────────────────────────────────────────
// Template implementation (must be in header)
// ─────────────────────────────────────────────────────────────────────────────

template <typename Layout>
auto Stream::get_field(std::string_view field_name) const
    -> std::mdspan<const double, std::dextents<std::size_t, 3>, Layout> {
    using view_type = std::mdspan<const double, std::dextents<std::size_t, 3>, Layout>;

    const double* data_ptr = nullptr;
    std::size_t ncols = 0;
    std::size_t nlevels = 0;
    std::size_t nfields = 0;

    get_field_data(field_name, data_ptr, ncols, nlevels, nfields);

    if (data_ptr == nullptr) {
        return view_type{nullptr, std::dextents<std::size_t, 3>{0, 0, 0}};
    }

    return view_type{data_ptr, std::dextents<std::size_t, 3>{ncols, nlevels, nfields}};
}

} // namespace tide

#endif // TIDE_STREAM_HPP
