/**
 * @file scrip.cpp
 * @brief Implementation of the TIDE SCRIP Weight File Reader.
 *
 * Reads SCRIP-format NetCDF weight files using the NetCDF-C API,
 * builds a CSR matrix from the sparse weight data, and provides
 * efficient matrix-vector multiplication for applying the weights.
 *
 * The NetCDF-C library is linked transitively via AMIO/HDF5 dependencies.
 *
 * Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8
 */

#include <tide/scrip.hpp>
#include <tide/error.hpp>

#include <algorithm>
#include <cstddef>
#include <numeric>
#include <string>
#include <vector>

#include <netcdf.h>

namespace tide::scrip {

namespace {

/// @brief Helper to check NetCDF return codes and map to TIDE errors.
struct NcGuard {
    int ncid{-1};

    ~NcGuard() {
        if (ncid >= 0) {
            nc_close(ncid);
        }
    }

    NcGuard() = default;
    NcGuard(const NcGuard&) = delete;
    NcGuard& operator=(const NcGuard&) = delete;
    NcGuard(NcGuard&& o) noexcept : ncid(o.ncid) { o.ncid = -1; }
    NcGuard& operator=(NcGuard&& o) noexcept {
        if (this != &o) {
            if (ncid >= 0) nc_close(ncid);
            ncid = o.ncid;
            o.ncid = -1;
        }
        return *this;
    }
};

} // anonymous namespace

auto read_scrip_weights(const std::filesystem::path& path)
    -> std::expected<CsrMatrix, Error> {

    // Open the NetCDF file
    NcGuard guard;
    int rc = nc_open(path.c_str(), NC_NOWRITE, &guard.ncid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileNotFound),
            std::string("Cannot open SCRIP weight file: ") + path.string() +
                " (" + nc_strerror(rc) + ")",
            "scrip"});
    }

    const int ncid = guard.ncid;

    // Read the n_s dimension (number of sparse links)
    int n_s_dimid{-1};
    rc = nc_inq_dimid(ncid, "n_s", &n_s_dimid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "SCRIP file missing dimension 'n_s': " + path.string(),
            "scrip"});
    }

    std::size_t n_s{0};
    rc = nc_inq_dimlen(ncid, n_s_dimid, &n_s);
    if (rc != NC_NOERR || n_s == 0) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "SCRIP file has invalid 'n_s' dimension: " + path.string(),
            "scrip"});
    }

    // Try to read optional dimensions n_a (source cells) and n_b (dest cells)
    std::size_t n_a{0};
    std::size_t n_b{0};
    int dim_id{-1};
    if (nc_inq_dimid(ncid, "n_a", &dim_id) == NC_NOERR) {
        nc_inq_dimlen(ncid, dim_id, &n_a);
    }
    if (nc_inq_dimid(ncid, "n_b", &dim_id) == NC_NOERR) {
        nc_inq_dimlen(ncid, dim_id, &n_b);
    }

    // Read src_address variable
    int varid{-1};
    rc = nc_inq_varid(ncid, "src_address", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "SCRIP file missing variable 'src_address': " + path.string(),
            "scrip"});
    }

    std::vector<int> src_address(n_s);
    rc = nc_get_var_int(ncid, varid, src_address.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "Failed to read 'src_address': " + std::string(nc_strerror(rc)),
            "scrip"});
    }

    // Read dst_address variable
    rc = nc_inq_varid(ncid, "dst_address", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "SCRIP file missing variable 'dst_address': " + path.string(),
            "scrip"});
    }

    std::vector<int> dst_address(n_s);
    rc = nc_get_var_int(ncid, varid, dst_address.data());
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "Failed to read 'dst_address': " + std::string(nc_strerror(rc)),
            "scrip"});
    }

    // Read remap_matrix variable
    rc = nc_inq_varid(ncid, "remap_matrix", &varid);
    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "SCRIP file missing variable 'remap_matrix': " + path.string(),
            "scrip"});
    }

    // Determine if remap_matrix is 1D (n_s) or 2D (n_s, num_wts)
    int ndims{0};
    nc_inq_varndims(ncid, varid, &ndims);

    std::vector<double> weights(n_s);
    if (ndims == 1) {
        // Simple 1D array
        rc = nc_get_var_double(ncid, varid, weights.data());
    } else {
        // 2D array (n_s, num_wts) — read only first weight column
        // Get the num_wts dimension size
        int dimids[2]{};
        nc_inq_vardimid(ncid, varid, dimids);
        std::size_t num_wts{1};
        nc_inq_dimlen(ncid, dimids[1], &num_wts);

        // Read the full 2D array and extract first column
        std::vector<double> full_weights(n_s * num_wts);
        rc = nc_get_var_double(ncid, varid, full_weights.data());
        if (rc == NC_NOERR) {
            for (std::size_t i = 0; i < n_s; ++i) {
                weights[i] = full_weights[i * num_wts];
            }
        }
    }

    if (rc != NC_NOERR) {
        return std::unexpected(Error{
            to_int(ErrorCode::WeightFileMissingVariable),
            "Failed to read 'remap_matrix': " + std::string(nc_strerror(rc)),
            "scrip"});
    }

    // Try to read optional remap_method global attribute
    std::string remap_method;
    {
        std::size_t att_len{0};
        if (nc_inq_attlen(ncid, NC_GLOBAL, "map_method", &att_len) == NC_NOERR) {
            std::vector<char> buf(att_len + 1, '\0');
            if (nc_get_att_text(ncid, NC_GLOBAL, "map_method", buf.data()) == NC_NOERR) {
                remap_method = std::string(buf.data(), att_len);
                // Trim trailing whitespace/nulls
                while (!remap_method.empty() &&
                       (remap_method.back() == '\0' || remap_method.back() == ' ')) {
                    remap_method.pop_back();
                }
            }
        }
    }

    // Determine n_src and n_dst from dimensions or max addresses
    std::size_t n_src = n_a;
    std::size_t n_dst = n_b;

    if (n_src == 0 && !src_address.empty()) {
        // Infer from max src_address (1-based)
        n_src = static_cast<std::size_t>(
            *std::max_element(src_address.begin(), src_address.end()));
    }
    if (n_dst == 0 && !dst_address.empty()) {
        // Infer from max dst_address (1-based)
        n_dst = static_cast<std::size_t>(
            *std::max_element(dst_address.begin(), dst_address.end()));
    }

    // Convert 1-based Fortran addresses to 0-based C indices
    // and build sort indices by destination address
    struct SparseEntry {
        int dst;  // 0-based destination
        int src;  // 0-based source
        double weight;
    };

    std::vector<SparseEntry> entries(n_s);
    for (std::size_t i = 0; i < n_s; ++i) {
        entries[i].dst = dst_address[i] - 1;  // 1-based -> 0-based
        entries[i].src = src_address[i] - 1;  // 1-based -> 0-based
        entries[i].weight = weights[i];
    }

    // Sort by destination address (row), then source address (column)
    std::sort(entries.begin(), entries.end(),
              [](const SparseEntry& a, const SparseEntry& b) {
                  if (a.dst != b.dst) return a.dst < b.dst;
                  return a.src < b.src;
              });

    // Build CSR arrays
    CsrMatrix matrix;
    matrix.n_src = n_src;
    matrix.n_dst = n_dst;
    matrix.n_s = n_s;
    matrix.remap_method = std::move(remap_method);
    matrix.values.resize(n_s);
    matrix.col_indices.resize(n_s);
    matrix.row_pointers.resize(n_dst + 1, 0);

    for (std::size_t i = 0; i < n_s; ++i) {
        matrix.values[i] = entries[i].weight;
        matrix.col_indices[i] = entries[i].src;
    }

    // Build row_pointers: count entries per row, then prefix sum
    for (std::size_t i = 0; i < n_s; ++i) {
        matrix.row_pointers[static_cast<std::size_t>(entries[i].dst) + 1]++;
    }
    for (std::size_t i = 1; i <= n_dst; ++i) {
        matrix.row_pointers[i] += matrix.row_pointers[i - 1];
    }

    return matrix;
}

auto apply_csr(const CsrMatrix& matrix,
               std::span<const double> source,
               std::span<double> target) -> int {

    // Bounds validation: ensure source and target buffers are large enough
    // to prevent out-of-bounds access from malformed SCRIP weight files.
    if (source.size() < matrix.n_src) {
        return to_int(ErrorCode::WeightFileDimensionMismatch);
    }
    if (target.size() < matrix.n_dst) {
        return to_int(ErrorCode::WeightFileDimensionMismatch);
    }

    const auto n_dst = matrix.n_dst;

    for (std::size_t i = 0; i < n_dst; ++i) {
        double sum = 0.0;
        const int row_start = matrix.row_pointers[i];
        const int row_end = matrix.row_pointers[i + 1];
        for (int k = row_start; k < row_end; ++k) {
            const auto col = static_cast<std::size_t>(matrix.col_indices[static_cast<std::size_t>(k)]);
            if (col >= source.size()) {
                return to_int(ErrorCode::WeightFileDimensionMismatch);
            }
            sum += matrix.values[static_cast<std::size_t>(k)] * source[col];
        }
        target[i] = sum;
    }

    return 0;
}

auto validate_dimensions(const CsrMatrix& matrix,
                         std::size_t expected_src,
                         std::size_t expected_dst) -> int {

    if (matrix.n_src > expected_src || matrix.n_dst > expected_dst) {
        return to_int(ErrorCode::WeightFileDimensionMismatch);
    }

    return 0;
}

auto write_scrip_weights(const CsrMatrix& matrix,
                         const std::filesystem::path& path) -> int {

    // RAII guard for the NetCDF file handle
    struct NcWriteGuard {
        int ncid{-1};
        ~NcWriteGuard() { if (ncid >= 0) nc_close(ncid); }
    } guard;

    int rc = nc_create(path.c_str(), NC_CLOBBER | NC_NETCDF4, &guard.ncid);
    if (rc != NC_NOERR) {
        return rc;
    }
    const int ncid = guard.ncid;

    // Define dimensions
    int n_s_dimid{-1};
    int n_a_dimid{-1};
    int n_b_dimid{-1};

    rc = nc_def_dim(ncid, "n_s", matrix.n_s, &n_s_dimid);
    if (rc != NC_NOERR) { return rc; }

    rc = nc_def_dim(ncid, "n_a", matrix.n_src, &n_a_dimid);
    if (rc != NC_NOERR) { return rc; }

    rc = nc_def_dim(ncid, "n_b", matrix.n_dst, &n_b_dimid);
    if (rc != NC_NOERR) { return rc; }

    // Define variables
    int src_varid{-1};
    int dst_varid{-1};
    int wgt_varid{-1};

    rc = nc_def_var(ncid, "src_address", NC_INT, 1, &n_s_dimid, &src_varid);
    if (rc != NC_NOERR) { return rc; }

    rc = nc_def_var(ncid, "dst_address", NC_INT, 1, &n_s_dimid, &dst_varid);
    if (rc != NC_NOERR) { return rc; }

    rc = nc_def_var(ncid, "remap_matrix", NC_DOUBLE, 1, &n_s_dimid, &wgt_varid);
    if (rc != NC_NOERR) { return rc; }

    // Write optional map_method global attribute
    if (!matrix.remap_method.empty()) {
        rc = nc_put_att_text(ncid, NC_GLOBAL, "map_method",
                             matrix.remap_method.size(),
                             matrix.remap_method.c_str());
        if (rc != NC_NOERR) { return rc; }
    }

    // End define mode
    rc = nc_enddef(ncid);
    if (rc != NC_NOERR) { return rc; }

    // Build src_address, dst_address, and remap_matrix arrays
    // Iterate in CSR row order: rows 0..n_dst-1, within each row iterate entries
    // dst_address[k] = row_index + 1 (1-based)
    // src_address[k] = col_indices[entry] + 1 (1-based)
    // remap_matrix[k] = values[entry]
    std::vector<int> src_address(matrix.n_s);
    std::vector<int> dst_address(matrix.n_s);

    std::size_t k = 0;
    for (std::size_t row = 0; row < matrix.n_dst; ++row) {
        const int row_start = matrix.row_pointers[row];
        const int row_end = matrix.row_pointers[row + 1];
        for (int entry = row_start; entry < row_end; ++entry) {
            dst_address[k] = static_cast<int>(row) + 1;          // 0-based → 1-based
            src_address[k] = matrix.col_indices[static_cast<std::size_t>(entry)] + 1; // 0-based → 1-based
            k++;
        }
    }

    // The values are already in CSR order (same as row_pointers traversal)
    // so we can write matrix.values directly as remap_matrix

    // Write variables
    rc = nc_put_var_int(ncid, src_varid, src_address.data());
    if (rc != NC_NOERR) { return rc; }

    rc = nc_put_var_int(ncid, dst_varid, dst_address.data());
    if (rc != NC_NOERR) { return rc; }

    rc = nc_put_var_double(ncid, wgt_varid, matrix.values.data());
    if (rc != NC_NOERR) { return rc; }

    // Guard will close the file on scope exit
    guard.ncid = -1;
    rc = nc_close(ncid);
    return rc;
}

} // namespace tide::scrip
