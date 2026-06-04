/**
 * @file test_scrip_roundtrip.cpp
 * @brief Integration test for SCRIP weight file round-trip property.
 *
 * Validates that loading a SCRIP weight file, exporting it, and reloading
 * the exported file produces bitwise identical results when applied to any
 * source field.
 *
 * Test flow:
 *   1. Load SCRIP weights from scrip_conserve_weights.nc
 *   2. Apply to a known source field → result_1
 *   3. Export loaded CSR matrix to a temporary file via write_scrip_weights()
 *   4. Reload the exported file via read_scrip_weights()
 *   5. Apply the reloaded matrix to the same source field → result_2
 *   6. Verify result_1 and result_2 are bitwise identical
 *
 * Validates: Requirements 9.3, 9.6
 */

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <tide/scrip.hpp>

namespace {

/// @brief Path to the test data directory (set by CMake compile definition).
const std::filesystem::path kTestDataDir{TIDE_TEST_DATA_DIR};

/// @brief Generate a deterministic source field with non-trivial values.
/// Uses a simple analytical function to produce distinct values per cell.
std::vector<double> generate_source_field(std::size_t n_src) {
    std::vector<double> field(n_src);
    for (std::size_t i = 0; i < n_src; ++i) {
        // Linear + sinusoidal pattern to avoid trivial constant fields
        field[i] = 100.0 + 3.5 * static_cast<double>(i)
                   - 0.02 * static_cast<double>(i * i);
    }
    return field;
}

/// @brief RAII helper for temporary file cleanup.
struct TempFile {
    std::filesystem::path path;

    explicit TempFile(const std::string& name)
        : path(std::filesystem::temp_directory_path() / name) {}

    ~TempFile() {
        std::error_code ec;
        std::filesystem::remove(path, ec);
    }

    TempFile(const TempFile&) = delete;
    TempFile& operator=(const TempFile&) = delete;
};

} // namespace

// =============================================================================
// Test: SCRIP round-trip with conservative weight file
// =============================================================================

TEST(ScripRoundTrip, ConservativeWeightsProduceBitwiseIdenticalResults) {
    // 1. Load SCRIP weights from the test data file
    const auto weight_file = kTestDataDir / "scrip_conserve_weights.nc";
    ASSERT_TRUE(std::filesystem::exists(weight_file))
        << "Test data file not found: " << weight_file;

    auto load_result = tide::scrip::read_scrip_weights(weight_file);
    ASSERT_TRUE(load_result.has_value())
        << "Failed to load SCRIP weights: code=" << load_result.error().code
        << " msg=" << load_result.error().message;

    const auto& original_matrix = load_result.value();
    ASSERT_GT(original_matrix.n_src, 0u);
    ASSERT_GT(original_matrix.n_dst, 0u);
    ASSERT_GT(original_matrix.n_s, 0u);

    // 2. Apply original matrix to a known source field
    auto source_field = generate_source_field(original_matrix.n_src);
    std::vector<double> result_original(original_matrix.n_dst, 0.0);

    int rc = tide::scrip::apply_csr(original_matrix, source_field, result_original);
    ASSERT_EQ(rc, 0) << "apply_csr failed on original matrix";

    // 3. Export the matrix to a temporary file
    TempFile tmp{"tide_scrip_roundtrip_test.nc"};
    rc = tide::scrip::write_scrip_weights(original_matrix, tmp.path);
    ASSERT_EQ(rc, 0) << "write_scrip_weights failed: rc=" << rc;
    ASSERT_TRUE(std::filesystem::exists(tmp.path))
        << "Exported file does not exist: " << tmp.path;

    // 4. Reload the exported file
    auto reload_result = tide::scrip::read_scrip_weights(tmp.path);
    ASSERT_TRUE(reload_result.has_value())
        << "Failed to reload exported SCRIP weights: code="
        << reload_result.error().code
        << " msg=" << reload_result.error().message;

    const auto& reloaded_matrix = reload_result.value();

    // Verify matrix metadata matches
    EXPECT_EQ(reloaded_matrix.n_src, original_matrix.n_src);
    EXPECT_EQ(reloaded_matrix.n_dst, original_matrix.n_dst);
    EXPECT_EQ(reloaded_matrix.n_s, original_matrix.n_s);

    // 5. Apply reloaded matrix to the same source field
    std::vector<double> result_reloaded(reloaded_matrix.n_dst, 0.0);
    rc = tide::scrip::apply_csr(reloaded_matrix, source_field, result_reloaded);
    ASSERT_EQ(rc, 0) << "apply_csr failed on reloaded matrix";

    // 6. Verify bitwise identical results
    ASSERT_EQ(result_original.size(), result_reloaded.size());
    for (std::size_t i = 0; i < result_original.size(); ++i) {
        EXPECT_EQ(result_original[i], result_reloaded[i])
            << "Mismatch at destination cell " << i
            << ": original=" << result_original[i]
            << " reloaded=" << result_reloaded[i];
    }
}

// =============================================================================
// Test: SCRIP round-trip preserves CSR structure exactly
// =============================================================================

TEST(ScripRoundTrip, RoundTripPreservesCsrStructure) {
    // Load original SCRIP weights
    const auto weight_file = kTestDataDir / "scrip_conserve_weights.nc";
    ASSERT_TRUE(std::filesystem::exists(weight_file));

    auto load_result = tide::scrip::read_scrip_weights(weight_file);
    ASSERT_TRUE(load_result.has_value());
    const auto& original = load_result.value();

    // Export and reload
    TempFile tmp{"tide_scrip_roundtrip_structure_test.nc"};
    int rc = tide::scrip::write_scrip_weights(original, tmp.path);
    ASSERT_EQ(rc, 0);

    auto reload_result = tide::scrip::read_scrip_weights(tmp.path);
    ASSERT_TRUE(reload_result.has_value());
    const auto& reloaded = reload_result.value();

    // Verify CSR arrays are identical
    ASSERT_EQ(original.values.size(), reloaded.values.size());
    ASSERT_EQ(original.col_indices.size(), reloaded.col_indices.size());
    ASSERT_EQ(original.row_pointers.size(), reloaded.row_pointers.size());

    for (std::size_t i = 0; i < original.values.size(); ++i) {
        EXPECT_EQ(original.values[i], reloaded.values[i])
            << "values mismatch at index " << i;
    }

    for (std::size_t i = 0; i < original.col_indices.size(); ++i) {
        EXPECT_EQ(original.col_indices[i], reloaded.col_indices[i])
            << "col_indices mismatch at index " << i;
    }

    for (std::size_t i = 0; i < original.row_pointers.size(); ++i) {
        EXPECT_EQ(original.row_pointers[i], reloaded.row_pointers[i])
            << "row_pointers mismatch at index " << i;
    }
}

// =============================================================================
// Test: Round-trip with bilinear weight file
// =============================================================================

TEST(ScripRoundTrip, BilinearWeightsProduceBitwiseIdenticalResults) {
    // Load SCRIP bilinear weights
    const auto weight_file = kTestDataDir / "scrip_bilinear_weights.nc";
    ASSERT_TRUE(std::filesystem::exists(weight_file))
        << "Test data file not found: " << weight_file;

    auto load_result = tide::scrip::read_scrip_weights(weight_file);
    ASSERT_TRUE(load_result.has_value())
        << "Failed to load SCRIP bilinear weights: code="
        << load_result.error().code
        << " msg=" << load_result.error().message;

    const auto& original_matrix = load_result.value();

    // Apply original matrix to a source field
    auto source_field = generate_source_field(original_matrix.n_src);
    std::vector<double> result_original(original_matrix.n_dst, 0.0);
    int rc = tide::scrip::apply_csr(original_matrix, source_field, result_original);
    ASSERT_EQ(rc, 0);

    // Export → reload
    TempFile tmp{"tide_scrip_roundtrip_bilinear_test.nc"};
    rc = tide::scrip::write_scrip_weights(original_matrix, tmp.path);
    ASSERT_EQ(rc, 0);

    auto reload_result = tide::scrip::read_scrip_weights(tmp.path);
    ASSERT_TRUE(reload_result.has_value());
    const auto& reloaded_matrix = reload_result.value();

    // Apply reloaded matrix to same source field
    std::vector<double> result_reloaded(reloaded_matrix.n_dst, 0.0);
    rc = tide::scrip::apply_csr(reloaded_matrix, source_field, result_reloaded);
    ASSERT_EQ(rc, 0);

    // Verify bitwise identical
    ASSERT_EQ(result_original.size(), result_reloaded.size());
    for (std::size_t i = 0; i < result_original.size(); ++i) {
        EXPECT_EQ(result_original[i], result_reloaded[i])
            << "Bilinear mismatch at destination cell " << i;
    }
}

// =============================================================================
// Test: Round-trip with multiple different source fields
// =============================================================================

TEST(ScripRoundTrip, MultipleFieldsAllBitwiseIdentical) {
    // Load weights once
    const auto weight_file = kTestDataDir / "scrip_conserve_weights.nc";
    auto load_result = tide::scrip::read_scrip_weights(weight_file);
    ASSERT_TRUE(load_result.has_value());
    const auto& original_matrix = load_result.value();

    // Export → reload
    TempFile tmp{"tide_scrip_roundtrip_multi_test.nc"};
    int rc = tide::scrip::write_scrip_weights(original_matrix, tmp.path);
    ASSERT_EQ(rc, 0);

    auto reload_result = tide::scrip::read_scrip_weights(tmp.path);
    ASSERT_TRUE(reload_result.has_value());
    const auto& reloaded_matrix = reload_result.value();

    // Test with several different source fields
    const std::size_t n_src = original_matrix.n_src;
    const std::size_t n_dst = original_matrix.n_dst;

    // Field 1: uniform value
    {
        std::vector<double> src(n_src, 42.0);
        std::vector<double> out_orig(n_dst, 0.0);
        std::vector<double> out_reload(n_dst, 0.0);

        tide::scrip::apply_csr(original_matrix, src, out_orig);
        tide::scrip::apply_csr(reloaded_matrix, src, out_reload);

        for (std::size_t i = 0; i < n_dst; ++i) {
            EXPECT_EQ(out_orig[i], out_reload[i])
                << "Uniform field mismatch at cell " << i;
        }
    }

    // Field 2: linearly increasing
    {
        std::vector<double> src(n_src);
        for (std::size_t i = 0; i < n_src; ++i) {
            src[i] = static_cast<double>(i) * 1.5;
        }
        std::vector<double> out_orig(n_dst, 0.0);
        std::vector<double> out_reload(n_dst, 0.0);

        tide::scrip::apply_csr(original_matrix, src, out_orig);
        tide::scrip::apply_csr(reloaded_matrix, src, out_reload);

        for (std::size_t i = 0; i < n_dst; ++i) {
            EXPECT_EQ(out_orig[i], out_reload[i])
                << "Linear field mismatch at cell " << i;
        }
    }

    // Field 3: alternating positive/negative
    {
        std::vector<double> src(n_src);
        for (std::size_t i = 0; i < n_src; ++i) {
            src[i] = (i % 2 == 0) ? 1000.0 : -500.0;
        }
        std::vector<double> out_orig(n_dst, 0.0);
        std::vector<double> out_reload(n_dst, 0.0);

        tide::scrip::apply_csr(original_matrix, src, out_orig);
        tide::scrip::apply_csr(reloaded_matrix, src, out_reload);

        for (std::size_t i = 0; i < n_dst; ++i) {
            EXPECT_EQ(out_orig[i], out_reload[i])
                << "Alternating field mismatch at cell " << i;
        }
    }
}
