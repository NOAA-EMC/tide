/**
 * @file tide.h
 * @brief TIDE (Time and Interpolation Data Engine) — C-interoperable API
 *
 * This header provides the public C API for the TIDE data forcing library.
 * All functions use opaque handles and integer return codes for compatibility
 * with C and Fortran (via ISO_C_BINDING) callers.
 *
 * @section lifecycle Lifecycle
 * 1. Call tide_init() with a YAML configuration path and MPI communicator
 * 2. Call tide_advance() to drive the pipeline to a target simulation time
 * 3. Call tide_get_field() to retrieve computed fields as raw pointers
 * 4. Call tide_finalize() to release all resources
 *
 * @section errors Error Handling
 * - All functions (except tide_get_error) return 0 on success and a non-zero
 *   error code on failure.
 * - Call tide_get_error() after a failed call to retrieve a human-readable
 *   error message.
 *
 * @section memory Memory Ownership
 * - TIDE retains ownership of all memory referenced by pointers returned
 *   through tide_get_field(). The caller SHALL NOT deallocate these pointers.
 * - Field data remains valid from the point of return until the next call to
 *   tide_advance(); after that, previously returned pointers are invalidated.
 */

#ifndef TIDE_H
#define TIDE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Opaque handle to a TIDE stream instance.
 *
 * Wraps internal C++ state. The caller treats this as an opaque pointer
 * and passes it to all subsequent TIDE API calls.
 */
typedef void* tide_handle_t;

/**
 * @brief Initialize TIDE from a YAML configuration file.
 *
 * Parses the configuration, initializes AMIO for MPI-parallel I/O,
 * builds Atlas regridding weights, and prepares internal buffers for
 * each configured forcing stream.
 *
 * @param[in]  config_path  Null-terminated path to YAML configuration file.
 * @param[in]  mpi_comm     MPI communicator as integer (Fortran-compatible
 *                          via MPI_Comm_c2f / MPI_Comm_f2c).
 * @param[out] handle       Receives the opaque stream handle on success.
 *                          Set to NULL on failure.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — handle is valid.
 * @retval 100 Configuration file not found.
 * @retval 101 YAML parse error.
 * @retval 102 Missing required configuration field.
 * @retval 1   I/O failure (file not found).
 * @retval 300 Atlas weight computation failure.
 *
 * @note On failure, no persistent resources are allocated.
 * @see tide_get_error() for detailed error messages.
 */
int tide_init(const char* config_path, int mpi_comm, tide_handle_t* handle);

/**
 * @brief Advance the pipeline to a target simulation time.
 *
 * Executes the full five-stage pipeline:
 * 1. Reads/rotates time levels via AMIO (if needed)
 * 2. Computes temporal interpolation to the target time
 * 3. Applies Atlas horizontal regridding
 * 4. Performs TSPACK vertical interpolation
 * 5. Applies scaling (Y = M*X + B)
 *
 * After a successful call, computed fields are available via tide_get_field().
 * Previously returned field pointers are invalidated.
 *
 * @param[in] handle       Stream handle from tide_init().
 * @param[in] target_time  Simulation time in seconds since epoch.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — output fields are ready.
 * @retval 600 Handle is NULL or not initialized.
 * @retval 200 Target time out of range for configured forcing files.
 * @retval 201 Forcing data time range exhausted.
 *
 * @pre handle was returned by a successful tide_init() call.
 * @post Output buffers contain interpolated fields at target_time.
 * @see tide_get_field()
 */
int tide_advance(tide_handle_t handle, double target_time);

/**
 * @brief Retrieve a computed field as a raw pointer with dimension metadata.
 *
 * Returns a pointer to the field data along with its rank and per-dimension
 * extents. The data is stored contiguously in column-major (Fortran) order
 * and remains valid until the next call to tide_advance().
 *
 * @param[in]  handle      Stream handle from tide_init().
 * @param[in]  field_name  Null-terminated name of the field to retrieve.
 * @param[out] data_ptr    Receives pointer to field data (owned by TIDE).
 *                         Set to NULL if field not found.
 * @param[out] rank        Receives the dimensionality of the field (1–7).
 * @param[out] extents     Array of at least 7 elements; filled with the size
 *                         of each dimension (unused dimensions set to 1).
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — data_ptr, rank, and extents are valid.
 * @retval 600 Handle is NULL or not initialized.
 * @retval 700 Field name not found or not yet computed.
 *
 * @warning The caller SHALL NOT deallocate the returned data pointer.
 *          TIDE retains ownership of the underlying memory.
 * @see tide_advance()
 */
int tide_get_field(tide_handle_t handle, const char* field_name,
                   double** data_ptr, int* rank, size_t extents[7]);

/**
 * @brief Release all resources associated with a stream handle.
 *
 * Closes file handles, deallocates internal buffers, and invalidates the
 * handle. After this call, the handle must not be used again.
 *
 * @param[in] handle  Stream handle to finalize.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — all resources released.
 * @retval 601 Handle is NULL, not initialized, or already finalized.
 *
 * @post The handle is invalidated and must not be reused.
 */
int tide_finalize(tide_handle_t handle);

/**
 * @brief Retrieve the last error message for a stream handle.
 *
 * Returns a pointer to a null-terminated string describing the most recent
 * error. The string remains valid until the next TIDE API call on the same
 * handle.
 *
 * @param[in] handle  Stream handle (or NULL for global initialization errors).
 *
 * @return Pointer to null-terminated error string. Returns an empty string
 *         ("") if no error has occurred.
 *
 * @note The returned string is owned by TIDE and must not be freed by the
 *       caller.
 */
const char* tide_get_error(tide_handle_t handle);

/* ═══════════════════════════════════════════════════════════════════════════
 * Multi-Stream Management API
 * ═══════════════════════════════════════════════════════════════════════════ */

/**
 * @brief Initialize all streams from a YAML configuration file.
 *
 * Parses the configuration, creates a StreamManager, initializes all
 * configured streams, and returns an array of per-stream handles.
 * Each handle can be used with tide_advance() and tide_get_field()
 * for individual stream operations.
 *
 * @param[in]  config_path  Null-terminated path to YAML configuration file.
 * @param[in]  mpi_comm     MPI communicator as integer (Fortran-compatible
 *                          via MPI_Comm_c2f / MPI_Comm_f2c).
 * @param[out] handles      Caller-allocated array of at least max_streams
 *                          elements. Receives one handle per stream on success.
 * @param[out] num_streams  Receives the number of streams actually initialized.
 * @param[in]  max_streams  Maximum number of handles the array can hold.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — num_streams handles are valid.
 * @retval 100 Configuration file not found.
 * @retval 101 YAML parse error.
 * @retval 102 Missing required configuration field.
 * @retval 900 Memory budget exceeded.
 * @retval 901 Number of streams exceeds max_streams capacity.
 *
 * @note On failure, no persistent resources are allocated (handles are NULL).
 * @see tide_advance_all(), tide_finalize_all()
 */
int tide_init_multi(const char* config_path, int mpi_comm,
                    tide_handle_t* handles, int* num_streams, int max_streams);

/**
 * @brief Advance all managed streams to a target simulation time.
 *
 * Iterates over the array of stream handles and calls the pipeline for
 * each one. Per-stream errors are isolated: a failure in one stream does
 * not prevent other streams from advancing.
 *
 * @param[in]  handles       Array of stream handles from tide_init_multi().
 * @param[in]  num_streams   Number of handles in the array.
 * @param[in]  target_time   Target simulation time in seconds since epoch.
 * @param[out] status_codes  Caller-allocated array of num_streams integers.
 *                           Receives 0 for success or non-zero error code
 *                           per stream.
 *
 * @return 0 if all streams succeeded, 1 if any stream failed.
 *
 * @pre All handles were returned by a successful tide_init_multi() call.
 * @post For each stream with status_codes[i]==0, output fields are ready.
 * @see tide_get_field(), tide_get_stream_status()
 */
int tide_advance_all(tide_handle_t* handles, int num_streams,
                     double target_time, int* status_codes);

/**
 * @brief Finalize all managed streams and release their resources.
 *
 * Calls tide_finalize() on each handle in the array, releasing all
 * internal buffers, file handles, and cached weights.
 *
 * @param[in] handles      Array of stream handles from tide_init_multi().
 * @param[in] num_streams  Number of handles in the array.
 *
 * @return 0 on success.
 *
 * @post All handles in the array are invalidated and must not be reused.
 * @see tide_init_multi()
 */
int tide_finalize_all(tide_handle_t* handles, int num_streams);

/**
 * @brief Get the number of streams defined in a configuration file.
 *
 * Parses the YAML configuration and returns the count of stream entries
 * without initializing any streams. Useful for allocating the handles
 * array before calling tide_init_multi().
 *
 * @param[in]  config_path  Null-terminated path to YAML configuration file.
 * @param[out] count        Receives the number of configured streams.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — count is valid.
 * @retval 100 Configuration file not found.
 * @retval 101 YAML parse error.
 */
int tide_get_stream_count(const char* config_path, int* count);

/**
 * @brief Get the health status of a specific stream.
 *
 * Returns the current status code for a stream handle. A return value
 * of 0 indicates the stream is healthy; a non-zero value is the error
 * code from the most recent failure.
 *
 * @param[in] handle  Stream handle from tide_init_multi() or tide_init().
 *
 * @return 0 if stream is healthy, non-zero error code if the stream has
 *         failed, or 600 if the handle is NULL/invalid.
 */
int tide_get_stream_status(tide_handle_t handle);

/**
 * @brief Reset a failed stream's status to allow retry.
 *
 * Clears the error state for a stream that previously failed during
 * tide_advance_all(), allowing it to participate in subsequent
 * tide_advance_all() calls.
 *
 * @param[in] handle  Stream handle to reset.
 * @return 0 on success, non-zero if handle is invalid.
 */
int tide_reset_stream_status(tide_handle_t handle);

/* ═══ Memory Management ═══ */

/**
 * @brief Get current memory usage across all streams (bytes).
 *
 * Computes the total buffer allocation for the given array of stream
 * handles using the standard formula:
 *   per_stream = 2 × src_cells × src_levels × 8 + tgt_cols × tgt_levels × 8 × 3
 *
 * @param[in] handles     Array of stream handles.
 * @param[in] num_streams Number of handles in the array.
 *
 * @return Total bytes allocated for pipeline buffers across all streams.
 *         Returns 0 if handles is NULL or num_streams <= 0.
 */
size_t tide_get_memory_usage(tide_handle_t* handles, int num_streams);

/* ═══ Performance Instrumentation ═══ */

/**
 * @brief Get cumulative per-stage timers for a stream.
 *
 * Retrieves the cumulative elapsed time (in seconds) for each pipeline
 * stage since initialization (or since the last timer reset).
 *
 * The 5-element array is indexed by stage:
 *   [0] I/O, [1] Temporal, [2] Spatial, [3] Vertical, [4] Scaling
 *
 * @param[in]  handle  Stream handle from tide_init().
 * @param[out] timers  Array of 5 doubles to receive cumulative stage times.
 *
 * @return 0 on success, non-zero on failure.
 * @retval 0   Success — timers array filled.
 * @retval 600 Handle is NULL or not initialized.
 */
int tide_get_timers(tide_handle_t handle, double timers[5]);

/**
 * @brief Reset cumulative timers for a stream to zero.
 *
 * Resets all per-stage cumulative timers for the given stream handle.
 * After this call, tide_get_timers() will return zeros until new
 * advance() calls accumulate time.
 *
 * @param[in] handle  Stream handle from tide_init().
 *
 * @return 0 on success, non-zero on failure.
 * @retval 0   Success — all timers reset.
 * @retval 600 Handle is NULL or not initialized.
 */
int tide_reset_timers(tide_handle_t handle);

/* ═══ Weight Export ═══ */

/**
 * @brief Export cached interpolation weights to a SCRIP NetCDF file.
 *
 * Writes the weight matrix (either loaded from a SCRIP file or computed
 * by Atlas) for the given stream to a NetCDF file in SCRIP format with
 * dimensions n_s and variables src_address, dst_address, remap_matrix.
 *
 * @param[in] handle       Stream handle (must have weights computed or loaded).
 * @param[in] output_path  Null-terminated path for the output SCRIP NetCDF file.
 *
 * @return 0 on success, non-zero error code on failure.
 * @retval 0   Success — weights written to output_path.
 * @retval 600 Handle is NULL or not initialized.
 * @retval 300 Weights not available (stream not advanced or no weights computed).
 *
 * @pre handle was returned by a successful tide_init() call.
 * @pre The stream must have weights available (either loaded from a SCRIP
 *      weight file at init, or computed by Atlas during initialization).
 *
 * Validates Requirements: 9.1, 9.4
 */
int tide_export_weights(tide_handle_t handle, const char* output_path);

#ifdef __cplusplus
}
#endif

#endif /* TIDE_H */
