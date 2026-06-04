/**
 * @file test_types.cpp
 * @brief Unit tests for TIDE core data types (Grid, Error, FieldBuffer)
 *        and error handling (ErrorCode, ErrorBuffer).
 *
 * Validates Requirements: 1.4, 1.5, 1.6, 7.4, 7.5, 7.6, 7.7
 */

#include <gtest/gtest.h>

#include <tide/error.hpp>
#include <tide/types.hpp>

#include <cstring>
#include <string>
#include <string_view>

namespace tide {
namespace {

// =============================================================================
// SourceGrid tests
// =============================================================================

TEST(SourceGridTest, DefaultConstruction) {
    SourceGrid grid;
    EXPECT_EQ(grid.num_cells, 0u);
    EXPECT_TRUE(grid.lats.empty());
    EXPECT_TRUE(grid.lons.empty());
    EXPECT_TRUE(grid.lat_bounds.empty());
    EXPECT_TRUE(grid.lon_bounds.empty());
    EXPECT_TRUE(grid.levels.empty());
    EXPECT_TRUE(grid.level_units.empty());
    EXPECT_TRUE(grid.metadata_convention.empty());
    EXPECT_FALSE(grid.bounds_inferred);
}

TEST(SourceGridTest, AllTypesEnumerable) {
    // Verify all six grid types exist
    [[maybe_unused]] auto regular = SourceGrid::Type::RegularLatLon;
    [[maybe_unused]] auto gauss = SourceGrid::Type::Gaussian;
    [[maybe_unused]] auto reduced = SourceGrid::Type::ReducedGaussian;
    [[maybe_unused]] auto curvi = SourceGrid::Type::Curvilinear;
    [[maybe_unused]] auto unstruct = SourceGrid::Type::Unstructured;
    [[maybe_unused]] auto cloud = SourceGrid::Type::PointCloud;
}

TEST(SourceGridTest, PopulateRegularLatLon) {
    SourceGrid grid;
    grid.type = SourceGrid::Type::RegularLatLon;
    grid.num_cells = 16;
    grid.lats = {-45.0, -15.0, 15.0, 45.0};
    grid.lons = {0.0, 90.0, 180.0, 270.0};
    grid.levels = {1000.0, 850.0, 500.0, 200.0};
    grid.level_units = "Pa";
    grid.metadata_convention = "CF";
    grid.bounds_inferred = false;

    EXPECT_EQ(grid.type, SourceGrid::Type::RegularLatLon);
    EXPECT_EQ(grid.num_cells, 16u);
    EXPECT_EQ(grid.lats.size(), 4u);
    EXPECT_EQ(grid.lons.size(), 4u);
    EXPECT_EQ(grid.levels.size(), 4u);
    EXPECT_EQ(grid.level_units, "Pa");
    EXPECT_EQ(grid.metadata_convention, "CF");
    EXPECT_FALSE(grid.bounds_inferred);
}

TEST(SourceGridTest, BoundsInferred) {
    SourceGrid grid;
    grid.type = SourceGrid::Type::RegularLatLon;
    grid.num_cells = 4;
    grid.lats = {-45.0, 45.0};
    grid.lons = {0.0, 180.0};
    // Simulate inferred bounds
    grid.lat_bounds = {-90.0, 0.0, 90.0};
    grid.lon_bounds = {-90.0, 90.0, 270.0};
    grid.bounds_inferred = true;

    EXPECT_TRUE(grid.bounds_inferred);
    EXPECT_EQ(grid.lat_bounds.size(), 3u);
    EXPECT_EQ(grid.lon_bounds.size(), 3u);
}

TEST(SourceGridTest, CurvilinearType) {
    SourceGrid grid;
    grid.type = SourceGrid::Type::Curvilinear;
    grid.num_cells = 100;
    grid.metadata_convention = "CF";
    EXPECT_EQ(grid.type, SourceGrid::Type::Curvilinear);
}

TEST(SourceGridTest, PointCloudType) {
    SourceGrid grid;
    grid.type = SourceGrid::Type::PointCloud;
    grid.num_cells = 50;
    grid.lats.resize(50, 0.0);
    grid.lons.resize(50, 0.0);
    EXPECT_EQ(grid.type, SourceGrid::Type::PointCloud);
    EXPECT_EQ(grid.lats.size(), 50u);
}

// =============================================================================
// TargetGrid tests
// =============================================================================

TEST(TargetGridTest, DefaultConstruction) {
    TargetGrid grid;
    EXPECT_EQ(grid.num_cols, 0u);
    EXPECT_EQ(grid.num_levels, 0u);
    EXPECT_TRUE(grid.lats.empty());
    EXPECT_TRUE(grid.lons.empty());
    EXPECT_TRUE(grid.levels.empty());
    EXPECT_TRUE(grid.level_units.empty());
}

TEST(TargetGridTest, PopulateWithDimensions) {
    TargetGrid grid;
    grid.num_cols = 64;
    grid.num_levels = 10;
    grid.lats = {-80.0, -60.0, -40.0, -20.0, 0.0, 20.0, 40.0, 60.0, 80.0};
    grid.lons.resize(64, 0.0);
    grid.levels = {100000.0, 92500.0, 85000.0, 70000.0, 60000.0,
                   50000.0, 40000.0, 30000.0, 25000.0, 20000.0};
    grid.level_units = "Pa";

    EXPECT_EQ(grid.num_cols, 64u);
    EXPECT_EQ(grid.num_levels, 10u);
    EXPECT_EQ(grid.levels.size(), 10u);
    EXPECT_EQ(grid.level_units, "Pa");
}

// =============================================================================
// Error struct tests
// =============================================================================

TEST(ErrorStructTest, DefaultConstruction) {
    Error err;
    EXPECT_EQ(err.code, 0);
    EXPECT_TRUE(err.message.empty());
    EXPECT_TRUE(err.context.empty());
}

TEST(ErrorStructTest, ConstructWithValues) {
    Error err;
    err.code = to_int(ErrorCode::FileNotFound);
    err.message = "File '/data/forcing.nc' not found";
    err.context = "io";

    EXPECT_EQ(err.code, 1);
    EXPECT_EQ(err.message, "File '/data/forcing.nc' not found");
    EXPECT_EQ(err.context, "io");
}

// =============================================================================
// FieldBuffer tests
// =============================================================================

TEST(FieldBufferTest, DefaultConstruction) {
    FieldBuffer buf;
    EXPECT_TRUE(buf.empty());
    EXPECT_EQ(buf.size(), 0u);
    EXPECT_EQ(buf.ncols, 0u);
    EXPECT_EQ(buf.nlevels, 0u);
    EXPECT_EQ(buf.nfields, 0u);
}

TEST(FieldBufferTest, Resize) {
    FieldBuffer buf;
    buf.resize(16, 4, 1);

    EXPECT_EQ(buf.ncols, 16u);
    EXPECT_EQ(buf.nlevels, 4u);
    EXPECT_EQ(buf.nfields, 1u);
    EXPECT_EQ(buf.size(), 64u);
    EXPECT_EQ(buf.data.size(), 64u);
    EXPECT_FALSE(buf.empty());

    // All elements initialised to zero
    for (auto val : buf.data) {
        EXPECT_DOUBLE_EQ(val, 0.0);
    }
}

TEST(FieldBufferTest, View3dCOrder) {
    FieldBuffer buf;
    buf.resize(4, 3, 2);

    // Write via raw data
    for (std::size_t i = 0; i < buf.data.size(); ++i) {
        buf.data[i] = static_cast<double>(i);
    }

    auto view = buf.view_3d();
    EXPECT_EQ(view.extent(0), 4u);
    EXPECT_EQ(view.extent(1), 3u);
    EXPECT_EQ(view.extent(2), 2u);

    // layout_right: last index varies fastest
    // Element (0,0,0) should be data[0]
    EXPECT_DOUBLE_EQ((view[0, 0, 0]), 0.0);
    // Element (0,0,1) should be data[1]
    EXPECT_DOUBLE_EQ((view[0, 0, 1]), 1.0);
    // Element (0,1,0) should be data[2]
    EXPECT_DOUBLE_EQ((view[0, 1, 0]), 2.0);
}

TEST(FieldBufferTest, View3dFortranOrder) {
    FieldBuffer buf;
    buf.resize(4, 3, 2);

    for (std::size_t i = 0; i < buf.data.size(); ++i) {
        buf.data[i] = static_cast<double>(i);
    }

    auto view = buf.view_3d_fortran();
    EXPECT_EQ(view.extent(0), 4u);
    EXPECT_EQ(view.extent(1), 3u);
    EXPECT_EQ(view.extent(2), 2u);

    // layout_left: first index varies fastest
    // Element (0,0,0) should be data[0]
    EXPECT_DOUBLE_EQ((view[0, 0, 0]), 0.0);
    // Element (1,0,0) should be data[1]
    EXPECT_DOUBLE_EQ((view[1, 0, 0]), 1.0);
    // Element (2,0,0) should be data[2]
    EXPECT_DOUBLE_EQ((view[2, 0, 0]), 2.0);
    // Element (0,1,0) should be data[4]
    EXPECT_DOUBLE_EQ((view[0, 1, 0]), 4.0);
}

TEST(FieldBufferTest, MutableView3d) {
    FieldBuffer buf;
    buf.resize(2, 2, 1);

    auto view = buf.view_3d_mut();
    view[0, 0, 0] = 1.0;
    view[0, 1, 0] = 2.0;
    view[1, 0, 0] = 3.0;
    view[1, 1, 0] = 4.0;

    // Verify changes propagated to underlying data
    auto const_view = buf.view_3d();
    EXPECT_DOUBLE_EQ((const_view[0, 0, 0]), 1.0);
    EXPECT_DOUBLE_EQ((const_view[0, 1, 0]), 2.0);
    EXPECT_DOUBLE_EQ((const_view[1, 0, 0]), 3.0);
    EXPECT_DOUBLE_EQ((const_view[1, 1, 0]), 4.0);
}

TEST(FieldBufferTest, MutableFortranView) {
    FieldBuffer buf;
    buf.resize(3, 2, 1);

    auto view = buf.view_3d_fortran_mut();
    view[0, 0, 0] = 10.0;
    view[1, 0, 0] = 20.0;
    view[2, 0, 0] = 30.0;

    // layout_left: first index varies fastest
    EXPECT_DOUBLE_EQ(buf.data[0], 10.0);
    EXPECT_DOUBLE_EQ(buf.data[1], 20.0);
    EXPECT_DOUBLE_EQ(buf.data[2], 30.0);
}

// =============================================================================
// ErrorCode tests
// =============================================================================

TEST(ErrorCodeTest, SuccessIsZero) {
    EXPECT_EQ(to_int(ErrorCode::Success), 0);
}

TEST(ErrorCodeTest, IoErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::FileNotFound), 1);
    EXPECT_EQ(to_int(ErrorCode::FileUnreadable), 2);
    EXPECT_EQ(to_int(ErrorCode::UnsupportedFormat), 3);
    EXPECT_EQ(to_int(ErrorCode::MissingGridMetadata), 4);
    EXPECT_EQ(to_int(ErrorCode::MpiIoError), 5);
}

TEST(ErrorCodeTest, ConfigErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::ConfigFileNotFound), 100);
    EXPECT_EQ(to_int(ErrorCode::ConfigParseError), 101);
    EXPECT_EQ(to_int(ErrorCode::ConfigMissingField), 102);
    EXPECT_EQ(to_int(ErrorCode::ConfigInvalidValue), 103);
}

TEST(ErrorCodeTest, TemporalErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::TimeOutOfRange), 200);
    EXPECT_EQ(to_int(ErrorCode::TimeLevelsExhausted), 201);
}

TEST(ErrorCodeTest, SpatialErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::WeightComputationFailed), 300);
    EXPECT_EQ(to_int(ErrorCode::NonOverlappingGrids), 301);
}

TEST(ErrorCodeTest, VerticalErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::InsufficientLevels), 400);
    EXPECT_EQ(to_int(ErrorCode::ExtrapolationExceeded), 401);
}

TEST(ErrorCodeTest, ScalingErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::InvalidScalingParam), 500);
}

TEST(ErrorCodeTest, LifecycleErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::UninitializedHandle), 600);
    EXPECT_EQ(to_int(ErrorCode::AlreadyFinalized), 601);
}

TEST(ErrorCodeTest, FieldErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::FieldNotFound), 700);
    EXPECT_EQ(to_int(ErrorCode::FieldNotComputed), 701);
}

TEST(ErrorCodeTest, WeightFileErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::WeightFileNotFound), 800);
    EXPECT_EQ(to_int(ErrorCode::WeightFileDimensionMismatch), 801);
    EXPECT_EQ(to_int(ErrorCode::WeightFileMissingVariable), 802);
}

TEST(ErrorCodeTest, EsmfErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::EsmfFileNotFound), 850);
    EXPECT_EQ(to_int(ErrorCode::EsmfMeshMissingVariable), 851);
    EXPECT_EQ(to_int(ErrorCode::EsmfGridSpecMissingVariable), 852);
}

TEST(ErrorCodeTest, MultiStreamErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::MemoryBudgetExceeded), 900);
    EXPECT_EQ(to_int(ErrorCode::StreamIndexOutOfRange), 901);
}

TEST(ErrorCodeTest, PrefetchErrorRange) {
    EXPECT_EQ(to_int(ErrorCode::PrefetchFailed), 950);
}

TEST(ErrorCodeTest, FromInt) {
    EXPECT_EQ(from_int(0), ErrorCode::Success);
    EXPECT_EQ(from_int(1), ErrorCode::FileNotFound);
    EXPECT_EQ(from_int(500), ErrorCode::InvalidScalingParam);
    EXPECT_EQ(from_int(800), ErrorCode::WeightFileNotFound);
    EXPECT_EQ(from_int(900), ErrorCode::MemoryBudgetExceeded);
    EXPECT_EQ(from_int(950), ErrorCode::PrefetchFailed);
}

// =============================================================================
// error_category tests
// =============================================================================

TEST(ErrorCategoryTest, SuccessCategory) {
    EXPECT_EQ(error_category(0), "Success");
    EXPECT_EQ(error_category(ErrorCode::Success), "Success");
}

TEST(ErrorCategoryTest, IoCategory) {
    EXPECT_EQ(error_category(1), "I/O");
    EXPECT_EQ(error_category(50), "I/O");
    EXPECT_EQ(error_category(99), "I/O");
}

TEST(ErrorCategoryTest, ConfigCategory) {
    EXPECT_EQ(error_category(100), "Configuration");
    EXPECT_EQ(error_category(150), "Configuration");
    EXPECT_EQ(error_category(199), "Configuration");
}

TEST(ErrorCategoryTest, TemporalCategory) {
    EXPECT_EQ(error_category(200), "Temporal");
    EXPECT_EQ(error_category(250), "Temporal");
    EXPECT_EQ(error_category(299), "Temporal");
}

TEST(ErrorCategoryTest, SpatialCategory) {
    EXPECT_EQ(error_category(300), "Spatial");
    EXPECT_EQ(error_category(350), "Spatial");
    EXPECT_EQ(error_category(399), "Spatial");
}

TEST(ErrorCategoryTest, VerticalCategory) {
    EXPECT_EQ(error_category(400), "Vertical");
    EXPECT_EQ(error_category(450), "Vertical");
    EXPECT_EQ(error_category(499), "Vertical");
}

TEST(ErrorCategoryTest, ScalingCategory) {
    EXPECT_EQ(error_category(500), "Scaling");
    EXPECT_EQ(error_category(550), "Scaling");
    EXPECT_EQ(error_category(599), "Scaling");
}

TEST(ErrorCategoryTest, LifecycleCategory) {
    EXPECT_EQ(error_category(600), "Lifecycle");
    EXPECT_EQ(error_category(650), "Lifecycle");
    EXPECT_EQ(error_category(699), "Lifecycle");
}

TEST(ErrorCategoryTest, FieldCategory) {
    EXPECT_EQ(error_category(700), "Field");
    EXPECT_EQ(error_category(750), "Field");
    EXPECT_EQ(error_category(799), "Field");
}

TEST(ErrorCategoryTest, WeightGridCategory) {
    EXPECT_EQ(error_category(800), "Weight/Grid");
    EXPECT_EQ(error_category(850), "Weight/Grid");
    EXPECT_EQ(error_category(899), "Weight/Grid");
}

TEST(ErrorCategoryTest, MultiStreamCategory) {
    EXPECT_EQ(error_category(900), "Multi-Stream");
    EXPECT_EQ(error_category(950), "Multi-Stream");
    EXPECT_EQ(error_category(999), "Multi-Stream");
}

TEST(ErrorCategoryTest, UnknownCategory) {
    EXPECT_EQ(error_category(-1), "Unknown");
    EXPECT_EQ(error_category(1000), "Unknown");
    EXPECT_EQ(error_category(1500), "Unknown");
}

// =============================================================================
// ErrorBuffer tests
// =============================================================================

TEST(ErrorBufferTest, DefaultIsEmpty) {
    ErrorBuffer buf;
    EXPECT_FALSE(buf.has_error());
    EXPECT_EQ(buf.code(), ErrorCode::Success);
    EXPECT_EQ(buf.code_int(), 0);
    EXPECT_STREQ(buf.message(), "");
}

TEST(ErrorBufferTest, SetSimpleMessage) {
    ErrorBuffer buf;
    buf.set(ErrorCode::FileNotFound, "File not found: /path/to/data.nc");

    EXPECT_TRUE(buf.has_error());
    EXPECT_EQ(buf.code(), ErrorCode::FileNotFound);
    EXPECT_EQ(buf.code_int(), 1);
    EXPECT_STREQ(buf.message(), "File not found: /path/to/data.nc");
}

TEST(ErrorBufferTest, SetWithContext) {
    ErrorBuffer buf;
    buf.set(ErrorCode::ConfigMissingField, "config",
            "Missing required field 'file_path' in stream 'temperature'");

    EXPECT_TRUE(buf.has_error());
    EXPECT_EQ(buf.code(), ErrorCode::ConfigMissingField);
    EXPECT_EQ(buf.code_int(), 102);
    EXPECT_STREQ(buf.message(),
                 "[config] Missing required field 'file_path' in stream 'temperature'");
}

TEST(ErrorBufferTest, ClearResetsState) {
    ErrorBuffer buf;
    buf.set(ErrorCode::TimeOutOfRange, "Time 999.0 out of range");
    EXPECT_TRUE(buf.has_error());

    buf.clear();
    EXPECT_FALSE(buf.has_error());
    EXPECT_EQ(buf.code(), ErrorCode::Success);
    EXPECT_STREQ(buf.message(), "");
}

TEST(ErrorBufferTest, TruncatesLongMessage) {
    ErrorBuffer buf;
    // Create a message longer than kErrorBufferSize
    std::string long_msg(kErrorBufferSize + 100, 'X');
    buf.set(ErrorCode::FileNotFound, long_msg);

    EXPECT_TRUE(buf.has_error());
    // Message should be truncated to kErrorBufferSize - 1 chars
    EXPECT_EQ(std::strlen(buf.message()), kErrorBufferSize - 1);
}

TEST(ErrorBufferTest, NoHeapAllocationOnSet) {
    // The ErrorBuffer uses std::array<char, kErrorBufferSize> internally,
    // so no heap allocation occurs. We verify the buffer size is fixed.
    static_assert(kErrorBufferSize == 512,
                  "Buffer should be 512 bytes for no-heap error paths");

    ErrorBuffer buf;
    // Setting errors should not throw or allocate
    buf.set(ErrorCode::UninitializedHandle, "lifecycle",
            "Handle not initialized");
    EXPECT_TRUE(buf.has_error());
}

TEST(ErrorBufferTest, OverwritePreviousError) {
    ErrorBuffer buf;
    buf.set(ErrorCode::FileNotFound, "First error");
    EXPECT_STREQ(buf.message(), "First error");

    buf.set(ErrorCode::ConfigParseError, "Second error");
    EXPECT_EQ(buf.code(), ErrorCode::ConfigParseError);
    EXPECT_STREQ(buf.message(), "Second error");
}

TEST(ErrorBufferTest, ContextWithLongMessage) {
    ErrorBuffer buf;
    std::string ctx = "io";
    std::string msg(kErrorBufferSize, 'Z'); // Longer than buffer
    buf.set(ErrorCode::FileUnreadable, ctx, msg);

    EXPECT_TRUE(buf.has_error());
    // Should start with "[io] "
    std::string result(buf.message());
    EXPECT_EQ(result.substr(0, 5), "[io] ");
    // Total should be truncated
    EXPECT_LE(result.size(), kErrorBufferSize - 1);
}

} // anonymous namespace
} // namespace tide
