/**
 * @file io.cpp
 * @brief TIDE AMIO I/O Stage implementation.
 *
 * Implements the AmioReader class which wraps the AMIO C99 API for
 * MPI-parallel reading of NetCDF/GRIB2 forcing files. Handles file
 * lifecycle, grid metadata extraction, and time-level data retrieval.
 *
 * The implementation uses the AMIO public C API:
 * - amio_init() / amio_finalize() for runtime lifecycle
 * - amio_open_dataset() / amio_close_dataset() for file access
 * - amio_read() / amio_view_data() / amio_release_view() for data reads
 *
 * Grid metadata is extracted by reading coordinate variables and
 * inspecting CF/COARDS/UGRID convention attributes.
 *
 * Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7
 */

#include "tide/io.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include <amio/amio.h>

#include <netcdf.h>

#include "tide/error.hpp"
#include "tide/types.hpp"

namespace tide::io {

// ─────────────────────────────────────────────────────────────────────────────
// Implementation (PImpl)
// ─────────────────────────────────────────────────────────────────────────────

/**
 * @brief Private implementation of AmioReader.
 *
 * Holds AMIO handles, cached metadata, and the MPI communicator.
 * All AMIO-specific types are confined here and never leak through
 * the public header.
 */
struct AmioReader::Impl {
    /// @brief AMIO core runtime handle.
    amio_core_handle core = nullptr;

    /// @brief AMIO dataset handle for the open file.
    amio_dataset_handle dataset = nullptr;

    /// @brief MPI communicator used for parallel I/O.
    MPI_Comm comm = MPI_COMM_NULL;

    /// @brief Path to the open file (for error reporting).
    std::filesystem::path file_path;

    /// @brief Cached time coordinate values from the file.
    std::vector<double> time_values;

    /// @brief Cached source grid metadata.
    SourceGrid source_grid;

    /// @brief Whether source grid has been extracted.
    bool grid_extracted = false;

    /// @brief Whether the dataset is currently open.
    bool is_open = false;

    /// @brief Whether we're using direct NetCDF-C access (fallback mode).
    bool use_direct_netcdf = false;

    /// @brief Number of time levels in the file.
    std::size_t num_time_levels = 0;

    /// @brief Path to the generated AMIO manifest (temporary).
    std::string manifest_path;

    /// @brief Path to the generated dataset config (temporary).
    std::string dataset_config_path;
};

// ─────────────────────────────────────────────────────────────────────────────
// Construction / destruction / move
// ─────────────────────────────────────────────────────────────────────────────

AmioReader::AmioReader() : impl_(std::make_unique<Impl>()) {}

AmioReader::~AmioReader() {
    if (impl_ && impl_->is_open) {
        close();
    }
}

AmioReader::AmioReader(AmioReader&&) noexcept = default;
AmioReader& AmioReader::operator=(AmioReader&&) noexcept = default;

// ─────────────────────────────────────────────────────────────────────────────
// Helper: detect file format from extension
// ─────────────────────────────────────────────────────────────────────────────

namespace {

/**
 * @brief Determine the AMIO backend name from a file extension.
 * @param path File path to inspect.
 * @return "netcdf4" for .nc files, "grib2" for .grib2/.grb2, empty for unknown.
 */
[[nodiscard]] std::string detect_backend(const std::filesystem::path& path) {
    const auto ext = path.extension().string();
    if (ext == ".nc" || ext == ".nc4" || ext == ".cdf") {
        return "netcdf4";
    }
    if (ext == ".grib2" || ext == ".grb2" || ext == ".grib") {
        return "grib2";
    }
    return {};
}

/**
 * @brief Generate a minimal AMIO manifest YAML string for reading.
 *
 * AMIO requires a manifest for initialization. We generate a minimal
 * one in-memory that configures the staging pool and worker threads
 * for read-only operation.
 *
 * @param backend Backend name ("netcdf4" or "grib2").
 * @return YAML manifest content string.
 */
[[nodiscard]] std::string generate_manifest_yaml(const std::string& backend) {
    return "staging_pool:\n"
           "  buffer_count: 4\n"
           "  buffer_capacity_bytes: 104857600\n"
           "worker_pool:\n"
           "  threads: 2\n"
           "prefetch:\n"
           "  depth: 2\n"
           "  read_timeout_s: 60\n"
           "staging_timeout_ms: 30000\n"
           "backend: " + backend + "\n";
}

/**
 * @brief Generate a minimal AMIO dataset config YAML string.
 *
 * @param path File path for the dataset.
 * @param backend Backend name.
 * @return YAML dataset config content string.
 */
[[nodiscard]] std::string generate_dataset_config_yaml(
    const std::filesystem::path& path,
    const std::string& backend) {
    return "backend: " + backend + "\n"
           "path: " + path.string() + "\n"
           "data_model: classic\n"
           "parallel: false\n";
}

/**
 * @brief Write a string to a temporary file and return the path.
 * @param content Content to write.
 * @param suffix File suffix (e.g., "_manifest.yaml").
 * @return Path to the created temporary file.
 */
[[nodiscard]] std::string write_temp_yaml(const std::string& content,
                                          const std::string& suffix) {
    auto tmp_dir = std::filesystem::temp_directory_path();
    auto tmp_path = tmp_dir / ("amio_tide" + suffix);
    // Write the file
    auto* fp = std::fopen(tmp_path.c_str(), "w");
    if (fp) {
        std::fwrite(content.data(), 1, content.size(), fp);
        std::fclose(fp);
    }
    return tmp_path.string();
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// open()
// ─────────────────────────────────────────────────────────────────────────────

auto AmioReader::open(const std::filesystem::path& path, MPI_Comm comm) -> int {
    // Validate file existence
    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || ec) {
        return to_int(ErrorCode::FileNotFound);
    }

    // Check readability (regular file check)
    if (!std::filesystem::is_regular_file(path, ec) || ec) {
        return to_int(ErrorCode::FileUnreadable);
    }

    // Detect backend from file extension
    const auto backend = detect_backend(path);
    if (backend.empty()) {
        return to_int(ErrorCode::UnsupportedFormat);
    }

    // Store state
    impl_->file_path = path;
    impl_->comm = comm;

    // Check MPI comm size — use serial mode for single-rank execution
    int comm_size = 1;
    MPI_Comm_size(comm, &comm_size);
    const bool use_serial = (comm_size == 1);

    // Generate AMIO manifest and dataset config files
    const auto manifest_content = generate_manifest_yaml(backend);
    impl_->manifest_path = write_temp_yaml(manifest_content, "_manifest.yaml");

    auto dataset_content = generate_dataset_config_yaml(path, backend);
    if (use_serial) {
        dataset_content += "io_mode: serial\n";
    }
    impl_->dataset_config_path = write_temp_yaml(dataset_content, "_dataset.yaml");

    // Initialize AMIO core — wrapped in try-catch because AMIO/eckit
    // may throw exceptions (e.g., when parallel NetCDF is unavailable).
    try {
        amio_status_t rc = amio_init(impl_->manifest_path.c_str(), &impl_->core);
        if (rc != AMIO_OK) {
            impl_->core = nullptr;
            impl_->dataset = nullptr;
            impl_->is_open = true;
            impl_->use_direct_netcdf = true;
            return to_int(ErrorCode::Success);
        }

        // Open the dataset for reading
        rc = amio_open_dataset(impl_->core, impl_->dataset_config_path.c_str(),
                               AMIO_MODE_READ, &impl_->dataset);
        if (rc != AMIO_OK) {
            amio_finalize(impl_->core);
            impl_->core = nullptr;
            impl_->dataset = nullptr;
            impl_->is_open = true;
            impl_->use_direct_netcdf = true;
            return to_int(ErrorCode::Success);
        }
    } catch (...) {
        // AMIO/eckit threw an exception — fall back to direct NetCDF access
        if (impl_->core) {
            amio_finalize(impl_->core);
            impl_->core = nullptr;
        }
        impl_->dataset = nullptr;
        impl_->is_open = true;
        impl_->use_direct_netcdf = true;
        return to_int(ErrorCode::Success);
    }

    impl_->is_open = true;
    impl_->use_direct_netcdf = false;
    return to_int(ErrorCode::Success);
}

// ─────────────────────────────────────────────────────────────────────────────
// read_time_level()
// ─────────────────────────────────────────────────────────────────────────────

auto AmioReader::read_time_level(std::string_view field_name,
                                 std::size_t time_index,
                                 std::span<double> buffer) -> int {
    if (!impl_->is_open) {
        return to_int(ErrorCode::FileNotFound);
    }

    // Validate time index against known time levels
    if (!impl_->time_values.empty() && time_index >= impl_->time_values.size()) {
        return to_int(ErrorCode::TimeOutOfRange);
    }

    // ── Direct NetCDF fallback mode ──────────────────────────────────────
    if (impl_->use_direct_netcdf) {
        int ncid = -1;
        int nc_rc = nc_open(impl_->file_path.c_str(), NC_NOWRITE, &ncid);
        if (nc_rc != NC_NOERR) {
            return to_int(ErrorCode::FileUnreadable);
        }

        const std::string var_name(field_name);
        int varid = -1;
        nc_rc = nc_inq_varid(ncid, var_name.c_str(), &varid);
        if (nc_rc != NC_NOERR) {
            nc_close(ncid);
            return to_int(ErrorCode::MpiIoError);
        }

        // Get variable dimensions to compute slice
        int ndims = 0;
        nc_inq_varndims(ncid, varid, &ndims);

        std::vector<int> dimids(ndims);
        nc_inq_vardimid(ncid, varid, dimids.data());

        std::vector<std::size_t> start(ndims, 0);
        std::vector<std::size_t> count(ndims);

        for (int d = 0; d < ndims; ++d) {
            std::size_t dimlen = 0;
            nc_inq_dimlen(ncid, dimids[d], &dimlen);
            count[d] = dimlen;
        }

        // First dimension is assumed to be time — select the requested index
        if (ndims > 0) {
            start[0] = time_index;
            count[0] = 1;
        }

        // Compute total elements for this slice
        std::size_t total = 1;
        for (int d = 0; d < ndims; ++d) {
            total *= count[d];
        }

        const std::size_t copy_count = std::min(total, buffer.size());
        std::vector<double> temp(total);
        nc_rc = nc_get_vara_double(ncid, varid, start.data(), count.data(), temp.data());
        nc_close(ncid);

        if (nc_rc != NC_NOERR) {
            return to_int(ErrorCode::MpiIoError);
        }

        std::memcpy(buffer.data(), temp.data(), copy_count * sizeof(double));
        return to_int(ErrorCode::Success);
    }

    // ── AMIO read path ───────────────────────────────────────────────────
    // Use AMIO read API to fetch the time level
    const std::string var_name(field_name);
    amio_view_handle view = nullptr;

    amio_status_t rc = amio_read(impl_->dataset, var_name.c_str(),
                                 static_cast<int64_t>(time_index),
                                 nullptr, &view);
    if (rc == AMIO_OK && view != nullptr) {
        // Retrieve data from the view
        const void* data = nullptr;
        std::size_t nbytes = 0;
        rc = amio_view_data(view, &data, &nbytes);
        if (rc == AMIO_OK && data != nullptr && nbytes > 0) {
            // Copy data into the output buffer
            const std::size_t num_elements = nbytes / sizeof(double);
            const std::size_t copy_count = std::min(num_elements, buffer.size());
            std::memcpy(buffer.data(), data, copy_count * sizeof(double));
            amio_release_view(view);
            return to_int(ErrorCode::Success);
        }
        amio_release_view(view);
    } else if (view) {
        amio_release_view(view);
    }

    // ── AMIO read failed — fall back to direct NetCDF-C ──────────────────
    {
        int ncid = -1;
        int nc_rc = nc_open(impl_->file_path.c_str(), NC_NOWRITE, &ncid);
        if (nc_rc != NC_NOERR) {
            return to_int(ErrorCode::FileUnreadable);
        }

        int varid = -1;
        nc_rc = nc_inq_varid(ncid, var_name.c_str(), &varid);
        if (nc_rc != NC_NOERR) {
            nc_close(ncid);
            return to_int(ErrorCode::MpiIoError);
        }

        int ndims = 0;
        nc_inq_varndims(ncid, varid, &ndims);

        std::vector<int> dimids(ndims);
        nc_inq_vardimid(ncid, varid, dimids.data());

        std::vector<std::size_t> start(ndims, 0);
        std::vector<std::size_t> count(ndims);

        for (int d = 0; d < ndims; ++d) {
            std::size_t dimlen = 0;
            nc_inq_dimlen(ncid, dimids[d], &dimlen);
            count[d] = dimlen;
        }

        if (ndims > 0) {
            start[0] = time_index;
            count[0] = 1;
        }

        std::size_t total = 1;
        for (int d = 0; d < ndims; ++d) {
            total *= count[d];
        }

        const std::size_t copy_count = std::min(total, buffer.size());
        std::vector<double> temp(total);
        nc_rc = nc_get_vara_double(ncid, varid, start.data(), count.data(), temp.data());
        nc_close(ncid);

        if (nc_rc != NC_NOERR) {
            return to_int(ErrorCode::MpiIoError);
        }

        std::memcpy(buffer.data(), temp.data(), copy_count * sizeof(double));
        return to_int(ErrorCode::Success);
    }
}

// ─────────────────────────────────────────────────────────────────────────────
// get_source_grid()
// ─────────────────────────────────────────────────────────────────────────────

auto AmioReader::get_source_grid() const -> std::expected<SourceGrid, Error> {
    if (!impl_->is_open) {
        return std::unexpected(Error{
            .code = to_int(ErrorCode::FileNotFound),
            .message = "No file is open for reading",
            .context = "io"
        });
    }

    // If we've already extracted the grid, return the cached version
    if (impl_->grid_extracted) {
        return impl_->source_grid;
    }

    // Attempt to read coordinate variables from the file to detect
    // the grid convention and extract metadata.
    //
    // Strategy:
    // 1. Try reading "lat" and "lon" variables (CF/COARDS convention)
    // 2. Try reading "latitude" / "longitude" (alternate CF naming)
    // 3. Try reading UGRID mesh topology variables
    // 4. If none found, return MissingGridMetadata error

    SourceGrid grid{};

    // ── Direct NetCDF fallback: read coordinates directly via NetCDF-C ──
    if (impl_->use_direct_netcdf || impl_->dataset == nullptr) {
        int ncid = -1;
        int nc_rc = nc_open(impl_->file_path.c_str(), NC_NOWRITE, &ncid);
        if (nc_rc != NC_NOERR) {
            return std::unexpected(Error{
                .code = to_int(ErrorCode::MissingGridMetadata),
                .message = "Cannot open file for grid extraction: " +
                           impl_->file_path.string(),
                .context = "io"
            });
        }

        // Try to read "lat" coordinate variable
        int lat_varid = -1;
        nc_rc = nc_inq_varid(ncid, "lat", &lat_varid);
        if (nc_rc != NC_NOERR) {
            nc_rc = nc_inq_varid(ncid, "latitude", &lat_varid);
        }

        int lon_varid = -1;
        int lon_rc = nc_inq_varid(ncid, "lon", &lon_varid);
        if (lon_rc != NC_NOERR) {
            lon_rc = nc_inq_varid(ncid, "longitude", &lon_varid);
        }

        if (nc_rc == NC_NOERR && lon_rc == NC_NOERR) {
            // Get lat dimension length
            int lat_ndims = 0;
            nc_inq_varndims(ncid, lat_varid, &lat_ndims);
            if (lat_ndims >= 1) {
                int lat_dimid = -1;
                nc_inq_vardimid(ncid, lat_varid, &lat_dimid);
                std::size_t nlats = 0;
                nc_inq_dimlen(ncid, lat_dimid, &nlats);
                grid.lats.resize(nlats);
                nc_get_var_double(ncid, lat_varid, grid.lats.data());
            }

            // Get lon dimension length
            int lon_ndims = 0;
            nc_inq_varndims(ncid, lon_varid, &lon_ndims);
            if (lon_ndims >= 1) {
                int lon_dimid = -1;
                nc_inq_vardimid(ncid, lon_varid, &lon_dimid);
                std::size_t nlons = 0;
                nc_inq_dimlen(ncid, lon_dimid, &nlons);
                grid.lons.resize(nlons);
                nc_get_var_double(ncid, lon_varid, grid.lons.data());
            }

            if (!grid.lats.empty() && !grid.lons.empty()) {
                grid.type = SourceGrid::Type::RegularLatLon;
                grid.num_cells = grid.lats.size() * grid.lons.size();
                grid.metadata_convention = "CF";

                // Try to read vertical levels
                int lev_varid = -1;
                int lev_rc2 = nc_inq_varid(ncid, "lev", &lev_varid);
                if (lev_rc2 != NC_NOERR)
                    lev_rc2 = nc_inq_varid(ncid, "level", &lev_varid);
                if (lev_rc2 != NC_NOERR)
                    lev_rc2 = nc_inq_varid(ncid, "plev", &lev_varid);

                if (lev_rc2 == NC_NOERR) {
                    int lev_dimid = -1;
                    nc_inq_vardimid(ncid, lev_varid, &lev_dimid);
                    std::size_t nlevs = 0;
                    nc_inq_dimlen(ncid, lev_dimid, &nlevs);
                    grid.levels.resize(nlevs);
                    nc_get_var_double(ncid, lev_varid, grid.levels.data());
                }

                // Try to detect level units from the "units" attribute
                char units_buf[64] = {};
                if (lev_varid >= 0 &&
                    nc_get_att_text(ncid, lev_varid, "units", units_buf) == NC_NOERR) {
                    grid.level_units = units_buf;
                }

                nc_close(ncid);
                impl_->source_grid = grid;
                impl_->grid_extracted = true;
                return grid;
            }
        }

        nc_close(ncid);
        return std::unexpected(Error{
            .code = to_int(ErrorCode::MissingGridMetadata),
            .message = "File does not contain recognized grid metadata "
                       "(CF, COARDS, or UGRID): " + impl_->file_path.string(),
            .context = "io"
        });
    }

    // ── AMIO path: read coordinate variables via AMIO API ────────────────
    // Attempt CF/COARDS detection: look for 1D lat/lon coordinate arrays
    // by trying to read standard coordinate variable names.
    amio_view_handle lat_view = nullptr;
    amio_status_t rc = amio_read(impl_->dataset, "lat", 0, nullptr, &lat_view);

    if (rc != AMIO_OK) {
        // Try alternate name "latitude"
        rc = amio_read(impl_->dataset, "latitude", 0, nullptr, &lat_view);
    }

    if (rc == AMIO_OK && lat_view != nullptr) {
        // Successfully found latitude coordinate variable
        const void* lat_data = nullptr;
        std::size_t lat_nbytes = 0;
        amio_view_data(lat_view, &lat_data, &lat_nbytes);

        if (lat_data != nullptr && lat_nbytes > 0) {
            const std::size_t nlats = lat_nbytes / sizeof(double);
            grid.lats.resize(nlats);
            std::memcpy(grid.lats.data(), lat_data, nlats * sizeof(double));
        }
        amio_release_view(lat_view);

        // Read longitude
        amio_view_handle lon_view = nullptr;
        rc = amio_read(impl_->dataset, "lon", 0, nullptr, &lon_view);
        if (rc != AMIO_OK) {
            rc = amio_read(impl_->dataset, "longitude", 0, nullptr, &lon_view);
        }

        if (rc == AMIO_OK && lon_view != nullptr) {
            const void* lon_data = nullptr;
            std::size_t lon_nbytes = 0;
            amio_view_data(lon_view, &lon_data, &lon_nbytes);

            if (lon_data != nullptr && lon_nbytes > 0) {
                const std::size_t nlons = lon_nbytes / sizeof(double);
                grid.lons.resize(nlons);
                std::memcpy(grid.lons.data(), lon_data, nlons * sizeof(double));
            }
            amio_release_view(lon_view);
        }

        // Determine grid type from dimensionality
        if (!grid.lats.empty() && !grid.lons.empty()) {
            // 1D lat/lon → RegularLatLon (CF/COARDS)
            grid.type = SourceGrid::Type::RegularLatLon;
            grid.num_cells = grid.lats.size() * grid.lons.size();
            grid.metadata_convention = "CF";

            // Try to read vertical levels
            amio_view_handle lev_view = nullptr;
            rc = amio_read(impl_->dataset, "lev", 0, nullptr, &lev_view);
            if (rc != AMIO_OK) {
                rc = amio_read(impl_->dataset, "level", 0, nullptr, &lev_view);
            }
            if (rc != AMIO_OK) {
                rc = amio_read(impl_->dataset, "plev", 0, nullptr, &lev_view);
            }

            if (rc == AMIO_OK && lev_view != nullptr) {
                const void* lev_data = nullptr;
                std::size_t lev_nbytes = 0;
                amio_view_data(lev_view, &lev_data, &lev_nbytes);
                if (lev_data != nullptr && lev_nbytes > 0) {
                    const std::size_t nlevs = lev_nbytes / sizeof(double);
                    grid.levels.resize(nlevs);
                    std::memcpy(grid.levels.data(), lev_data,
                                nlevs * sizeof(double));
                }
                amio_release_view(lev_view);
            }

            // Try to read lat_bounds / lon_bounds for conservative regridding
            amio_view_handle lat_bnds_view = nullptr;
            rc = amio_read(impl_->dataset, "lat_bnds", 0, nullptr,
                           &lat_bnds_view);
            if (rc != AMIO_OK) {
                rc = amio_read(impl_->dataset, "lat_bounds", 0, nullptr,
                               &lat_bnds_view);
            }
            if (rc == AMIO_OK && lat_bnds_view != nullptr) {
                const void* bnds_data = nullptr;
                std::size_t bnds_nbytes = 0;
                amio_view_data(lat_bnds_view, &bnds_data, &bnds_nbytes);
                if (bnds_data != nullptr && bnds_nbytes > 0) {
                    const std::size_t n = bnds_nbytes / sizeof(double);
                    grid.lat_bounds.resize(n);
                    std::memcpy(grid.lat_bounds.data(), bnds_data,
                                n * sizeof(double));
                }
                amio_release_view(lat_bnds_view);
            }

            amio_view_handle lon_bnds_view = nullptr;
            rc = amio_read(impl_->dataset, "lon_bnds", 0, nullptr,
                           &lon_bnds_view);
            if (rc != AMIO_OK) {
                rc = amio_read(impl_->dataset, "lon_bounds", 0, nullptr,
                               &lon_bnds_view);
            }
            if (rc == AMIO_OK && lon_bnds_view != nullptr) {
                const void* bnds_data = nullptr;
                std::size_t bnds_nbytes = 0;
                amio_view_data(lon_bnds_view, &bnds_data, &bnds_nbytes);
                if (bnds_data != nullptr && bnds_nbytes > 0) {
                    const std::size_t n = bnds_nbytes / sizeof(double);
                    grid.lon_bounds.resize(n);
                    std::memcpy(grid.lon_bounds.data(), bnds_data,
                                n * sizeof(double));
                }
                amio_release_view(lon_bnds_view);
            }

            impl_->source_grid = grid;
            impl_->grid_extracted = true;
            return grid;
        }
    }

    // Try UGRID convention: look for mesh topology variable
    amio_view_handle mesh_view = nullptr;
    rc = amio_read(impl_->dataset, "mesh_topology", 0, nullptr, &mesh_view);
    if (rc == AMIO_OK && mesh_view != nullptr) {
        amio_release_view(mesh_view);

        // UGRID detected — read node coordinates
        amio_view_handle node_x_view = nullptr;
        rc = amio_read(impl_->dataset, "node_x", 0, nullptr, &node_x_view);
        if (rc == AMIO_OK && node_x_view != nullptr) {
            const void* x_data = nullptr;
            std::size_t x_nbytes = 0;
            amio_view_data(node_x_view, &x_data, &x_nbytes);
            if (x_data && x_nbytes > 0) {
                const std::size_t n = x_nbytes / sizeof(double);
                grid.lons.resize(n);
                std::memcpy(grid.lons.data(), x_data, n * sizeof(double));
            }
            amio_release_view(node_x_view);
        }

        amio_view_handle node_y_view = nullptr;
        rc = amio_read(impl_->dataset, "node_y", 0, nullptr, &node_y_view);
        if (rc == AMIO_OK && node_y_view != nullptr) {
            const void* y_data = nullptr;
            std::size_t y_nbytes = 0;
            amio_view_data(node_y_view, &y_data, &y_nbytes);
            if (y_data && y_nbytes > 0) {
                const std::size_t n = y_nbytes / sizeof(double);
                grid.lats.resize(n);
                std::memcpy(grid.lats.data(), y_data, n * sizeof(double));
            }
            amio_release_view(node_y_view);
        }

        grid.type = SourceGrid::Type::Unstructured;
        grid.num_cells = grid.lats.size();
        grid.metadata_convention = "UGRID";

        impl_->source_grid = grid;
        impl_->grid_extracted = true;
        return grid;
    }

    // No recognized grid metadata found via AMIO — try direct NetCDF-C
    // as a final fallback. This handles the case where AMIO opened the
    // file successfully but cannot read coordinate variables.
    {
        SourceGrid grid{};
        int ncid = -1;
        int nc_rc = nc_open(impl_->file_path.c_str(), NC_NOWRITE, &ncid);
        if (nc_rc == NC_NOERR) {
            // Read lat coordinate
            int varid = -1;
            nc_rc = nc_inq_varid(ncid, "lat", &varid);
            if (nc_rc != NC_NOERR) {
                nc_rc = nc_inq_varid(ncid, "latitude", &varid);
            }
            if (nc_rc == NC_NOERR) {
                int dimid = -1;
                nc_inq_vardimid(ncid, varid, &dimid);
                std::size_t dimlen = 0;
                nc_inq_dimlen(ncid, dimid, &dimlen);
                grid.lats.resize(dimlen);
                nc_get_var_double(ncid, varid, grid.lats.data());
            }

            // Read lon coordinate
            nc_rc = nc_inq_varid(ncid, "lon", &varid);
            if (nc_rc != NC_NOERR) {
                nc_rc = nc_inq_varid(ncid, "longitude", &varid);
            }
            if (nc_rc == NC_NOERR) {
                int dimid = -1;
                nc_inq_vardimid(ncid, varid, &dimid);
                std::size_t dimlen = 0;
                nc_inq_dimlen(ncid, dimid, &dimlen);
                grid.lons.resize(dimlen);
                nc_get_var_double(ncid, varid, grid.lons.data());
            }

            // Read level coordinate
            nc_rc = nc_inq_varid(ncid, "level", &varid);
            if (nc_rc != NC_NOERR) {
                nc_rc = nc_inq_varid(ncid, "lev", &varid);
            }
            if (nc_rc == NC_NOERR) {
                int dimid = -1;
                nc_inq_vardimid(ncid, varid, &dimid);
                std::size_t dimlen = 0;
                nc_inq_dimlen(ncid, dimid, &dimlen);
                grid.levels.resize(dimlen);
                nc_get_var_double(ncid, varid, grid.levels.data());
                grid.level_units = "Pa";
            }

            nc_close(ncid);

            if (!grid.lats.empty() && !grid.lons.empty()) {
                grid.type = SourceGrid::Type::RegularLatLon;
                grid.num_cells = grid.lats.size() * grid.lons.size();
                grid.metadata_convention = "CF";
                impl_->source_grid = grid;
                impl_->grid_extracted = true;
                return grid;
            }
        }
    }

    return std::unexpected(Error{
        .code = to_int(ErrorCode::MissingGridMetadata),
        .message = "File does not contain recognized grid metadata "
                   "(CF, COARDS, or UGRID): " + impl_->file_path.string(),
        .context = "io"
    });
}

// ─────────────────────────────────────────────────────────────────────────────
// get_time_values()
// ─────────────────────────────────────────────────────────────────────────────

auto AmioReader::get_time_values() const -> std::vector<double> {
    if (!impl_->is_open) {
        return {};
    }

    // Return cached values if already read
    if (!impl_->time_values.empty()) {
        return impl_->time_values;
    }

    // Attempt to read the "time" coordinate variable via AMIO
    if (impl_->dataset != nullptr) {
        amio_view_handle time_view = nullptr;
        amio_status_t rc = amio_read(impl_->dataset, "time", 0, nullptr, &time_view);
        if (rc != AMIO_OK) {
            // Try alternate name "Time"
            rc = amio_read(impl_->dataset, "Time", 0, nullptr, &time_view);
        }

        if (rc == AMIO_OK && time_view != nullptr) {
            const void* time_data = nullptr;
            std::size_t time_nbytes = 0;
            amio_view_data(time_view, &time_data, &time_nbytes);

            if (time_data != nullptr && time_nbytes > 0) {
                const std::size_t ntimes = time_nbytes / sizeof(double);
                impl_->time_values.resize(ntimes);
                std::memcpy(impl_->time_values.data(), time_data,
                            ntimes * sizeof(double));
                impl_->num_time_levels = ntimes;
            }
            amio_release_view(time_view);
        }
    }

    // Fallback: if AMIO didn't return time values, read directly via NetCDF-C.
    // This handles the case where AMIO treats the record-dimension variable
    // differently and cannot return its values through amio_read(), or when
    // we are in direct NetCDF fallback mode (AMIO init/open failed).
    if (impl_->time_values.empty() && !impl_->file_path.empty()) {
        int ncid = -1;
        int nc_rc = nc_open(impl_->file_path.c_str(), NC_NOWRITE, &ncid);
        if (nc_rc == NC_NOERR) {
            int time_dimid = -1;
            nc_rc = nc_inq_dimid(ncid, "time", &time_dimid);
            if (nc_rc != NC_NOERR) {
                nc_rc = nc_inq_dimid(ncid, "Time", &time_dimid);
            }

            std::size_t ntimes = 0;
            if (nc_rc == NC_NOERR) {
                nc_inq_dimlen(ncid, time_dimid, &ntimes);
            }

            if (ntimes > 0) {
                int time_varid = -1;
                nc_rc = nc_inq_varid(ncid, "time", &time_varid);
                if (nc_rc != NC_NOERR) {
                    nc_rc = nc_inq_varid(ncid, "Time", &time_varid);
                }
                if (nc_rc == NC_NOERR) {
                    impl_->time_values.resize(ntimes);
                    nc_get_var_double(ncid, time_varid,
                                      impl_->time_values.data());
                    impl_->num_time_levels = ntimes;
                }
            }
            nc_close(ncid);
        }
    }

    return impl_->time_values;
}

// ─────────────────────────────────────────────────────────────────────────────
// close()
// ─────────────────────────────────────────────────────────────────────────────

void AmioReader::close() {
    if (!impl_->is_open) {
        return;
    }

    // Close the dataset
    if (impl_->dataset != nullptr) {
        amio_close_dataset(impl_->dataset);
        impl_->dataset = nullptr;
    }

    // Finalize the AMIO core
    if (impl_->core != nullptr) {
        amio_finalize(impl_->core);
        impl_->core = nullptr;
    }

    // Clean up temporary config files
    if (!impl_->manifest_path.empty()) {
        std::filesystem::remove(impl_->manifest_path);
        impl_->manifest_path.clear();
    }
    if (!impl_->dataset_config_path.empty()) {
        std::filesystem::remove(impl_->dataset_config_path);
        impl_->dataset_config_path.clear();
    }

    impl_->is_open = false;
    impl_->grid_extracted = false;
    impl_->time_values.clear();
    impl_->num_time_levels = 0;
}

// ─────────────────────────────────────────────────────────────────────────────
// is_open()
// ─────────────────────────────────────────────────────────────────────────────

bool AmioReader::is_open() const noexcept {
    return impl_ && impl_->is_open;
}

} // namespace tide::io
