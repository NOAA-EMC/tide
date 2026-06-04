/**
 * @file stream.cpp
 * @brief TIDE Pipeline Orchestrator implementation.
 *
 * Implements the Stream class which wires together all five pipeline
 * stages in sequence: AMIO I/O → temporal interpolation → Atlas spatial
 * regridding → TSPACK vertical interpolation → scaling engine.
 *
 * The implementation uses the PImpl pattern. All internal state (reader,
 * ring buffer, regridder, interpolator, output buffer) is owned by the
 * Impl struct, ensuring complete isolation between stream instances.
 *
 * Error propagation follows the "first error halts pipeline" strategy:
 * each stage is executed in order, and the first non-zero return code
 * stops execution and is propagated to the caller.
 *
 * Validates Requirements: 7.1, 7.2, 7.3, 7.4, 7.5, 7.6, 7.7, 7.8,
 *                         6.1, 6.2, 6.3, 6.4, 6.6
 */

#include "tide/stream.hpp"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/grid.hpp"
#include "tide/io.hpp"
#include "tide/perf.hpp"
#include "tide/prefetch.hpp"
#include "tide/scaling.hpp"
#include "tide/scrip.hpp"
#include "tide/spatial.hpp"
#include "tide/temporal.hpp"
#include "tide/types.hpp"
#include "tide/vertical.hpp"

namespace tide {

// ─────────────────────────────────────────────────────────────────────────────
// Implementation (PImpl)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Private implementation of Stream.
 *
 * Holds all pipeline stage components, intermediate buffers, and state
 * needed to execute the five-stage forcing pipeline. Each Stream::Impl
 * is completely independent — no shared mutable state exists between
 * instances.
 */
struct Stream::Impl {
    // ── Configuration ────────────────────────────────────────────────────
    /// @brief Stream configuration (file path, field name, modes, etc.)
    config::StreamConfig config;

    /// @brief Target grid specification from the host model.
    TargetGrid target_grid;

    // ── Stage 1: I/O ─────────────────────────────────────────────────────
    /// @brief AMIO file reader for MPI-parallel I/O.
    io::AmioReader reader;

    /// @brief Cached time values from the forcing file.
    std::vector<double> file_time_values;

    /// @brief Source grid extracted from file metadata.
    SourceGrid source_grid;

    // ── Stage 2: Temporal Interpolation ──────────────────────────────────
    /// @brief Ring buffer holding two bounding time levels.
    std::unique_ptr<temporal::RingBuffer> ring_buffer;

    /// @brief Index of the next time level to read from file.
    std::size_t next_time_index = 0;

    /// @brief Whether the ring buffer has been initially populated.
    bool buffer_initialized = false;

    // ── Stage 3: Spatial Regridding ──────────────────────────────────────
    /// @brief Atlas-based horizontal regridder with cached weights.
    spatial::AtlasRegridder regridder;

    /// @brief Cached CSR weight matrix for export (populated from SCRIP load
    ///        or Atlas extraction when wired in task 16.5).
    std::optional<scrip::CsrMatrix> cached_weights;

    // ── Stage 4: Vertical Interpolation ──────────────────────────────────
    /// @brief TSPACK vertical interpolation engine.
    vertical::TspackInterpolator vert_interp;

    // ── Intermediate Buffers ─────────────────────────────────────────────
    /// @brief Temporally interpolated field on source grid.
    std::vector<double> temporal_output;

    /// @brief Horizontally regridded field on target grid (ncols elements).
    std::vector<double> spatial_output;

    /// @brief Vertically interpolated field (ncols × nlevels_tgt).
    std::vector<double> vertical_output;

    // ── Output ───────────────────────────────────────────────────────────
    /// @brief Final output buffer exposed via get_field().
    FieldBuffer output_buffer;

    /// @brief Whether advance() has been called at least once successfully.
    bool field_computed = false;

    // ── Lifecycle ────────────────────────────────────────────────────────
    /// @brief Per-handle error buffer for error messages.
    ErrorBuffer error_buffer;

    /// @brief Whether the stream has been finalized.
    bool finalized = false;

    /// @brief Whether the stream was successfully initialized.
    bool initialized = false;

    // ── Async I/O Prefetch ───────────────────────────────────────────────
    /// @brief Manages async prefetch for upcoming time levels.
    std::unique_ptr<prefetch::PrefetchManager> prefetch_mgr;

    // ── Performance Instrumentation ──────────────────────────────────────
    /// @brief Per-stage cumulative timers for this stream.
    perf::StageTimers timers{false};
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction / move
// ─────────────────────────────────────────────────────────────────────────────

Stream::Stream() : impl_(std::make_unique<Impl>()) {}

Stream::~Stream() {
    if (impl_ && !impl_->finalized) {
        finalize();
    }
}

Stream::Stream(Stream&&) noexcept = default;
Stream& Stream::operator=(Stream&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// create() — static factory
// ─────────────────────────────────────────────────────────────────────────────

auto Stream::create(const config::StreamConfig& config,
                    const TargetGrid& target,
                    MPI_Comm comm)
    -> std::expected<Stream, Error> {

    Stream stream;
    auto& impl = *stream.impl_;

    impl.config = config;
    impl.target_grid = target;

    // ── Resolve ESMF-based target grid if specified ──────────────────────
    // When target grid is defined via ESMF mesh or grid spec file,
    // load coordinates from the file (Requirement 2.7)
    if (!target.esmf_mesh.empty()) {
        auto tgt_result = grid::read_esmf_as_target(
            std::filesystem::path(target.esmf_mesh), grid::EsmfFileType::Mesh);
        if (!tgt_result) {
            return std::unexpected(Error{
                .code = tgt_result.error().code,
                .message = "Failed to load ESMF mesh target grid '" +
                           target.esmf_mesh + "': " +
                           tgt_result.error().message,
                .context = "stream::create"
            });
        }
        // Merge loaded coordinates into target grid, preserving any
        // vertical level info already set by the caller
        auto loaded_tgt = std::move(tgt_result.value());
        impl.target_grid.num_cols = loaded_tgt.num_cols;
        impl.target_grid.lats = std::move(loaded_tgt.lats);
        impl.target_grid.lons = std::move(loaded_tgt.lons);
        if (impl.target_grid.num_levels == 0 && loaded_tgt.num_levels > 0) {
            impl.target_grid.num_levels = loaded_tgt.num_levels;
        }
    } else if (!target.esmf_grid_spec.empty()) {
        auto tgt_result = grid::read_esmf_as_target(
            std::filesystem::path(target.esmf_grid_spec), grid::EsmfFileType::GridSpec);
        if (!tgt_result) {
            return std::unexpected(Error{
                .code = tgt_result.error().code,
                .message = "Failed to load ESMF grid spec target grid '" +
                           target.esmf_grid_spec + "': " +
                           tgt_result.error().message,
                .context = "stream::create"
            });
        }
        auto loaded_tgt = std::move(tgt_result.value());
        impl.target_grid.num_cols = loaded_tgt.num_cols;
        impl.target_grid.lats = std::move(loaded_tgt.lats);
        impl.target_grid.lons = std::move(loaded_tgt.lons);
        if (impl.target_grid.num_levels == 0 && loaded_tgt.num_levels > 0) {
            impl.target_grid.num_levels = loaded_tgt.num_levels;
        }
    }

    // ── Stage 1: Open file and extract metadata ──────────────────────────
    int rc = impl.reader.open(config.file_path, comm);
    if (rc != 0) {
        return std::unexpected(Error{
            .code = rc,
            .message = "Failed to open forcing file: " +
                       config.file_path.string(),
            .context = "stream::create"
        });
    }

    // Extract source grid metadata — either from ESMF file or from CF metadata
    if (!config.source_esmf_mesh.empty()) {
        // Load source grid from ESMF mesh file (Requirement 2.1, 2.5)
        auto esmf_result = grid::read_esmf_mesh(config.source_esmf_mesh);
        if (!esmf_result) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = esmf_result.error().code,
                .message = "Failed to load ESMF mesh source grid '" +
                           config.source_esmf_mesh.string() + "': " +
                           esmf_result.error().message,
                .context = "stream::create"
            });
        }
        impl.source_grid = std::move(esmf_result.value());
    } else if (!config.source_esmf_grid_spec.empty()) {
        // Load source grid from ESMF grid spec file (Requirement 2.2, 2.6)
        auto esmf_result = grid::read_esmf_grid_spec(config.source_esmf_grid_spec);
        if (!esmf_result) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = esmf_result.error().code,
                .message = "Failed to load ESMF grid spec source grid '" +
                           config.source_esmf_grid_spec.string() + "': " +
                           esmf_result.error().message,
                .context = "stream::create"
            });
        }
        impl.source_grid = std::move(esmf_result.value());
    } else {
        // Default: extract source grid from file CF metadata (existing behavior)
        auto grid_result = impl.reader.get_source_grid();
        if (!grid_result) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = grid_result.error().code,
                .message = "Failed to extract source grid metadata: " +
                           grid_result.error().message,
                .context = "stream::create"
            });
        }
        impl.source_grid = std::move(grid_result.value());
    }

    // Get available time values
    impl.file_time_values = impl.reader.get_time_values();

    // ── Stage 2: Initialize ring buffer ──────────────────────────────────
    // Buffer size = number of source grid cells (for one 2D field slice)
    // For 3D fields with vertical levels, buffer per-level slices
    const std::size_t src_cells = impl.source_grid.num_cells;
    const std::size_t src_levels = impl.source_grid.levels.empty()
                                       ? 1
                                       : impl.source_grid.levels.size();
    const std::size_t buffer_elements = src_cells * src_levels;

    impl.ring_buffer = std::make_unique<temporal::RingBuffer>(buffer_elements);

    // ── Stage 3: Build remapping weights (SCRIP file or Atlas) ─────────
    if (!config.weight_file.empty()) {
        // Load pre-computed SCRIP weights from file
        auto scrip_result = scrip::read_scrip_weights(config.weight_file);
        if (!scrip_result) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = scrip_result.error().code,
                .message = "Failed to load SCRIP weight file '" +
                           config.weight_file.string() + "': " +
                           scrip_result.error().message,
                .context = "stream::create"
            });
        }

        auto csr = std::move(scrip_result.value());

        // Validate SCRIP dimensions against source and target grid sizes
        rc = scrip::validate_dimensions(csr, src_cells, impl.target_grid.num_cols);
        if (rc != 0) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = rc,
                .message = "SCRIP weight file dimensions mismatch: n_src=" +
                           std::to_string(csr.n_src) + " vs source_grid=" +
                           std::to_string(src_cells) + ", n_dst=" +
                           std::to_string(csr.n_dst) + " vs target_grid=" +
                           std::to_string(impl.target_grid.num_cols),
                .context = "stream::create"
            });
        }

        // Store the CSR matrix for use in advance() and export_weights()
        impl.cached_weights = std::move(csr);
    } else {
        // Compute Atlas weights at runtime
        auto interp_method = spatial::interp_method_from_string(config.interp_method);
        auto missing_mode = spatial::missing_mode_from_string(config.missing_data_mode);

        rc = impl.regridder.build_weights(impl.source_grid, impl.target_grid,
                                          interp_method, missing_mode);
        if (rc != 0) {
            impl.reader.close();
            return std::unexpected(Error{
                .code = rc,
                .message = "Failed to compute regridding weights for stream '" +
                           config.name + "'",
                .context = "stream::create"
            });
        }
    }

    // ── Stage 4: Configure vertical interpolator ─────────────────────────
    vertical::VerticalConfig vert_config{};
    vert_config.log_pressure = config.log_pressure;
    vert_config.max_extrap_distance = config.extrap_limit;
    impl.vert_interp = vertical::TspackInterpolator(vert_config);

    // ── Allocate intermediate buffers ────────────────────────────────────
    impl.temporal_output.resize(buffer_elements, 0.0);
    impl.spatial_output.resize(impl.target_grid.num_cols * src_levels, 0.0);

    const std::size_t tgt_levels = impl.target_grid.num_levels > 0 ? impl.target_grid.num_levels : 1;
    impl.vertical_output.resize(impl.target_grid.num_cols * tgt_levels, 0.0);

    // ── Allocate output buffer ───────────────────────────────────────────
    impl.output_buffer.resize(impl.target_grid.num_cols, tgt_levels, 1);

    // ── Initialize async prefetch manager ────────────────────────────────
    // Convert config::PrefetchConfig to prefetch::PrefetchConfig
    prefetch::PrefetchConfig pf_config{
        .enabled = config.prefetch.enabled,
        .depth = config.prefetch.depth
    };
    impl.prefetch_mgr = std::make_unique<prefetch::PrefetchManager>(pf_config);

    // ── Initialize per-stage timers (enabled by default) ─────────────────
    impl.timers = perf::StageTimers(true);

    impl.initialized = true;
    return stream;
}

// ─────────────────────────────────────────────────────────────────────────────
// create() with enable_timers parameter
// ─────────────────────────────────────────────────────────────────────────────

auto Stream::create(const config::StreamConfig& config,
                    const TargetGrid& target,
                    MPI_Comm comm,
                    bool enable_timers)
    -> std::expected<Stream, Error> {

    auto result = create(config, target, comm);
    if (result) {
        result.value().impl_->timers = perf::StageTimers(enable_timers);
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// advance()
// ─────────────────────────────────────────────────────────────────────────────

auto Stream::advance(double target_time) -> int {
    if (!impl_ || !impl_->initialized) {
        return to_int(ErrorCode::UninitializedHandle);
    }

    if (impl_->finalized) {
        return to_int(ErrorCode::AlreadyFinalized);
    }

    // Clear previous error state
    impl_->error_buffer.clear();

    auto& impl = *impl_;
    int rc = 0;

    // ══════════════════════════════════════════════════════════════════════
    // Stage 1: I/O — Populate ring buffer with bounding time levels
    // ══════════════════════════════════════════════════════════════════════

    impl.timers.start(perf::Stage::IO);

    if (!impl.buffer_initialized) {
        // First call: read the initial two time levels into the ring buffer
        if (impl.file_time_values.size() < 1) {
            impl.timers.stop(perf::Stage::IO);
            impl.error_buffer.set(ErrorCode::TimeOutOfRange, "stream",
                                  "No time levels available in forcing file");
            return to_int(ErrorCode::TimeOutOfRange);
        }

        // Find the bracketing time levels for target_time
        std::size_t bracket_idx = 0;
        bool found_bracket = false;

        for (std::size_t i = 0; i + 1 < impl.file_time_values.size(); ++i) {
            if (target_time >= impl.file_time_values[i] &&
                target_time <= impl.file_time_values[i + 1]) {
                bracket_idx = i;
                found_bracket = true;
                break;
            }
        }

        // For cyclical mode, allow wrapping
        if (!found_bracket &&
            impl.config.temporal_mode != config::TemporalMode::Cyclical) {
            // Check if target_time is before the first level (use first two)
            if (impl.file_time_values.size() >= 2 &&
                target_time <= impl.file_time_values[0]) {
                bracket_idx = 0;
                found_bracket = true;
            }
            // Check if target_time is at or after the last level
            else if (impl.file_time_values.size() >= 2 &&
                     target_time >= impl.file_time_values.back()) {
                impl.timers.stop(perf::Stage::IO);
                impl.error_buffer.set(ErrorCode::TimeOutOfRange, "stream",
                                      "Target time exceeds file time range");
                return to_int(ErrorCode::TimeOutOfRange);
            }
        }

        // For cyclical mode with no bracket found, use wrap-around
        if (!found_bracket &&
            impl.config.temporal_mode == config::TemporalMode::Cyclical) {
            // Wrap: T_prev = last time level, T_next = first time level
            bracket_idx = impl.file_time_values.size() - 1;

            // Read last level into ring buffer as "prev"
            auto slot = impl.ring_buffer->next_slot();
            rc = impl.reader.read_time_level(impl.config.field_name,
                                             bracket_idx, slot);
            if (rc != 0) {
                impl.timers.stop(perf::Stage::IO);
                impl.error_buffer.set(from_int(rc), "io",
                                      "Failed to read time level for ring buffer");
                return rc;
            }
            impl.ring_buffer->rotate();

            // Read first level as "next"
            slot = impl.ring_buffer->next_slot();
            rc = impl.reader.read_time_level(impl.config.field_name, 0, slot);
            if (rc != 0) {
                impl.timers.stop(perf::Stage::IO);
                impl.error_buffer.set(from_int(rc), "io",
                                      "Failed to read time level for ring buffer");
                return rc;
            }

            impl.ring_buffer->set_times(
                impl.file_time_values[bracket_idx],
                impl.file_time_values[0]);

            impl.next_time_index = 1;
            impl.buffer_initialized = true;
            found_bracket = true;

            // Issue initial prefetch for upcoming time level (Req 5.1)
            if (impl.prefetch_mgr && impl.prefetch_mgr->is_enabled() &&
                impl.next_time_index < impl.file_time_values.size()) {
                auto prefetch_slot = impl.ring_buffer->next_slot();
                impl.prefetch_mgr->issue_prefetch(
                    impl.reader, impl.config.field_name,
                    impl.next_time_index, prefetch_slot);
                // Prefetch failure is non-fatal (Req 5.5)
            }
        }

        if (!found_bracket) {
            impl.timers.stop(perf::Stage::IO);
            impl.error_buffer.set(ErrorCode::TimeOutOfRange, "stream",
                                  "Target time does not fall within any time bracket");
            return to_int(ErrorCode::TimeOutOfRange);
        }

        if (!impl.buffer_initialized) {
            // Read T_prev (bracket_idx)
            auto slot = impl.ring_buffer->next_slot();
            rc = impl.reader.read_time_level(impl.config.field_name,
                                             bracket_idx, slot);
            if (rc != 0) {
                impl.timers.stop(perf::Stage::IO);
                impl.error_buffer.set(from_int(rc), "io",
                                      "Failed to read time level for ring buffer");
                return rc;
            }
            impl.ring_buffer->rotate();

            // Read T_next (bracket_idx + 1)
            slot = impl.ring_buffer->next_slot();
            rc = impl.reader.read_time_level(impl.config.field_name,
                                             bracket_idx + 1, slot);
            if (rc != 0) {
                impl.timers.stop(perf::Stage::IO);
                impl.error_buffer.set(from_int(rc), "io",
                                      "Failed to read time level for ring buffer");
                return rc;
            }

            impl.ring_buffer->set_times(
                impl.file_time_values[bracket_idx],
                impl.file_time_values[bracket_idx + 1]);

            impl.next_time_index = bracket_idx + 2;
            impl.buffer_initialized = true;

            // Issue initial prefetch for upcoming time level (Req 5.1)
            if (impl.prefetch_mgr && impl.prefetch_mgr->is_enabled() &&
                impl.next_time_index < impl.file_time_values.size()) {
                auto prefetch_slot = impl.ring_buffer->next_slot();
                impl.prefetch_mgr->issue_prefetch(
                    impl.reader, impl.config.field_name,
                    impl.next_time_index, prefetch_slot);
                // Prefetch failure is non-fatal (Req 5.5)
            }
        }
    } else {
        // Subsequent calls: check if we need to advance the ring buffer
        while (target_time > impl.ring_buffer->t_next()) {
            // Need to read the next time level
            if (impl.next_time_index >= impl.file_time_values.size()) {
                // For cyclical mode, wrap around
                if (impl.config.temporal_mode == config::TemporalMode::Cyclical) {
                    impl.next_time_index = 0;
                } else {
                    impl.timers.stop(perf::Stage::IO);
                    impl.error_buffer.set(ErrorCode::TimeLevelsExhausted, "stream",
                                          "No more time levels available in forcing file");
                    return to_int(ErrorCode::TimeLevelsExhausted);
                }
            }

            // Rotate: current next becomes prev
            impl.ring_buffer->rotate();

            // Read new next time level — use prefetch if pending, else sync
            auto slot = impl.ring_buffer->next_slot();

            if (impl.prefetch_mgr && impl.prefetch_mgr->has_pending()) {
                // A prefetch was issued for this time level — wait for it.
                // Note: wait_prefetch() calls future::get() which guarantees
                // the async operation has fully completed (thread joined)
                // before returning. There is no data race on the buffer even
                // in the fallback path, because std::future::get() provides
                // the happens-before synchronization between the async task's
                // writes and the caller's subsequent reads/writes.
                rc = impl.prefetch_mgr->wait_prefetch();
                // If prefetch failed, the PrefetchManager falls back to
                // synchronous read internally. If that also fails, rc != 0.
                if (rc != 0) {
                    // Fallback: attempt a direct synchronous read.
                    // This is safe because wait_prefetch() already called
                    // future::get(), ensuring the async task has fully
                    // completed — no concurrent writes to 'slot' are possible.
                    rc = impl.reader.read_time_level(impl.config.field_name,
                                                     impl.next_time_index, slot);
                    if (rc != 0) {
                        impl.timers.stop(perf::Stage::IO);
                        impl.error_buffer.set(from_int(rc), "io",
                                              "Failed to read next time level");
                        return rc;
                    }
                }
            } else {
                // No prefetch pending — synchronous read (default behavior)
                rc = impl.reader.read_time_level(impl.config.field_name,
                                                 impl.next_time_index, slot);
                if (rc != 0) {
                    impl.timers.stop(perf::Stage::IO);
                    impl.error_buffer.set(from_int(rc), "io",
                                          "Failed to read next time level");
                    return rc;
                }
            }

            double new_t_next = impl.file_time_values[impl.next_time_index];
            impl.ring_buffer->set_times(impl.ring_buffer->t_next(), new_t_next);
            impl.next_time_index++;

            // Issue prefetch for the next upcoming time level (Req 5.3)
            if (impl.prefetch_mgr && impl.prefetch_mgr->is_enabled() &&
                !impl.prefetch_mgr->has_pending()) {
                std::size_t prefetch_index = impl.next_time_index;
                // Handle cyclical wrap-around
                if (prefetch_index >= impl.file_time_values.size()) {
                    if (impl.config.temporal_mode == config::TemporalMode::Cyclical) {
                        prefetch_index = 0;
                    }
                }
                // Only prefetch if a valid index exists
                if (prefetch_index < impl.file_time_values.size()) {
                    auto prefetch_slot = impl.ring_buffer->next_slot();
                    impl.prefetch_mgr->issue_prefetch(
                        impl.reader, impl.config.field_name,
                        prefetch_index, prefetch_slot);
                    // Prefetch failure is non-fatal (Req 5.5)
                }
            }
        }
    }

    impl.timers.stop(perf::Stage::IO);

    // ══════════════════════════════════════════════════════════════════════
    // Stage 2: Temporal Interpolation
    // ══════════════════════════════════════════════════════════════════════

    impl.timers.start(perf::Stage::Temporal);

    switch (impl.config.temporal_mode) {
        case config::TemporalMode::Linear:
            rc = temporal::interpolate_linear(
                *impl.ring_buffer, target_time,
                std::span<double>(impl.temporal_output));
            break;

        case config::TemporalMode::Cyclical:
            rc = temporal::interpolate_cyclical(
                *impl.ring_buffer, target_time,
                std::span<double>(impl.temporal_output));
            break;

        case config::TemporalMode::SeasonalPreservation:
            rc = temporal::interpolate_seasonal(
                *impl.ring_buffer, target_time,
                std::span<const double>(impl.file_time_values),
                std::span<double>(impl.temporal_output));
            break;
    }

    if (rc != 0) {
        impl.timers.stop(perf::Stage::Temporal);
        impl.error_buffer.set(from_int(rc), "temporal",
                              "Temporal interpolation failed");
        return rc;
    }

    impl.timers.stop(perf::Stage::Temporal);

    // ══════════════════════════════════════════════════════════════════════
    // Stage 3: Spatial Regridding (SCRIP CSR or Atlas)
    // ══════════════════════════════════════════════════════════════════════

    impl.timers.start(perf::Stage::Spatial);

    // For 3D fields, apply regridding level-by-level
    const std::size_t src_levels = impl.source_grid.levels.empty()
                                       ? 1
                                       : impl.source_grid.levels.size();
    const std::size_t src_cells = impl.source_grid.num_cells;
    const std::size_t tgt_cols = impl.target_grid.num_cols;

    for (std::size_t lev = 0; lev < src_levels; ++lev) {
        // Source slice for this level: temporal_output[lev*src_cells .. (lev+1)*src_cells)
        std::span<const double> src_slice(
            impl.temporal_output.data() + lev * src_cells, src_cells);

        // Target slice for this level: spatial_output[lev*tgt_cols .. (lev+1)*tgt_cols)
        std::span<double> tgt_slice(
            impl.spatial_output.data() + lev * tgt_cols, tgt_cols);

        if (impl.cached_weights.has_value()) {
            // Use pre-loaded SCRIP weights (CSR mat-vec)
            rc = scrip::apply_csr(impl.cached_weights.value(),
                                  src_slice, tgt_slice);
        } else {
            // Use Atlas-computed weights
            rc = impl.regridder.apply(src_slice, tgt_slice);
        }

        if (rc != 0) {
            impl.timers.stop(perf::Stage::Spatial);
            impl.error_buffer.set(from_int(rc), "spatial",
                                  "Spatial regridding failed");
            return rc;
        }
    }

    impl.timers.stop(perf::Stage::Spatial);

    // ══════════════════════════════════════════════════════════════════════
    // Stage 4: Vertical Interpolation (TSPACK)
    // ══════════════════════════════════════════════════════════════════════

    impl.timers.start(perf::Stage::Vertical);

    const std::size_t tgt_levels = impl.target_grid.num_levels > 0
                                       ? impl.target_grid.num_levels
                                       : 1;

    if (src_levels > 1 && tgt_levels > 1 &&
        !impl.source_grid.levels.empty() &&
        !impl.target_grid.levels.empty()) {
        // Transpose spatial output from level-major [lev][col] to column-major
        // [col][lev] because TspackInterpolator::interpolate_field expects data
        // laid out as [col * n_src_levels + lev] (column-major per column).
        {
            std::vector<double> transposed(tgt_cols * src_levels);
            for (std::size_t lev = 0; lev < src_levels; ++lev) {
                for (std::size_t col = 0; col < tgt_cols; ++col) {
                    transposed[col * src_levels + lev] =
                        impl.spatial_output[lev * tgt_cols + col];
                }
            }
            impl.spatial_output = std::move(transposed);
        }

        // Perform vertical interpolation
        rc = impl.vert_interp.interpolate_field(
            std::span<const double>(impl.source_grid.levels),
            std::span<const double>(impl.spatial_output),
            std::span<const double>(impl.target_grid.levels),
            std::span<double>(impl.vertical_output),
            tgt_cols);

        if (rc != 0) {
            impl.timers.stop(perf::Stage::Vertical);
            impl.error_buffer.set(from_int(rc), "vertical",
                                  "Vertical interpolation failed");
            return rc;
        }

        // Transpose vertical output from column-major [col][lev] back to
        // level-major [lev][col] for the output buffer. The C API exposes
        // data via layout_left (Fortran column-major) which expects the
        // first dimension (ncols) to vary fastest, i.e., level-major storage.
        {
            std::vector<double> transposed(tgt_cols * tgt_levels);
            for (std::size_t col = 0; col < tgt_cols; ++col) {
                for (std::size_t lev = 0; lev < tgt_levels; ++lev) {
                    transposed[lev * tgt_cols + col] =
                        impl.vertical_output[col * tgt_levels + lev];
                }
            }
            impl.vertical_output = std::move(transposed);
        }
    } else {
        // No vertical interpolation needed — copy spatial output directly
        const std::size_t copy_size = std::min(
            impl.spatial_output.size(), impl.vertical_output.size());
        std::copy_n(impl.spatial_output.begin(), copy_size,
                    impl.vertical_output.begin());
    }

    impl.timers.stop(perf::Stage::Vertical);

    // ══════════════════════════════════════════════════════════════════════
    // Stage 5: Scaling (Y = M*X + B)
    // ══════════════════════════════════════════════════════════════════════

    impl.timers.start(perf::Stage::Scaling);

    // Copy vertical output into the final output buffer
    const std::size_t output_size = tgt_cols * tgt_levels;
    const std::size_t copy_count = std::min(output_size,
                                            impl.vertical_output.size());
    std::copy_n(impl.vertical_output.begin(), copy_count,
                impl.output_buffer.data.begin());

    // Apply scaling transform in-place on the output buffer
    rc = scaling::apply_linear_transform(
        std::span<double>(impl.output_buffer.data),
        impl.config.scaling.multiplier,
        impl.config.scaling.offset);

    if (rc != 0) {
        impl.timers.stop(perf::Stage::Scaling);
        impl.error_buffer.set(from_int(rc), "scaling",
                              "Scaling transform failed (invalid M or B)");
        return rc;
    }

    impl.timers.stop(perf::Stage::Scaling);

    impl.field_computed = true;
    return to_int(ErrorCode::Success);
}

// ─────────────────────────────────────────────────────────────────────────────
// get_field_data() — internal helper for template get_field()
// ─────────────────────────────────────────────────────────────────────────────

void Stream::get_field_data(std::string_view field_name,
                            const double*& data_ptr,
                            std::size_t& ncols,
                            std::size_t& nlevels,
                            std::size_t& nfields) const {
    data_ptr = nullptr;
    ncols = 0;
    nlevels = 0;
    nfields = 0;

    if (!impl_ || !impl_->initialized || impl_->finalized) {
        return;
    }

    // Check if the requested field matches this stream's configured field
    if (field_name != impl_->config.field_name) {
        // Field not found — set error indicator
        impl_->error_buffer.set(ErrorCode::FieldNotFound, "stream",
                                "Unknown field name requested");
        return;
    }

    // Check if advance() has been called and produced valid output
    if (!impl_->field_computed) {
        impl_->error_buffer.set(ErrorCode::FieldNotComputed, "stream",
                                "Field not yet computed (call advance() first)");
        return;
    }

    // Return pointer into the output buffer
    data_ptr = impl_->output_buffer.data.data();
    ncols = impl_->output_buffer.ncols;
    nlevels = impl_->output_buffer.nlevels;
    nfields = impl_->output_buffer.nfields;
}

// ─────────────────────────────────────────────────────────────────────────────
// export_weights() — write cached weight matrix to SCRIP NetCDF
// ─────────────────────────────────────────────────────────────────────────────

auto Stream::export_weights(const std::filesystem::path& output_path) -> int {
    if (!impl_ || !impl_->initialized) {
        return to_int(ErrorCode::UninitializedHandle);
    }

    if (impl_->finalized) {
        return to_int(ErrorCode::AlreadyFinalized);
    }

    impl_->error_buffer.clear();

    // If we don't have cached weights but the regridder has computed weights,
    // extract the CSR matrix from Atlas on-demand (Requirement 9.1, 9.3, 9.4).
    if (!impl_->cached_weights.has_value() && impl_->regridder.has_weights()) {
        auto extracted = impl_->regridder.extract_csr();
        if (extracted.n_s > 0 || extracted.n_dst > 0) {
            impl_->cached_weights = std::move(extracted);
        }
    }

    // Check if we have a cached CSR matrix available for export
    if (!impl_->cached_weights.has_value()) {
        // Weights are not available in exportable form.
        // This can happen if:
        // - The stream was not initialized with a SCRIP weight file
        // - Atlas weight computation failed or was not performed
        // - advance() has not been called yet on a stream that needs it
        impl_->error_buffer.set(ErrorCode::WeightComputationFailed, "export_weights",
                                "Weights not available for export: no cached CSR matrix");
        return to_int(ErrorCode::WeightComputationFailed);
    }

    // Write the cached CSR matrix to SCRIP format
    int rc = scrip::write_scrip_weights(impl_->cached_weights.value(), output_path);
    if (rc != 0) {
        impl_->error_buffer.set(from_int(rc), "export_weights",
                                "Failed to write SCRIP weight file to: " +
                                output_path.string());
        return rc;
    }

    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// finalize()
// ─────────────────────────────────────────────────────────────────────────────

void Stream::finalize() {
    if (!impl_ || impl_->finalized) {
        return;
    }

    // Release prefetch manager (waits for pending operations)
    impl_->prefetch_mgr.reset();

    // Close file handle
    impl_->reader.close();

    // Release ring buffer
    impl_->ring_buffer.reset();

    // Clear intermediate buffers
    impl_->temporal_output.clear();
    impl_->spatial_output.clear();
    impl_->vertical_output.clear();

    // Clear output buffer
    impl_->output_buffer.data.clear();
    impl_->output_buffer.ncols = 0;
    impl_->output_buffer.nlevels = 0;
    impl_->output_buffer.nfields = 0;

    impl_->field_computed = false;
    impl_->finalized = true;
    impl_->initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────
// is_valid()
// ─────────────────────────────────────────────────────────────────────────────

bool Stream::is_valid() const noexcept {
    return impl_ && impl_->initialized && !impl_->finalized;
}

// ─────────────────────────────────────────────────────────────────────────────
// last_error()
// ─────────────────────────────────────────────────────────────────────────────

const char* Stream::last_error() const noexcept {
    if (!impl_) {
        return "";
    }
    return impl_->error_buffer.message();
}

// ─────────────────────────────────────────────────────────────────────────────
// get_timers()
// ─────────────────────────────────────────────────────────────────────────────

perf::StageTimers& Stream::get_timers() noexcept {
    return impl_->timers;
}

const perf::StageTimers& Stream::get_timers() const noexcept {
    return impl_->timers;
}

} // namespace tide
