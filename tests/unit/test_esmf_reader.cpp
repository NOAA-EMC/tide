/**
 * @file test_esmf_reader.cpp
 * @brief Unit tests for the TIDE ESMF Grid File Reader.
 *
 * Tests cover:
 * - Reading ESMF mesh files and producing SourceGrid with Unstructured type
 * - Reading ESMF grid spec files with and without corner coordinates
 * - Producing TargetGrid from ESMF files via read_esmf_as_target
 * - Error handling: file not found, missing variables
 *
 * Validates Requirements: 2.1, 2.2, 2.3, 2.4, 2.5, 2.6, 2.7
 */

#include <tide/grid.hpp>
#include <tide/error.hpp>
#include <tide/types.hpp>

#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

namespace {

/// @brief Path to test data directory (set by CMake compile definition).
const std::filesystem::path kTestDataDir{TIDE_TEST_DATA_DIR};

// =============================================================================
// ESMF Mesh Reader Tests
// =============================================================================

TEST(EsmfMeshReader, ReadsValidMeshFile) {
    const auto path = kTestDataDir / "esmf_mesh_100cells.nc";
    auto result = tide::grid::read_esmf_mesh(path);

    ASSERT_TRUE(result.has_value())
        << "Error: code=" << result.error().code
        << " msg=" << result.error().message;

    const auto& grid = result.value();

    // Should be unstructured type
    EXPECT_EQ(grid.type, tide::SourceGrid::Type::Unstructured);

    // 10x10 grid = 100 elements
    EXPECT_EQ(grid.num_cells, 100u);

    // Should have center coordinates for each element
    EXPECT_EQ(grid.lats.size(), 100u);
    EXPECT_EQ(grid.lons.size(), 100u);

    // Bounds should contain vertex coordinates for conservative regridding
    // Each quad element has 4 vertices: total = 100 * 4 = 400
    EXPECT_EQ(grid.lat_bounds.size(), 400u);
    EXPECT_EQ(grid.lon_bounds.size(), 400u);

    // Metadata convention
    EXPECT_EQ(grid.metadata_convention, "ESMF_Mesh");
    EXPECT_FALSE(grid.bounds_inferred);
}

TEST(EsmfMeshReader, CellCentersAreAverageOfNodes) {
    const auto path = kTestDataDir / "esmf_mesh_100cells.nc";
    auto result = tide::grid::read_esmf_mesh(path);
    ASSERT_TRUE(result.has_value());

    const auto& grid = result.value();

    // For a regular 10x10 quad mesh covering [0,360] x [-90,90],
    // the first element center should be approximately:
    // lon: (0 + 36 + 36 + 0) / 4 = 18.0
    // lat: (-90 + -90 + -72 + -72) / 4 = -81.0
    EXPECT_NEAR(grid.lons[0], 18.0, 0.01);
    EXPECT_NEAR(grid.lats[0], -81.0, 0.01);

    // All latitudes should be in [-90, 90]
    for (const auto& lat : grid.lats) {
        EXPECT_GE(lat, -90.0);
        EXPECT_LE(lat, 90.0);
    }

    // All longitudes should be in [0, 360]
    for (const auto& lon : grid.lons) {
        EXPECT_GE(lon, 0.0);
        EXPECT_LE(lon, 360.0);
    }
}

TEST(EsmfMeshReader, ReturnsEsmfFileNotFoundForMissingFile) {
    const auto path = kTestDataDir / "nonexistent_mesh.nc";
    auto result = tide::grid::read_esmf_mesh(path);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::EsmfFileNotFound));
    EXPECT_EQ(result.error().context, "grid");
}

TEST(EsmfMeshReader, ReturnsEsmfMeshMissingVariableForBadFile) {
    // Use the SCRIP weights file which has different variables
    const auto path = kTestDataDir / "scrip_weights_conservative.nc";
    auto result = tide::grid::read_esmf_mesh(path);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::EsmfMeshMissingVariable));
    EXPECT_EQ(result.error().context, "grid");
}

// =============================================================================
// ESMF Grid Spec Reader Tests
// =============================================================================

TEST(EsmfGridSpecReader, ReadsValidGridSpecWithCorners) {
    const auto path = kTestDataDir / "esmf_grid_spec.nc";
    auto result = tide::grid::read_esmf_grid_spec(path);

    ASSERT_TRUE(result.has_value())
        << "Error: code=" << result.error().code
        << " msg=" << result.error().message;

    const auto& grid = result.value();

    // 18x36 grid = 648 cells
    EXPECT_EQ(grid.num_cells, 648u);

    // Should have center coordinates
    EXPECT_EQ(grid.lats.size(), 648u);
    EXPECT_EQ(grid.lons.size(), 648u);

    // Should have corner coordinates (4 corners per cell)
    EXPECT_EQ(grid.lat_bounds.size(), 648u * 4u);
    EXPECT_EQ(grid.lon_bounds.size(), 648u * 4u);

    // Metadata
    EXPECT_EQ(grid.metadata_convention, "ESMF_GridSpec");
    EXPECT_FALSE(grid.bounds_inferred);
}

TEST(EsmfGridSpecReader, ReadsValidGridSpecWithoutCorners) {
    const auto path = kTestDataDir / "esmf_grid_spec_no_corners.nc";
    auto result = tide::grid::read_esmf_grid_spec(path);

    ASSERT_TRUE(result.has_value())
        << "Error: code=" << result.error().code
        << " msg=" << result.error().message;

    const auto& grid = result.value();

    // 4x8 grid = 32 cells
    EXPECT_EQ(grid.num_cells, 32u);

    // Should have center coordinates
    EXPECT_EQ(grid.lats.size(), 32u);
    EXPECT_EQ(grid.lons.size(), 32u);

    // No corner coordinates — bounds_inferred should be true
    EXPECT_TRUE(grid.lat_bounds.empty());
    EXPECT_TRUE(grid.lon_bounds.empty());
    EXPECT_TRUE(grid.bounds_inferred);
}

TEST(EsmfGridSpecReader, CenterCoordinatesAreCorrect) {
    const auto path = kTestDataDir / "esmf_grid_spec.nc";
    auto result = tide::grid::read_esmf_grid_spec(path);
    ASSERT_TRUE(result.has_value());

    const auto& grid = result.value();

    // All latitudes should be in (-90, 90) for cell centers
    for (const auto& lat : grid.lats) {
        EXPECT_GT(lat, -90.0);
        EXPECT_LT(lat, 90.0);
    }

    // All longitudes should be in (0, 360) for cell centers
    for (const auto& lon : grid.lons) {
        EXPECT_GT(lon, 0.0);
        EXPECT_LT(lon, 360.0);
    }
}

TEST(EsmfGridSpecReader, ReturnsEsmfFileNotFoundForMissingFile) {
    const auto path = kTestDataDir / "nonexistent_grid_spec.nc";
    auto result = tide::grid::read_esmf_grid_spec(path);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::EsmfFileNotFound));
    EXPECT_EQ(result.error().context, "grid");
}

TEST(EsmfGridSpecReader, ReturnsEsmfGridSpecMissingVariableForBadFile) {
    // Use the SCRIP weights file which has different variables
    const auto path = kTestDataDir / "scrip_weights_conservative.nc";
    auto result = tide::grid::read_esmf_grid_spec(path);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code,
              tide::to_int(tide::ErrorCode::EsmfGridSpecMissingVariable));
    EXPECT_EQ(result.error().context, "grid");
}

// =============================================================================
// ESMF as Target Grid Tests
// =============================================================================

TEST(EsmfAsTarget, ReadsMeshAsTargetGrid) {
    const auto path = kTestDataDir / "esmf_mesh_100cells.nc";
    auto result = tide::grid::read_esmf_as_target(path, tide::grid::EsmfFileType::Mesh);

    ASSERT_TRUE(result.has_value())
        << "Error: code=" << result.error().code
        << " msg=" << result.error().message;

    const auto& target = result.value();

    EXPECT_EQ(target.num_cols, 100u);
    EXPECT_EQ(target.lats.size(), 100u);
    EXPECT_EQ(target.lons.size(), 100u);
    EXPECT_EQ(target.esmf_mesh, path.string());
    EXPECT_TRUE(target.esmf_grid_spec.empty());
}

TEST(EsmfAsTarget, ReadsGridSpecAsTargetGrid) {
    const auto path = kTestDataDir / "esmf_grid_spec.nc";
    auto result = tide::grid::read_esmf_as_target(path, tide::grid::EsmfFileType::GridSpec);

    ASSERT_TRUE(result.has_value())
        << "Error: code=" << result.error().code
        << " msg=" << result.error().message;

    const auto& target = result.value();

    EXPECT_EQ(target.num_cols, 648u);
    EXPECT_EQ(target.lats.size(), 648u);
    EXPECT_EQ(target.lons.size(), 648u);
    EXPECT_EQ(target.esmf_grid_spec, path.string());
    EXPECT_TRUE(target.esmf_mesh.empty());
}

TEST(EsmfAsTarget, ReturnsErrorForMissingMeshFile) {
    const auto path = kTestDataDir / "nonexistent.nc";
    auto result = tide::grid::read_esmf_as_target(path, tide::grid::EsmfFileType::Mesh);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::EsmfFileNotFound));
}

TEST(EsmfAsTarget, ReturnsErrorForMissingGridSpecFile) {
    const auto path = kTestDataDir / "nonexistent.nc";
    auto result = tide::grid::read_esmf_as_target(path, tide::grid::EsmfFileType::GridSpec);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, tide::to_int(tide::ErrorCode::EsmfFileNotFound));
}

} // anonymous namespace
