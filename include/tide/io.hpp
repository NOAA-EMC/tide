/**
 * @file io.hpp
 * @brief TIDE AMIO I/O Stage — MPI-parallel file reading wrapper.
 *
 * Provides the AmioReader class which wraps the AMIO C99 API to deliver
 * MPI-parallel reading of NetCDF and GRIB2 forcing files. The reader
 * handles file lifecycle, grid metadata extraction from CF-compliant,
 * COARDS, or UGRID conventions, and time-level data retrieval.
 *
 * The implementation uses the PImpl pattern to isolate AMIO internals
 * from downstream consumers.
 *
 * @section supported_formats Supported File Formats
 * - NetCDF-4 (parallel HDF5 / MPI-IO via AMIO NetCDF backend)
 * - GRIB2 (via AMIO GRIB2 backend / nceplibs-g2c)
 *
 * @section grid_conventions Grid Metadata Conventions
 * - CF-compliant (Climate and Forecast conventions)
 * - COARDS (Cooperative Ocean/Atmosphere Research Data Service)
 * - UGRID (Unstructured Grid conventions)
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 1 (ErrorCode::FileNotFound): File path is invalid or does not exist
 * - 2 (ErrorCode::FileUnreadable): File exists but cannot be read
 * - 3 (ErrorCode::UnsupportedFormat): File format not supported
 * - 4 (ErrorCode::MissingGridMetadata): No recognized grid metadata found
 * - 5 (ErrorCode::MpiIoError): MPI I/O initialization or read failure
 * - 200 (ErrorCode::TimeOutOfRange): Requested time outside file range
 *
 * Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7
 */

#ifndef TIDE_IO_HPP
#define TIDE_IO_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

#include <mpi.h>

#include "tide/error.hpp"
#include "tide/types.hpp"

namespace tide::io {

/**
 * @brief MPI-parallel file reader wrapping the AMIO C99 API.
 *
 * AmioReader provides a high-level C++ interface over AMIO's asynchronous
 * I/O engine for reading multidimensional scientific data from NetCDF and
 * GRIB2 files. It handles:
 *
 * - Opening files with MPI-parallel decomposition
 * - Extracting source grid metadata from CF/COARDS/UGRID conventions
 * - Reading individual time levels into caller-provided buffers
 * - Querying available time coordinate values
 *
 * The class uses the PImpl idiom to hide all AMIO internals from the
 * public header, ensuring no AMIO types leak into downstream code.
 *
 * @note Thread safety: each AmioReader instance is independent. Concurrent
 *       reads on different instances are safe without synchronization.
 *       Concurrent access to the same instance is undefined behavior.
 *
 * @par Example Usage
 * @code
 * tide::io::AmioReader reader;
 * int rc = reader.open("/data/forcing.nc", MPI_COMM_WORLD);
 * if (rc != 0) { return rc; }
 *
 * auto grid = reader.get_source_grid();
 * if (!grid) { return -1; }
 *
 * auto times = reader.get_time_values();
 * std::vector<double> buffer(grid->num_cells);
 * rc = reader.read_time_level("temperature", 0, buffer);
 *
 * reader.close();
 * @endcode
 */
class AmioReader {
public:
    /**
     * @brief Construct an uninitialized AmioReader.
     *
     * The reader must be opened via open() before any data access.
     */
    AmioReader();

    /**
     * @brief Destructor — closes any open file and releases resources.
     */
    ~AmioReader();

    /// @brief Move constructor.
    AmioReader(AmioReader&&) noexcept;

    /// @brief Move assignment operator.
    AmioReader& operator=(AmioReader&&) noexcept;

    // Non-copyable
    AmioReader(const AmioReader&) = delete;
    AmioReader& operator=(const AmioReader&) = delete;

    /**
     * @brief Open a file for MPI-parallel reading.
     *
     * Initializes the AMIO runtime and opens the specified file using
     * the appropriate backend (NetCDF-4 or GRIB2) with MPI-parallel
     * decomposition across all ranks in the supplied communicator.
     *
     * @param path File path (NetCDF .nc or GRIB2 .grib2/.grb2)
     * @param comm MPI communicator for parallel I/O decomposition
     * @return 0 on success, or one of:
     *   - ErrorCode::FileNotFound (1) if path does not exist
     *   - ErrorCode::FileUnreadable (2) if file cannot be read
     *   - ErrorCode::UnsupportedFormat (3) if format is not recognized
     *   - ErrorCode::MpiIoError (5) if MPI/AMIO initialization fails
     */
    auto open(const std::filesystem::path& path, MPI_Comm comm) -> int;

    /**
     * @brief Read a specific time level into the provided buffer.
     *
     * Reads the field data for the given variable at the specified time
     * index from the open file. The data is read in parallel across MPI
     * ranks according to the decomposition established during open().
     *
     * @param field_name Variable name in the file (e.g., "temperature")
     * @param time_index Zero-based index of the time level to read
     * @param buffer Output buffer — must be pre-allocated with sufficient
     *               space for the local partition of the field data
     * @return 0 on success, or one of:
     *   - ErrorCode::FileNotFound (1) if no file is open
     *   - ErrorCode::TimeOutOfRange (200) if time_index exceeds available levels
     *   - ErrorCode::MpiIoError (5) if the read operation fails
     */
    auto read_time_level(std::string_view field_name,
                         std::size_t time_index,
                         std::span<double> buffer) -> int;

    /**
     * @brief Extract source grid metadata from the open file.
     *
     * Inspects the file's coordinate variables and attributes to detect
     * the grid convention (CF, COARDS, or UGRID) and constructs a
     * SourceGrid containing topology type, coordinate arrays, vertical
     * levels, and cell bounds where available.
     *
     * @return SourceGrid on success, or Error on failure:
     *   - ErrorCode::FileNotFound (1) if no file is open
     *   - ErrorCode::MissingGridMetadata (4) if no CF/COARDS/UGRID metadata found
     */
    [[nodiscard]] auto get_source_grid() const -> std::expected<SourceGrid, Error>;

    /**
     * @brief Query available time values in the file.
     *
     * Returns the time coordinate values for all time levels in the file.
     * Values are typically in seconds since epoch or day-of-year depending
     * on the file's time unit metadata.
     *
     * @return Vector of time values (empty if no file is open or no time
     *         dimension exists)
     */
    [[nodiscard]] auto get_time_values() const -> std::vector<double>;

    /**
     * @brief Close the file and release all AMIO resources.
     *
     * After this call, the reader is in a closed state. It may be
     * reopened with a subsequent call to open().
     */
    void close();

    /**
     * @brief Check whether a file is currently open.
     * @return true if a file has been successfully opened and not yet closed.
     */
    [[nodiscard]] bool is_open() const noexcept;

private:
    /// @brief Forward-declared implementation (PImpl).
    struct Impl;

    /// @brief Pointer to the implementation.
    std::unique_ptr<Impl> impl_;
};

} // namespace tide::io

#endif // TIDE_IO_HPP
