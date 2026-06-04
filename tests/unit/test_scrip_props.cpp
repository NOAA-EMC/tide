/**
 * @file test_scrip_props.cpp
 * @brief Property-based tests for the TIDE SCRIP Weight File Reader.
 *
 * Feature: tide-production-readiness, Property 1: SCRIP weight matrix application equals matrix-vector product
 * Feature: tide-production-readiness, Property 2: SCRIP dimension validation accepts matching and rejects mismatching grids
 * Feature: tide-production-readiness, Property 11: SCRIP weight export round-trip
 *
 * Validates: Requirements 1.1, 1.2, 1.3, 1.4, 1.5, 9.3, 9.6
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include "tide/error.hpp"
#include "tide/scrip.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <numeric>
#include <vector>

namespace tide::scrip {
namespace {

// ─── Generators for Property 1 ──────────────────────────────────────────────

/**
 * @brief Generate a valid CSR matrix with random sparse entries.
 *
 * Generates a random n_src x n_dst sparse matrix with a random number of
 * non-zero entries, valid column indices (within [0, n_src)), and properly
 * formed row_pointers (monotonically non-decreasing, starting at 0,
 * ending at n_s).
 */
rc::Gen<CsrMatrix> genCsrMatrix() {
    return rc::gen::exec([]() {
        // Generate dimensions in [5, 100]
        auto n_src = *rc::gen::inRange<std::size_t>(5, 101);
        auto n_dst = *rc::gen::inRange<std::size_t>(5, 101);

        // Generate number of non-zero entries: between 0 and min(n_src*n_dst, 500)
        // Cap at 500 to keep tests fast while still exercising sparse structure
        std::size_t max_entries = std::min(n_src * n_dst, std::size_t{500});
        auto n_s = *rc::gen::inRange<std::size_t>(0, max_entries + 1);

        // Generate entries: for each entry, pick a random row and column
        // Then sort by row to build valid CSR
        struct Entry {
            std::size_t row;
            std::size_t col;
            double value;
        };

        std::vector<Entry> entries;
        entries.reserve(n_s);

        for (std::size_t i = 0; i < n_s; ++i) {
            auto row = *rc::gen::inRange<std::size_t>(0, n_dst);
            auto col = *rc::gen::inRange<std::size_t>(0, n_src);
            // Generate weights in a reasonable range to avoid numerical issues
            auto value = *rc::gen::map(rc::gen::inRange(-1000, 1001),
                                       [](int v) { return v * 0.001; });
            entries.push_back({row, col, value});
        }

        // Sort by row, then by column for canonical CSR ordering
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) {
                      if (a.row != b.row) return a.row < b.row;
                      return a.col < b.col;
                  });

        // Build CSR arrays
        CsrMatrix matrix;
        matrix.n_src = n_src;
        matrix.n_dst = n_dst;
        matrix.n_s = n_s;
        matrix.values.resize(n_s);
        matrix.col_indices.resize(n_s);
        matrix.row_pointers.resize(n_dst + 1, 0);

        for (std::size_t i = 0; i < n_s; ++i) {
            matrix.values[i] = entries[i].value;
            matrix.col_indices[i] = static_cast<int>(entries[i].col);
        }

        // Build row_pointers via counting sort
        for (std::size_t i = 0; i < n_s; ++i) {
            matrix.row_pointers[entries[i].row + 1]++;
        }
        for (std::size_t i = 1; i <= n_dst; ++i) {
            matrix.row_pointers[i] += matrix.row_pointers[i - 1];
        }

        return matrix;
    });
}

/**
 * @brief Generate a source field vector of a given size with random values.
 */
rc::Gen<std::vector<double>> genSourceField(std::size_t n_src) {
    return rc::gen::container<std::vector<double>>(
        n_src,
        rc::gen::map(rc::gen::inRange(-10000, 10001),
                     [](int v) { return v * 0.01; }));
}

/**
 * @brief Compute the expected output via naive row-by-row dot product.
 *
 * For each destination cell i, computes:
 *   expected[i] = sum_{k in [row_pointers[i], row_pointers[i+1])}
 *                     values[k] * source[col_indices[k]]
 */
std::vector<double> naiveMatVec(const CsrMatrix& matrix,
                                const std::vector<double>& source) {
    std::vector<double> result(matrix.n_dst, 0.0);
    for (std::size_t i = 0; i < matrix.n_dst; ++i) {
        double sum = 0.0;
        const int row_start = matrix.row_pointers[i];
        const int row_end = matrix.row_pointers[i + 1];
        for (int k = row_start; k < row_end; ++k) {
            sum += matrix.values[static_cast<std::size_t>(k)] *
                   source[static_cast<std::size_t>(
                       matrix.col_indices[static_cast<std::size_t>(k)])];
        }
        result[i] = sum;
    }
    return result;
}

// =============================================================================
// Property 1: SCRIP weight matrix application equals matrix-vector product
// =============================================================================

/**
 * **Validates: Requirements 1.1, 1.2, 1.3**
 *
 * For any valid CSR weight matrix W and any source field vector X of the
 * correct size, applying the SCRIP weights SHALL produce an output Y where
 * each element Y[i] equals the sum of W[i,j] × X[j] over all non-zero
 * entries in row i, to within machine epsilon.
 */
// Feature: tide-production-readiness, Property 1: SCRIP weight matrix application equals matrix-vector product
RC_GTEST_PROP(ScripProperty, P1_WeightMatrixApplicationEqualsMatVecProduct, ()) {
    // Generate a random valid CSR matrix
    auto matrix = *genCsrMatrix();

    // Generate a source field of the correct size
    auto source = *genSourceField(matrix.n_src);

    // Apply using the implementation under test
    std::vector<double> actual(matrix.n_dst, 0.0);
    int rc_code = apply_csr(matrix, source, actual);
    RC_ASSERT(rc_code == 0);

    // Compute expected output via naive row-by-row dot product
    auto expected = naiveMatVec(matrix, source);

    // Verify: each element should match to within machine epsilon
    // Use relative tolerance for non-zero values, absolute for near-zero
    for (std::size_t i = 0; i < matrix.n_dst; ++i) {
        double diff = std::abs(actual[i] - expected[i]);
        double magnitude = std::max(std::abs(expected[i]), std::abs(actual[i]));

        if (magnitude > 0.0) {
            // Relative error check — allow for accumulated floating-point error
            // proportional to the number of terms in the row
            int row_len = matrix.row_pointers[i + 1] - matrix.row_pointers[i];
            double eps = std::max(1.0, static_cast<double>(row_len)) *
                         std::numeric_limits<double>::epsilon() * magnitude;
            RC_ASSERT(diff <= eps);
        } else {
            // Both values are zero (or very close)
            RC_ASSERT(diff <= std::numeric_limits<double>::epsilon());
        }
    }
}

// =============================================================================
// Property 2: SCRIP dimension validation accepts matching and rejects
//             mismatching grids
// =============================================================================

/**
 * **Validates: Requirements 1.4, 1.5**
 *
 * For any SCRIP weight file with n_src source addresses and n_dst destination
 * addresses, and any configured source grid of size S and target grid of size T:
 * if n_src <= S and n_dst <= T, validation SHALL succeed (return 0); otherwise
 * validation SHALL return WeightFileDimensionMismatch (801).
 */
// Feature: tide-production-readiness, Property 2: SCRIP dimension validation accepts matching and rejects mismatching grids
RC_GTEST_PROP(ScripDimensionProperty, P2_ValidationAcceptsMatchingRejectsMismatching, ()) {
    // Generate matrix dimensions (n_src and n_dst in [1, 1000])
    auto n_src = *rc::gen::inRange<std::size_t>(1, 1001);
    auto n_dst = *rc::gen::inRange<std::size_t>(1, 1001);

    // Generate expected grid sizes (also [1, 1000])
    auto expected_src = *rc::gen::inRange<std::size_t>(1, 1001);
    auto expected_dst = *rc::gen::inRange<std::size_t>(1, 1001);

    // Construct a minimal CsrMatrix with the given dimensions
    CsrMatrix matrix;
    matrix.n_src = n_src;
    matrix.n_dst = n_dst;

    // Call validate_dimensions
    int rc_code = validate_dimensions(matrix, expected_src, expected_dst);

    if (n_src <= expected_src && n_dst <= expected_dst) {
        // Validation should succeed
        RC_ASSERT(rc_code == 0);
    } else {
        // Validation should return WeightFileDimensionMismatch (801)
        RC_ASSERT(rc_code == to_int(ErrorCode::WeightFileDimensionMismatch));
    }
}

/**
 * **Validates: Requirements 1.4, 1.5**
 *
 * When expected grid sizes exactly match the matrix dimensions, validation
 * SHALL always succeed.
 */
// Feature: tide-production-readiness, Property 2: SCRIP dimension validation accepts matching and rejects mismatching grids
RC_GTEST_PROP(ScripDimensionProperty, P2_ExactMatchAlwaysSucceeds, ()) {
    // Generate exact matching dimensions
    auto n_src = *rc::gen::inRange<std::size_t>(1, 1001);
    auto n_dst = *rc::gen::inRange<std::size_t>(1, 1001);

    CsrMatrix matrix;
    matrix.n_src = n_src;
    matrix.n_dst = n_dst;

    // With exact match, validation must succeed
    int rc_code = validate_dimensions(matrix, n_src, n_dst);
    RC_ASSERT(rc_code == 0);
}

/**
 * **Validates: Requirements 1.4, 1.5**
 *
 * When expected grid sizes are strictly less than matrix dimensions,
 * validation SHALL always fail with WeightFileDimensionMismatch.
 */
// Feature: tide-production-readiness, Property 2: SCRIP dimension validation accepts matching and rejects mismatching grids
RC_GTEST_PROP(ScripDimensionProperty, P2_SmallerExpectedAlwaysFails, ()) {
    // Generate matrix dimensions [2, 1000] so we can subtract 1 for expected
    auto n_src = *rc::gen::inRange<std::size_t>(2, 1001);
    auto n_dst = *rc::gen::inRange<std::size_t>(2, 1001);

    // Expected sizes are strictly less than matrix dimensions
    auto expected_src = *rc::gen::inRange<std::size_t>(1, n_src);
    auto expected_dst = *rc::gen::inRange<std::size_t>(1, n_dst);

    CsrMatrix matrix;
    matrix.n_src = n_src;
    matrix.n_dst = n_dst;

    int rc_code = validate_dimensions(matrix, expected_src, expected_dst);
    RC_ASSERT(rc_code == to_int(ErrorCode::WeightFileDimensionMismatch));
}

// =============================================================================
// Property 11: SCRIP weight export round-trip
// =============================================================================

/**
 * **Validates: Requirements 9.3, 9.6**
 *
 * For any valid weight matrix, exporting via write_scrip_weights and then
 * re-loading via read_scrip_weights SHALL produce a CSR matrix where applying
 * it to any source field X produces output bitwise identical to applying the
 * original matrix to X.
 */
// Feature: tide-production-readiness, Property 11: SCRIP weight export round-trip
RC_GTEST_PROP(ScripRoundTripProperty, P11_WeightExportRoundTrip, ()) {
    // 1. Generate a random valid CSR matrix with unique (row, col) pairs.
    //    Duplicate column indices within a row cause summation order differences
    //    after round-trip (since the file sorts by (dst, src)), which leads to
    //    floating-point non-associativity. A well-formed SCRIP matrix does not
    //    have duplicate entries for the same (src, dst) pair.
    auto matrix = *rc::gen::exec([]() {
        auto n_src = *rc::gen::inRange<std::size_t>(5, 51);
        auto n_dst = *rc::gen::inRange<std::size_t>(5, 51);

        // Generate a set of unique (row, col) pairs
        std::size_t max_possible = n_src * n_dst;
        std::size_t max_entries = std::min(max_possible, std::size_t{200});
        auto n_s = *rc::gen::inRange<std::size_t>(1, max_entries + 1);

        // Generate unique positions using a set-like approach
        struct Pos { std::size_t row; std::size_t col; };
        std::vector<Pos> all_positions;
        all_positions.reserve(max_possible);
        for (std::size_t r = 0; r < n_dst; ++r) {
            for (std::size_t c = 0; c < n_src; ++c) {
                all_positions.push_back({r, c});
            }
        }

        // Shuffle and take first n_s positions
        // Use RapidCheck to pick indices for reproducibility
        std::vector<Pos> chosen;
        chosen.reserve(n_s);
        std::vector<bool> used(max_possible, false);
        for (std::size_t i = 0; i < n_s; ++i) {
            // Pick a random unused index
            auto idx = *rc::gen::inRange<std::size_t>(0, max_possible);
            // Linear probe to find an unused position
            std::size_t start = idx;
            while (used[idx]) {
                idx = (idx + 1) % max_possible;
                if (idx == start) break; // all used (shouldn't happen given n_s <= max_possible)
            }
            if (used[idx]) break;
            used[idx] = true;
            chosen.push_back(all_positions[idx]);
        }
        n_s = chosen.size();

        // Assign random weight values
        struct Entry { std::size_t row; std::size_t col; double value; };
        std::vector<Entry> entries;
        entries.reserve(n_s);
        for (auto& pos : chosen) {
            auto value = *rc::gen::map(rc::gen::inRange(-1000, 1001),
                                       [](int v) { return v * 0.001; });
            entries.push_back({pos.row, pos.col, value});
        }

        // Sort by row, then by column for canonical CSR ordering
        std::sort(entries.begin(), entries.end(),
                  [](const Entry& a, const Entry& b) {
                      if (a.row != b.row) return a.row < b.row;
                      return a.col < b.col;
                  });

        // Build CSR arrays
        CsrMatrix m;
        m.n_src = n_src;
        m.n_dst = n_dst;
        m.n_s = n_s;
        m.values.resize(n_s);
        m.col_indices.resize(n_s);
        m.row_pointers.resize(n_dst + 1, 0);

        for (std::size_t i = 0; i < n_s; ++i) {
            m.values[i] = entries[i].value;
            m.col_indices[i] = static_cast<int>(entries[i].col);
        }

        // Build row_pointers
        for (std::size_t i = 0; i < n_s; ++i) {
            m.row_pointers[entries[i].row + 1]++;
        }
        for (std::size_t i = 1; i <= n_dst; ++i) {
            m.row_pointers[i] += m.row_pointers[i - 1];
        }

        return m;
    });

    // Skip degenerate cases
    RC_PRE(matrix.n_s > 0);
    RC_PRE(matrix.n_src > 0);
    RC_PRE(matrix.n_dst > 0);

    // 2. Write it to a temporary file
    auto tmp_path = std::filesystem::temp_directory_path() /
                    ("test_scrip_roundtrip_" + std::to_string(std::hash<std::size_t>{}(
                         matrix.n_s ^ (matrix.n_src << 16) ^ (matrix.n_dst << 32))) +
                     ".nc");

    int write_rc = write_scrip_weights(matrix, tmp_path);
    RC_ASSERT(write_rc == 0);

    // 3. Read it back using read_scrip_weights
    auto reload_result = read_scrip_weights(tmp_path);

    // Clean up temp file regardless of outcome
    std::filesystem::remove(tmp_path);

    RC_ASSERT(reload_result.has_value());
    auto& reloaded = reload_result.value();

    // 4. Generate a random source field
    auto source = *genSourceField(matrix.n_src);

    // 5. Apply the original matrix to the source field
    std::vector<double> output_original(matrix.n_dst, 0.0);
    int apply_rc1 = apply_csr(matrix, source, output_original);
    RC_ASSERT(apply_rc1 == 0);

    // 6. Apply the re-loaded matrix to the same source field
    std::vector<double> output_reloaded(reloaded.n_dst, 0.0);
    int apply_rc2 = apply_csr(reloaded, source, output_reloaded);
    RC_ASSERT(apply_rc2 == 0);

    // 7. Verify the results are bitwise identical
    RC_ASSERT(output_original.size() == output_reloaded.size());
    for (std::size_t i = 0; i < output_original.size(); ++i) {
        RC_ASSERT(output_original[i] == output_reloaded[i]);
    }
}

} // namespace
} // namespace tide::scrip
