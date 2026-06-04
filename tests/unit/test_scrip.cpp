/**
 * @file test_scrip.cpp
 * @brief Unit tests for the TIDE SCRIP Weight File Reader.
 *
 * Tests reading SCRIP-format NetCDF weight files, CSR matrix construction,
 * sparse matrix-vector multiplication (apply_csr), and dimension validation.
 *
 * Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7, 1.8
 */

#include <tide/scrip.hpp>
#include <tide/error.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <numeric>
#include <span>
#include <vector>

namespace {

// Test data directory defined by CMake compile definition
#ifndef TIDE_TEST_DATA_DIR
#define TIDE_TEST_DATA_DIR "."
#endif

const std::filesystem::path kTestDataDir{TIDE_TEST_DATA_DIR};

} // anonymous namespace

// =============================================================================
// Tests for read_scrip_weights
// =============================================================================

TEST(ScripReader, ReadConservativeWeightFile) {
    auto path = kTestDataDir / "scrip_weights_conservative.nc";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Test data not generated: " << path;
    }

    auto result = tide::scrip::read_scrip_weights(path);
    ASSERT_TRUE(result.has_value()) << "Error: " << result.error().message;

    const auto& matrix = result.value();

    // Verify dimensions from the file (n_a=16, n_b=16)
    EXPECT_EQ(matrix.n_src, 16u);
    EXPECT_EQ(matrix.n_dst, 16u);

    // n_s should be 17 (16 identity entries, minus 1 replaced, plus 2 blend = 17)
    EXPECT_EQ(matrix.n_s, 17u);

    // CSR arrays should be properly sized
    EXPECT_EQ(matrix.values.size(), matrix.n_s);
    EXPECT_EQ(matrix.col_indices.size(), matrix.n_s);
    EXPECT_EQ(matrix.row_pointers.size(), matrix.n_dst + 1);

    // row_pointers should start at 0 and end at n_s
    EXPECT_EQ(matrix.row_pointers.front(), 0);
    EXPECT_EQ(matrix.row_pointers.back(), static_cast<int>(matrix.n_s));

    // row_pointers should be monotonically non-decreasing
    for (std::size_t i = 1; i < matrix.row_pointers.size(); ++i) {
        EXPECT_GE(matrix.row_pointers[i], matrix.row_pointers[i - 1]);
    }

    // Remap method should be captured
    EXPECT_EQ(matrix.remap_method, "Conservative");
}

TEST(ScripReader, ReadBilinear2DWeightFile) {
    auto path = kTestDataDir / "scrip_weights_bilinear_2d.nc";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Test data not generated: " << path;
    }

    auto result = tide::scrip::read_scrip_weights(path);
    ASSERT_TRUE(result.has_value()) << "Error: " << result.error().message;

    const auto& matrix = result.value();

    // This is an identity map with 16 cells
    EXPECT_EQ(matrix.n_src, 16u);
    EXPECT_EQ(matrix.n_dst, 16u);
    EXPECT_EQ(matrix.n_s, 16u);

    // All weights should be 1.0 (identity mapping, first column of 2D remap_matrix)
    for (std::size_t i = 0; i < matrix.n_s; ++i) {
        EXPECT_DOUBLE_EQ(matrix.values[i], 1.0);
    }

    // Each row should have exactly one entry
    for (std::size_t i = 0; i < matrix.n_dst; ++i) {
        EXPECT_EQ(matrix.row_pointers[i + 1] - matrix.row_pointers[i], 1);
    }

    EXPECT_EQ(matrix.remap_method, "Bilinear");
}

TEST(ScripReader, NonexistentFileReturnsError) {
    auto result = tide::scrip::read_scrip_weights("/nonexistent/path/weights.nc");
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::WeightFileNotFound));
    EXPECT_EQ(result.error().context, "scrip");
}

TEST(ScripReader, ZeroBasedConversion) {
    // After reading, all col_indices should be 0-based (non-negative)
    auto path = kTestDataDir / "scrip_weights_conservative.nc";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Test data not generated: " << path;
    }

    auto result = tide::scrip::read_scrip_weights(path);
    ASSERT_TRUE(result.has_value());

    const auto& matrix = result.value();
    for (std::size_t i = 0; i < matrix.n_s; ++i) {
        EXPECT_GE(matrix.col_indices[i], 0) << "col_indices[" << i << "] is negative";
        EXPECT_LT(static_cast<std::size_t>(matrix.col_indices[i]), matrix.n_src)
            << "col_indices[" << i << "] exceeds n_src";
    }
}

// =============================================================================
// Tests for apply_csr
// =============================================================================

TEST(ScripApply, IdentityMapping) {
    // Construct a simple identity CSR matrix (4x4)
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 4;
    matrix.n_dst = 4;
    matrix.n_s = 4;
    matrix.values = {1.0, 1.0, 1.0, 1.0};
    matrix.col_indices = {0, 1, 2, 3};
    matrix.row_pointers = {0, 1, 2, 3, 4};

    std::vector<double> source = {10.0, 20.0, 30.0, 40.0};
    std::vector<double> target(4, 0.0);

    int rc = tide::scrip::apply_csr(matrix, source, target);
    EXPECT_EQ(rc, 0);

    EXPECT_DOUBLE_EQ(target[0], 10.0);
    EXPECT_DOUBLE_EQ(target[1], 20.0);
    EXPECT_DOUBLE_EQ(target[2], 30.0);
    EXPECT_DOUBLE_EQ(target[3], 40.0);
}

TEST(ScripApply, WeightedAverage) {
    // Destination cell 0 blends source cells 0 and 1 with weights 0.6 and 0.4
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 3;
    matrix.n_dst = 2;
    matrix.n_s = 3;
    matrix.values = {0.6, 0.4, 1.0};
    matrix.col_indices = {0, 1, 2};
    matrix.row_pointers = {0, 2, 3};

    std::vector<double> source = {100.0, 200.0, 300.0};
    std::vector<double> target(2, 0.0);

    int rc = tide::scrip::apply_csr(matrix, source, target);
    EXPECT_EQ(rc, 0);

    // target[0] = 0.6*100 + 0.4*200 = 60 + 80 = 140
    EXPECT_DOUBLE_EQ(target[0], 140.0);
    // target[1] = 1.0*300 = 300
    EXPECT_DOUBLE_EQ(target[1], 300.0);
}

TEST(ScripApply, UniformFieldPreservation) {
    // Conservative property: uniform source should produce uniform target
    // when row weights sum to 1.0
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 4;
    matrix.n_dst = 3;
    matrix.n_s = 6;
    // Each dest cell gets contributions summing to 1.0
    matrix.values = {0.5, 0.5, 0.3, 0.7, 0.25, 0.75};
    matrix.col_indices = {0, 1, 1, 2, 2, 3};
    matrix.row_pointers = {0, 2, 4, 6};

    const double uniform_value = 42.0;
    std::vector<double> source(4, uniform_value);
    std::vector<double> target(3, 0.0);

    int rc = tide::scrip::apply_csr(matrix, source, target);
    EXPECT_EQ(rc, 0);

    for (std::size_t i = 0; i < 3; ++i) {
        EXPECT_NEAR(target[i], uniform_value, 1e-14)
            << "Uniform field not preserved at target cell " << i;
    }
}

TEST(ScripApply, EmptyMatrix) {
    // Zero links — target should remain zero
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 4;
    matrix.n_dst = 3;
    matrix.n_s = 0;
    matrix.row_pointers = {0, 0, 0, 0};

    std::vector<double> source = {1.0, 2.0, 3.0, 4.0};
    std::vector<double> target(3, 99.0);

    int rc = tide::scrip::apply_csr(matrix, source, target);
    EXPECT_EQ(rc, 0);

    // All targets should be 0 (sum of empty range)
    for (auto v : target) {
        EXPECT_DOUBLE_EQ(v, 0.0);
    }
}

// =============================================================================
// Tests for validate_dimensions
// =============================================================================

TEST(ScripValidate, MatchingDimensionsPass) {
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 16;
    matrix.n_dst = 16;

    int rc = tide::scrip::validate_dimensions(matrix, 16, 16);
    EXPECT_EQ(rc, 0);
}

TEST(ScripValidate, LargerExpectedDimensionsPass) {
    // It's valid for the matrix to reference fewer cells than the grid has
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 10;
    matrix.n_dst = 8;

    int rc = tide::scrip::validate_dimensions(matrix, 16, 16);
    EXPECT_EQ(rc, 0);
}

TEST(ScripValidate, SourceDimensionMismatchFails) {
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 20;  // Exceeds expected
    matrix.n_dst = 16;

    int rc = tide::scrip::validate_dimensions(matrix, 16, 16);
    EXPECT_EQ(rc, tide::to_int(tide::ErrorCode::WeightFileDimensionMismatch));
}

TEST(ScripValidate, DestDimensionMismatchFails) {
    tide::scrip::CsrMatrix matrix;
    matrix.n_src = 16;
    matrix.n_dst = 20;  // Exceeds expected

    int rc = tide::scrip::validate_dimensions(matrix, 16, 16);
    EXPECT_EQ(rc, tide::to_int(tide::ErrorCode::WeightFileDimensionMismatch));
}

// =============================================================================
// Integration test: read file then apply
// =============================================================================

TEST(ScripIntegration, ReadAndApplyIdentity) {
    auto path = kTestDataDir / "scrip_weights_bilinear_2d.nc";
    if (!std::filesystem::exists(path)) {
        GTEST_SKIP() << "Test data not generated: " << path;
    }

    auto result = tide::scrip::read_scrip_weights(path);
    ASSERT_TRUE(result.has_value()) << "Error: " << result.error().message;

    const auto& matrix = result.value();

    // Create a source field with known values
    std::vector<double> source(matrix.n_src);
    std::iota(source.begin(), source.end(), 1.0);  // 1, 2, 3, ..., 16

    std::vector<double> target(matrix.n_dst, 0.0);

    int rc = tide::scrip::apply_csr(matrix, source, target);
    EXPECT_EQ(rc, 0);

    // Identity mapping: target should equal source
    for (std::size_t i = 0; i < matrix.n_dst; ++i) {
        EXPECT_DOUBLE_EQ(target[i], source[i])
            << "Mismatch at cell " << i;
    }
}
