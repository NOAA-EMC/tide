/**
 * @file scrip.hpp
 * @brief TIDE SCRIP Weight File Reader — load and apply pre-computed remapping weights.
 *
 * Provides functions to read SCRIP-format NetCDF weight files, validate
 * their dimensions against configured grids, and apply the sparse weight
 * matrix (CSR format) to source fields via matrix-vector multiplication.
 *
 * The SCRIP format stores sparse remapping weights as three parallel arrays:
 * - src_address(n_s): 1-based source cell indices
 * - dst_address(n_s): 1-based destination cell indices
 * - remap_matrix(n_s) or remap_matrix(n_s, num_wts): weight values
 *
 * On load, addresses are converted to 0-based and entries are sorted by
 * destination address to build a CSR (Compressed Sparse Row) matrix for
 * efficient repeated application.
 *
 * @section error_codes Error Codes
 * - 0: Success
 * - 800 (ErrorCode::WeightFileNotFound): File not found or unreadable
 * - 801 (ErrorCode::WeightFileDimensionMismatch): Grid dimension mismatch
 * - 802 (ErrorCode::WeightFileMissingVariable): Required variable missing
 *
 * Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8
 */

#ifndef TIDE_SCRIP_HPP
#define TIDE_SCRIP_HPP

#include <cstddef>
#include <expected>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include <tide/types.hpp>

namespace tide::scrip {

/**
 * @brief Sparse weight matrix in Compressed Sparse Row (CSR) format.
 *
 * Stores the sparse remapping matrix read from a SCRIP weight file.
 * The CSR layout enables efficient row-wise access for matrix-vector
 * multiplication (one row per destination cell).
 *
 * Memory layout:
 * - values[row_pointers[i] .. row_pointers[i+1]) are the weights for dest cell i
 * - col_indices[k] is the 0-based source cell index for values[k]
 */
struct CsrMatrix {
    /// @brief Non-zero weight values (length n_s).
    std::vector<double> values;

    /// @brief Source cell indices, 0-based (length n_s).
    std::vector<int> col_indices;

    /// @brief Row start offsets (length n_dst + 1).
    /// row_pointers[i] is the index into values/col_indices where row i begins.
    /// row_pointers[n_dst] == n_s.
    std::vector<int> row_pointers;

    /// @brief Number of source grid cells.
    std::size_t n_src{0};

    /// @brief Number of destination grid cells.
    std::size_t n_dst{0};

    /// @brief Number of sparse links (non-zero entries).
    std::size_t n_s{0};

    /// @brief Optional remap method metadata from the weight file.
    std::string remap_method;
};

/**
 * @brief Read a SCRIP weight file and produce a CSR matrix.
 *
 * Opens the NetCDF file at @p path, reads the src_address, dst_address,
 * and remap_matrix variables, converts 1-based Fortran addresses to
 * 0-based C indices, sorts entries by destination address, and builds
 * the CSR row_pointers array.
 *
 * If the file contains optional dimensions n_a (source cells) and n_b
 * (destination cells), they are stored in n_src and n_dst. Otherwise,
 * n_src and n_dst are inferred from max(src_address) and max(dst_address).
 *
 * @param path Path to the SCRIP NetCDF file.
 * @return CsrMatrix on success, Error on failure.
 *
 * @retval Error{800, ...} File not found or cannot be opened as NetCDF.
 * @retval Error{802, ...} Required variable (src_address, dst_address,
 *                          or remap_matrix) is missing from the file.
 */
[[nodiscard]] auto read_scrip_weights(const std::filesystem::path& path)
    -> std::expected<CsrMatrix, Error>;

/**
 * @brief Apply a CSR weight matrix to a source field (sparse mat-vec).
 *
 * For each destination cell i, computes:
 *   target[i] = sum_{k in [row_pointers[i], row_pointers[i+1])}
 *                   values[k] * source[col_indices[k]]
 *
 * @param matrix The CSR weight matrix.
 * @param source Source field values (must have at least n_src elements).
 * @param target Output field (must have at least n_dst elements, pre-allocated).
 * @return 0 on success.
 *
 * @pre source.size() >= matrix.n_src
 * @pre target.size() >= matrix.n_dst
 */
auto apply_csr(const CsrMatrix& matrix,
               std::span<const double> source,
               std::span<double> target) -> int;

/**
 * @brief Validate SCRIP matrix dimensions against expected grid sizes.
 *
 * Checks that max(src_address) <= expected_src and
 * max(dst_address) <= expected_dst (using the stored n_src and n_dst).
 *
 * @param matrix The loaded CSR matrix.
 * @param expected_src Expected number of source grid cells.
 * @param expected_dst Expected number of destination grid cells.
 * @return 0 on success, 801 (ErrorCode::WeightFileDimensionMismatch) on mismatch.
 */
auto validate_dimensions(const CsrMatrix& matrix,
                         std::size_t expected_src,
                         std::size_t expected_dst) -> int;

/**
 * @brief Export a CSR matrix to SCRIP NetCDF format.
 *
 * Creates a NetCDF-4 file containing the sparse weight matrix in SCRIP
 * convention with dimensions n_s, n_a, n_b, and variables src_address,
 * dst_address, and remap_matrix.
 *
 * Addresses are written as 1-based integers (SCRIP/Fortran convention).
 * The write order matches the CSR row traversal so that a round-trip
 * read → write → read produces an identical matrix.
 *
 * @param matrix The CSR weight matrix to export.
 * @param path Output NetCDF file path.
 * @return 0 on success, non-zero on I/O failure.
 */
auto write_scrip_weights(const CsrMatrix& matrix,
                         const std::filesystem::path& path) -> int;

} // namespace tide::scrip

#endif // TIDE_SCRIP_HPP
