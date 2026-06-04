/**
 * @file c_api.cpp
 * @brief TIDE C API implementation.
 *
 * Implements the C-interoperable functions declared in tide.h by wrapping
 * the C++ tide::Stream class behind opaque void* handles. All functions
 * use extern "C" linkage and return integer status codes (0 = success).
 *
 * Memory ownership model:
 * - tide_init() allocates a StreamHandle on the heap and returns it as void*.
 * - tide_finalize() destroys the StreamHandle and frees its memory.
 * - Field data pointers returned by tide_get_field() are owned by TIDE
 *   and valid until the next tide_advance() call on the same handle.
 *
 * Error handling:
 * - A static thread_local error buffer stores the last global error
 *   (used when handle is NULL in tide_get_error).
 * - Per-handle errors are delegated to the Stream's internal error buffer.
 *
 * Validates Requirements: 4.1, 4.2, 4.3, 4.7, 4.8, 8.3, 12.1, 12.2, 12.4, 12.5, 12.6
 */

#include "tide.h"

#include <cstring>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <mpi.h>

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/memory.hpp"
#include "tide/perf.hpp"
#include "tide/scrip.hpp"
#include "tide/stream.hpp"
#include "tide/types.hpp"

namespace {

/**
 * @brief Internal handle structure wrapping a TIDE stream.
 *
 * This struct is allocated on the heap by tide_init() and freed by
 * tide_finalize(). It holds the Stream object (via unique_ptr since
 * Stream's default constructor is private), target grid information,
 * and a per-handle error buffer for tide_get_error().
 */
struct StreamHandle {
    /// @brief The C++ Stream object managing the pipeline.
    std::unique_ptr<tide::Stream> stream{nullptr};

    /// @brief Configuration used to create this stream.
    tide::config::TideConfig config{};

    /// @brief Per-handle error buffer for error messages.
    tide::ErrorBuffer error_buffer{};

    /// @brief Whether the handle has been finalized.
    bool finalized = false;

    /// @brief Cached field name for get_field lookups.
    std::string field_name{};

    /// @brief Per-handle stage timers for performance instrumentation.
    tide::perf::StageTimers timers{true};

    /// @brief Computed buffer memory usage for this stream (bytes).
    std::size_t memory_bytes{0};

    /// @brief Default constructor.
    StreamHandle() = default;
};

/**
 * @brief Thread-local global error buffer for initialization failures.
 *
 * Used when tide_get_error() is called with a NULL handle, e.g., after
 * a failed tide_init() call.
 */
thread_local tide::ErrorBuffer g_error_buffer;

/**
 * @brief Validate that a handle pointer is non-null and not finalized.
 *
 * @param handle The opaque handle to validate.
 * @return Pointer to the StreamHandle, or nullptr if invalid.
 */
StreamHandle* validate_handle(tide_handle_t handle) {
    if (!handle) {
        return nullptr;
    }
    auto* sh = static_cast<StreamHandle*>(handle);
    if (sh->finalized) {
        return nullptr;
    }
    return sh;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// C API Implementation
// ─────────────────────────────────────────────────────────────────────────────

extern "C" {

int tide_init(const char* config_path, int mpi_comm, tide_handle_t* handle) {
    // Clear global error state
    g_error_buffer.clear();

    // Validate output pointer
    if (!handle) {
        g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                           "Output handle pointer is NULL");
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }
    *handle = nullptr;

    // Validate config path
    if (!config_path || config_path[0] == '\0') {
        g_error_buffer.set(tide::ErrorCode::ConfigFileNotFound, "c_api",
                           "Configuration path is NULL or empty");
        return tide::to_int(tide::ErrorCode::ConfigFileNotFound);
    }

    // Parse YAML configuration
    auto config_result = tide::config::parse_yaml(config_path);
    if (!config_result) {
        const auto& err = config_result.error();
        g_error_buffer.set(tide::from_int(err.code), "c_api", err.message);
        return err.code;
    }

    auto& tide_config = config_result.value();

    // Resolve relative file paths in stream configs against the config
    // file's parent directory. This allows test_config.yaml to reference
    // "synthetic_forcing.nc" without an absolute path.
    std::filesystem::path config_dir =
        std::filesystem::path(config_path).parent_path();
    for (auto& stream : tide_config.streams) {
        if (stream.file_path.is_relative()) {
            stream.file_path = config_dir / stream.file_path;
        }
    }

    // We need at least one stream configured
    if (tide_config.streams.empty()) {
        g_error_buffer.set(tide::ErrorCode::ConfigMissingField, "c_api",
                           "No streams configured in YAML file");
        return tide::to_int(tide::ErrorCode::ConfigMissingField);
    }

    // Convert Fortran-compatible integer communicator to MPI_Comm
    MPI_Comm comm = MPI_Comm_f2c(mpi_comm);

    // For the C API, we initialize the first configured stream.
    // (Multi-stream management could be extended in the future.)
    const auto& stream_config = tide_config.streams[0];

    // Look up the target grid for this stream
    auto tgt_it = tide_config.target_grids.find(stream_config.target_grid_ref);
    if (tgt_it == tide_config.target_grids.end()) {
        g_error_buffer.set(tide::ErrorCode::ConfigMissingField, "c_api",
                           "Target grid '" + stream_config.target_grid_ref +
                           "' not found in configuration");
        return tide::to_int(tide::ErrorCode::ConfigMissingField);
    }

    // Create the C++ Stream object
    auto stream_result = tide::Stream::create(stream_config, tgt_it->second, comm,
                                              tide_config.enable_timers);
    if (!stream_result) {
        const auto& err = stream_result.error();
        g_error_buffer.set(tide::from_int(err.code), "c_api", err.message);
        return err.code;
    }

    // Allocate the handle structure
    auto* sh = new (std::nothrow) StreamHandle{};
    if (!sh) {
        g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                           "Memory allocation failed for stream handle");
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    sh->stream = std::make_unique<tide::Stream>(std::move(stream_result.value()));
    sh->config = std::move(tide_config);
    sh->field_name = stream_config.field_name;
    sh->finalized = false;

    // Initialize per-handle timers (respects enable_timers config)
    sh->timers = tide::perf::StageTimers(sh->config.enable_timers);

    // Compute buffer memory usage for this stream using the standard formula
    const auto& tgt = tgt_it->second;
    tide::StreamBufferReq req{};
    req.src_cells = tgt.num_cols;  // Conservative estimate: source ≈ target
    req.src_levels = tgt.num_levels > 0 ? tgt.num_levels : 1;
    req.tgt_cols = tgt.num_cols;
    req.tgt_levels = tgt.num_levels > 0 ? tgt.num_levels : 1;
    sh->memory_bytes = tide::MemoryBudget::compute_stream_bytes(req);

    *handle = static_cast<tide_handle_t>(sh);
    return 0;
}

int tide_advance(tide_handle_t handle, double target_time) {
    auto* sh = validate_handle(handle);
    if (!sh) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    sh->error_buffer.clear();

    int rc = sh->stream->advance(target_time);
    if (rc != 0) {
        // Copy the stream's error message to the handle error buffer
        const char* msg = sh->stream->last_error();
        sh->error_buffer.set(tide::from_int(rc), "advance",
                             msg ? msg : "Pipeline advance failed");
    }

    return rc;
}

int tide_get_field(tide_handle_t handle, const char* field_name,
                   double** data_ptr, int* rank, size_t extents[7]) {
    // Validate handle
    auto* sh = validate_handle(handle);
    if (!sh) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    // Validate output pointers
    if (!data_ptr || !rank || !extents) {
        sh->error_buffer.set(tide::ErrorCode::FieldNotFound, "c_api",
                             "Output pointer parameters must not be NULL");
        return tide::to_int(tide::ErrorCode::FieldNotFound);
    }

    // Initialize outputs
    *data_ptr = nullptr;
    *rank = 0;
    std::memset(extents, 0, 7 * sizeof(size_t));

    // Validate field name
    if (!field_name || field_name[0] == '\0') {
        sh->error_buffer.set(tide::ErrorCode::FieldNotFound, "c_api",
                             "Field name is NULL or empty");
        return tide::to_int(tide::ErrorCode::FieldNotFound);
    }

    // Use layout_left (Fortran column-major) for the C API since it's
    // designed for interop with Fortran callers.
    // get_field returns an mdspan view; we need the raw pointer and extents.
    auto view = sh->stream->get_field<std::layout_left>(field_name);

    if (view.data_handle() == nullptr) {
        // Field not found or not yet computed
        const char* msg = sh->stream->last_error();
        if (msg && msg[0] != '\0') {
            sh->error_buffer.set(tide::ErrorCode::FieldNotFound, "c_api", msg);
        } else {
            sh->error_buffer.set(tide::ErrorCode::FieldNotFound, "c_api",
                                 "Field not found or not yet computed");
        }
        return tide::to_int(tide::ErrorCode::FieldNotFound);
    }

    // The view is 3D: (ncols, nlevels, nfields)
    // Return the data pointer (const_cast is safe because TIDE owns this
    // memory and the C API contract says caller must not modify or free it)
    *data_ptr = const_cast<double*>(view.data_handle());

    // Determine rank based on extents
    // The underlying buffer is always 3D (ncols x nlevels x nfields)
    // but we report the effective rank:
    //   - If nfields==1 and nlevels==1: rank 1 (just horizontal columns)
    //   - If nfields==1 and nlevels>1: rank 2 (columns x levels)
    //   - Otherwise: rank 3
    const std::size_t ncols = view.extent(0);
    const std::size_t nlevels = view.extent(1);
    const std::size_t nfields = view.extent(2);

    if (nfields == 1 && nlevels == 1) {
        *rank = 1;
        extents[0] = ncols;
        for (int i = 1; i < 7; ++i) extents[i] = 1;
    } else if (nfields == 1) {
        *rank = 2;
        extents[0] = ncols;
        extents[1] = nlevels;
        for (int i = 2; i < 7; ++i) extents[i] = 1;
    } else {
        *rank = 3;
        extents[0] = ncols;
        extents[1] = nlevels;
        extents[2] = nfields;
        for (int i = 3; i < 7; ++i) extents[i] = 1;
    }

    return 0;
}

int tide_finalize(tide_handle_t handle) {
    if (!handle) {
        return tide::to_int(tide::ErrorCode::AlreadyFinalized);
    }

    auto* sh = static_cast<StreamHandle*>(handle);

    if (sh->finalized) {
        return tide::to_int(tide::ErrorCode::AlreadyFinalized);
    }

    // Finalize the underlying stream
    if (sh->stream) {
        sh->stream->finalize();
        sh->stream.reset();  // Release the Stream object (large allocation)
    }
    sh->finalized = true;

    // NOTE: We intentionally do NOT delete the StreamHandle here.
    // The handle remains valid (but finalized) to prevent use-after-free
    // if the caller accidentally calls tide_finalize twice on the same
    // pointer. The handle struct itself is small (~100 bytes) and will be
    // freed when tide_finalize_all is used (which nulls the caller's
    // pointer), or at program exit. This is a deliberate safety trade-off:
    // a trivial memory leak vs undefined behavior from double-free.

    return 0;
}

const char* tide_get_error(tide_handle_t handle) {
    if (!handle) {
        // Return global error buffer for initialization failures
        return g_error_buffer.message();
    }

    auto* sh = static_cast<StreamHandle*>(handle);

    // First check the handle-level error buffer
    if (sh->error_buffer.has_error()) {
        return sh->error_buffer.message();
    }

    // Fall back to the stream's internal error
    if (sh->stream) {
        return sh->stream->last_error();
    }

    return "";
}

// ─────────────────────────────────────────────────────────────────────────────
// Multi-Stream C API Implementation
// Validates Requirements: 4.1, 4.2, 4.3, 4.7, 4.8, 8.3
// ─────────────────────────────────────────────────────────────────────────────

int tide_init_multi(const char* config_path, int mpi_comm,
                    tide_handle_t* handles, int* num_streams, int max_streams) {
    // Clear global error state
    g_error_buffer.clear();

    // Validate output pointers
    if (!handles || !num_streams) {
        g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                           "Output handles or num_streams pointer is NULL");
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }
    *num_streams = 0;

    // Validate config path
    if (!config_path || config_path[0] == '\0') {
        g_error_buffer.set(tide::ErrorCode::ConfigFileNotFound, "c_api",
                           "Configuration path is NULL or empty");
        return tide::to_int(tide::ErrorCode::ConfigFileNotFound);
    }

    // Validate max_streams
    if (max_streams <= 0) {
        g_error_buffer.set(tide::ErrorCode::StreamIndexOutOfRange, "c_api",
                           "max_streams must be greater than 0");
        return tide::to_int(tide::ErrorCode::StreamIndexOutOfRange);
    }

    // Parse YAML configuration
    auto config_result = tide::config::parse_yaml(config_path);
    if (!config_result) {
        const auto& err = config_result.error();
        g_error_buffer.set(tide::from_int(err.code), "c_api", err.message);
        return err.code;
    }

    auto& tide_config = config_result.value();

    // Resolve relative file paths against config file's parent directory
    std::filesystem::path config_dir =
        std::filesystem::path(config_path).parent_path();
    for (auto& stream : tide_config.streams) {
        if (stream.file_path.is_relative()) {
            stream.file_path = config_dir / stream.file_path;
        }
    }

    // Check that we have streams configured
    if (tide_config.streams.empty()) {
        g_error_buffer.set(tide::ErrorCode::ConfigMissingField, "c_api",
                           "No streams configured in YAML file");
        return tide::to_int(tide::ErrorCode::ConfigMissingField);
    }

    // Check that stream count fits in caller's array
    const int stream_count = static_cast<int>(tide_config.streams.size());
    if (stream_count > max_streams) {
        g_error_buffer.set(tide::ErrorCode::StreamIndexOutOfRange, "c_api",
                           "Configuration has " + std::to_string(stream_count) +
                           " streams but max_streams is " +
                           std::to_string(max_streams));
        return tide::to_int(tide::ErrorCode::StreamIndexOutOfRange);
    }

    // Convert Fortran-compatible integer communicator to MPI_Comm
    MPI_Comm comm = MPI_Comm_f2c(mpi_comm);

    // Check memory budget before creating streams (fail fast)
    if (tide_config.memory_budget_mb > 0) {
        tide::MemoryBudget budget(tide_config.memory_budget_mb);
        for (std::size_t i = 0; i < tide_config.streams.size(); ++i) {
            const auto& sc = tide_config.streams[i];
            auto tgt_it = tide_config.target_grids.find(sc.target_grid_ref);
            if (tgt_it != tide_config.target_grids.end()) {
                tide::StreamBufferReq req{};
                req.stream_name = sc.name;
                req.tgt_cols = tgt_it->second.num_cols;
                req.tgt_levels = tgt_it->second.num_levels > 0
                                     ? tgt_it->second.num_levels : 1;
                req.src_cells = tgt_it->second.num_cols;
                req.src_levels = tgt_it->second.num_levels > 0
                                     ? tgt_it->second.num_levels : 1;
                budget.add_stream(req);
            }
        }
        auto budget_check = budget.check_budget();
        if (!budget_check) {
            const auto& err = budget_check.error();
            g_error_buffer.set(tide::from_int(err.code), "c_api", err.message);
            return err.code;
        }
    }

    // Initialize output handles array to NULL
    for (int i = 0; i < max_streams; ++i) {
        handles[i] = nullptr;
    }

    // Create one StreamHandle per configured stream.
    // Each handle owns its own Stream independently, providing error
    // isolation: a failure in one stream does not affect others.
    int initialized_count = 0;

    for (int i = 0; i < stream_count; ++i) {
        const auto& stream_config =
            tide_config.streams[static_cast<std::size_t>(i)];

        // Look up the target grid for this stream
        auto tgt_it = tide_config.target_grids.find(stream_config.target_grid_ref);
        if (tgt_it == tide_config.target_grids.end()) {
            // Clean up already-allocated handles
            for (int j = 0; j < initialized_count; ++j) {
                tide_finalize(handles[j]);
                handles[j] = nullptr;
            }
            g_error_buffer.set(tide::ErrorCode::ConfigMissingField, "c_api",
                               "Target grid '" + stream_config.target_grid_ref +
                               "' not found in configuration (stream '" +
                               stream_config.name + "')");
            return tide::to_int(tide::ErrorCode::ConfigMissingField);
        }

        // Create the C++ Stream object
        auto stream_result = tide::Stream::create(
            stream_config, tgt_it->second, comm, tide_config.enable_timers);
        if (!stream_result) {
            // Clean up already-allocated handles
            for (int j = 0; j < initialized_count; ++j) {
                tide_finalize(handles[j]);
                handles[j] = nullptr;
            }
            const auto& err = stream_result.error();
            g_error_buffer.set(tide::from_int(err.code), "c_api",
                               "Failed to create stream '" +
                               stream_config.name + "': " + err.message);
            return err.code;
        }

        // Allocate the handle structure
        auto* sh = new (std::nothrow) StreamHandle{};
        if (!sh) {
            // Clean up already-allocated handles
            for (int j = 0; j < initialized_count; ++j) {
                tide_finalize(handles[j]);
                handles[j] = nullptr;
            }
            g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                               "Memory allocation failed for stream handle");
            return tide::to_int(tide::ErrorCode::UninitializedHandle);
        }

        sh->stream = std::make_unique<tide::Stream>(
            std::move(stream_result.value()));
        sh->config = tide_config;
        sh->field_name = stream_config.field_name;
        sh->finalized = false;
        sh->timers = tide::perf::StageTimers(tide_config.enable_timers);

        // Compute buffer memory usage for this stream
        const auto& tgt = tgt_it->second;
        tide::StreamBufferReq req{};
        req.src_cells = tgt.num_cols;
        req.src_levels = tgt.num_levels > 0 ? tgt.num_levels : 1;
        req.tgt_cols = tgt.num_cols;
        req.tgt_levels = tgt.num_levels > 0 ? tgt.num_levels : 1;
        sh->memory_bytes = tide::MemoryBudget::compute_stream_bytes(req);

        handles[i] = static_cast<tide_handle_t>(sh);
        initialized_count++;
    }

    *num_streams = initialized_count;
    return 0;
}

int tide_advance_all(tide_handle_t* handles, int num_streams,
                     double target_time, int* status_codes) {
    // Validate inputs
    if (!handles || !status_codes || num_streams <= 0) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    int any_failed = 0;

    // Advance each stream independently — error isolation ensures that
    // a failure in stream N does not affect stream M (M ≠ N).
    for (int i = 0; i < num_streams; ++i) {
        auto* sh = validate_handle(handles[i]);
        if (!sh) {
            status_codes[i] = tide::to_int(tide::ErrorCode::UninitializedHandle);
            any_failed = 1;
            continue;
        }

        // If this handle already has a recorded error (stream previously
        // failed), report the previous error code and skip advancement.
        if (sh->error_buffer.has_error()) {
            status_codes[i] = sh->error_buffer.code_int();
            any_failed = 1;
            continue;
        }

        int rc = sh->stream->advance(target_time);
        status_codes[i] = rc;

        if (rc != 0) {
            any_failed = 1;
            const char* msg = sh->stream->last_error();
            sh->error_buffer.set(tide::from_int(rc), "advance_all",
                                 msg ? msg : "Pipeline advance failed");
        }
    }

    return any_failed;
}

int tide_finalize_all(tide_handle_t* handles, int num_streams) {
    if (!handles || num_streams <= 0) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    for (int i = 0; i < num_streams; ++i) {
        if (handles[i]) {
            auto* sh = static_cast<StreamHandle*>(handles[i]);
            if (!sh->finalized) {
                if (sh->stream) {
                    sh->stream->finalize();
                    sh->stream.reset();
                }
                sh->finalized = true;
            }
            delete sh;  // Safe: we null the handle below
            handles[i] = nullptr;
        }
    }

    return 0;
}

int tide_get_stream_count(const char* config_path, int* count) {
    // Clear global error state
    g_error_buffer.clear();

    // Validate output pointer
    if (!count) {
        g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                           "Output count pointer is NULL");
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }
    *count = 0;

    // Validate config path
    if (!config_path || config_path[0] == '\0') {
        g_error_buffer.set(tide::ErrorCode::ConfigFileNotFound, "c_api",
                           "Configuration path is NULL or empty");
        return tide::to_int(tide::ErrorCode::ConfigFileNotFound);
    }

    // Parse YAML configuration (just to count streams)
    auto config_result = tide::config::parse_yaml(config_path);
    if (!config_result) {
        const auto& err = config_result.error();
        g_error_buffer.set(tide::from_int(err.code), "c_api", err.message);
        return err.code;
    }

    *count = static_cast<int>(config_result.value().streams.size());
    return 0;
}

int tide_get_stream_status(tide_handle_t handle) {
    if (!handle) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    auto* sh = static_cast<StreamHandle*>(handle);

    if (sh->finalized) {
        return tide::to_int(tide::ErrorCode::AlreadyFinalized);
    }

    // If the handle has an error recorded, return that error code
    if (sh->error_buffer.has_error()) {
        return sh->error_buffer.code_int();
    }

    // Stream is healthy
    return 0;
}

int tide_reset_stream_status(tide_handle_t handle) {
    auto* sh = validate_handle(handle);
    if (!sh) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }
    sh->error_buffer.clear();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Memory and Timer C API Implementation
// Validates Requirements: 6.3, 7.2, 7.3
// ─────────────────────────────────────────────────────────────────────────────

size_t tide_get_memory_usage(tide_handle_t* handles, int num_streams) {
    if (!handles || num_streams <= 0) {
        return 0;
    }

    std::size_t total = 0;
    for (int i = 0; i < num_streams; ++i) {
        if (!handles[i]) {
            continue;
        }
        auto* sh = static_cast<StreamHandle*>(handles[i]);
        if (sh->finalized) {
            continue;
        }
        total += sh->memory_bytes;
    }

    return total;
}

int tide_get_timers(tide_handle_t handle, double timers[5]) {
    auto* sh = validate_handle(handle);
    if (!sh) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    if (!timers) {
        sh->error_buffer.set(tide::ErrorCode::FieldNotFound, "c_api",
                             "Output timers array must not be NULL");
        return tide::to_int(tide::ErrorCode::FieldNotFound);
    }

    // Delegate to the Stream's internal per-stage timers (wired in advance())
    sh->stream->get_timers().get_all(timers);
    return 0;
}

int tide_reset_timers(tide_handle_t handle) {
    auto* sh = validate_handle(handle);
    if (!sh) {
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    // Delegate to the Stream's internal per-stage timers
    sh->stream->get_timers().reset();
    return 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// Weight Export C API Implementation
// Validates Requirements: 9.1, 9.4
// ─────────────────────────────────────────────────────────────────────────────

int tide_export_weights(tide_handle_t handle, const char* output_path) {
    auto* sh = validate_handle(handle);
    if (!sh) {
        g_error_buffer.set(tide::ErrorCode::UninitializedHandle, "c_api",
                           "Handle is NULL or not initialized");
        return tide::to_int(tide::ErrorCode::UninitializedHandle);
    }

    sh->error_buffer.clear();

    // Validate output path
    if (!output_path || output_path[0] == '\0') {
        sh->error_buffer.set(tide::ErrorCode::WeightFileNotFound, "c_api",
                             "Output path is NULL or empty");
        return tide::to_int(tide::ErrorCode::WeightFileNotFound);
    }

    // Delegate to the Stream's export_weights method
    int rc = sh->stream->export_weights(std::filesystem::path(output_path));
    if (rc != 0) {
        // Copy the stream's error message to the handle error buffer
        const char* msg = sh->stream->last_error();
        sh->error_buffer.set(tide::from_int(rc), "export_weights",
                             msg ? msg : "Weight export failed");
    }

    return rc;
}

} // extern "C"
