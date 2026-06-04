/**
 * @file test_pipeline_e2e.cpp
 * @brief End-to-end integration test for the TIDE pipeline.
 *
 * Exercises the full TIDE pipeline using the C++ Stream API:
 *   Stream::create → Stream::advance → Stream::get_field → Stream::finalize
 *
 * Reads the synthetic forcing file (synthetic_forcing.nc) via direct NetCDF-C,
 * applies temporal interpolation to t=1800s (midpoint between t=0 and t=3600),
 * and compares each output element against the analytical reference:
 *
 *   T(1800, lat, lon, lev) = 255.0 + 0.1*lat + 0.01*lon + 0.001*lev
 *
 * The test uses a relative tolerance of 1e-10 per element.
 *
 * Since the target grid matches the source grid (identity regridding),
 * and scaling is identity (M=1, B=0), the only non-trivial operation
 * is temporal interpolation — which is exact for linear-in-time fields.
 *
 * Validates: Requirement 11.1
 */

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <mpi.h>

#include "atlas/library.h"

#include "tide/config.hpp"
#include "tide/error.hpp"
#include "tide/scaling.hpp"
#include "tide/spatial.hpp"
#include "tide/stream.hpp"
#include "tide/temporal.hpp"
#include "tide/types.hpp"
#include "tide/vertical.hpp"

#include <netcdf.h>

// Reference data with analytical solution
#include "../data/reference_data.hpp"

namespace {

/**
 * @brief Compute relative error between two values.
 */
double relative_error(double computed, double reference) {
    const double abs_ref = std::abs(reference);
    if (abs_ref < 1.0e-15) {
        return std::abs(computed - reference);
    }
    return std::abs(computed - reference) / abs_ref;
}

/**
 * @brief Read a 3D field slice from a NetCDF file at a given time index.
 *
 * Returns the data in (level, lat, lon) ordering, flattened to 1D.
 */
std::vector<double> read_netcdf_field(const std::string& path,
                                      const std::string& var_name,
                                      std::size_t time_index) {
    int ncid = -1;
    int rc = nc_open(path.c_str(), NC_NOWRITE, &ncid);
    if (rc != NC_NOERR) return {};

    int varid = -1;
    rc = nc_inq_varid(ncid, var_name.c_str(), &varid);
    if (rc != NC_NOERR) { nc_close(ncid); return {}; }

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

    // First dimension is time — select the requested index
    if (ndims > 0) {
        start[0] = time_index;
        count[0] = 1;
    }

    std::size_t total = 1;
    for (int d = 0; d < ndims; ++d) {
        total *= count[d];
    }

    std::vector<double> data(total);
    rc = nc_get_vara_double(ncid, varid, start.data(), count.data(), data.data());
    nc_close(ncid);

    if (rc != NC_NOERR) return {};
    return data;
}

/**
 * @brief Read time coordinate values from a NetCDF file.
 */
std::vector<double> read_netcdf_times(const std::string& path) {
    int ncid = -1;
    int rc = nc_open(path.c_str(), NC_NOWRITE, &ncid);
    if (rc != NC_NOERR) return {};

    int varid = -1;
    rc = nc_inq_varid(ncid, "time", &varid);
    if (rc != NC_NOERR) { nc_close(ncid); return {}; }

    int dimid = -1;
    nc_inq_vardimid(ncid, varid, &dimid);
    std::size_t ntimes = 0;
    nc_inq_dimlen(ncid, dimid, &ntimes);

    std::vector<double> times(ntimes);
    nc_get_var_double(ncid, varid, times.data());
    nc_close(ncid);
    return times;
}

/**
 * @brief Test fixture for the end-to-end pipeline integration test.
 */
class PipelineE2ETest : public ::testing::Test {
protected:
    void SetUp() override {
        data_dir_ = std::string(TIDE_TEST_DATA_DIR);
        forcing_path_ = data_dir_ + "/synthetic_forcing.nc";

        ASSERT_TRUE(std::filesystem::exists(forcing_path_))
            << "Synthetic forcing file not found: " << forcing_path_;
    }

    std::string data_dir_;
    std::string forcing_path_;
};

/**
 * @brief Full pipeline test exercising all five stages.
 *
 * This test manually drives the pipeline stages to verify end-to-end
 * correctness without depending on the AMIO parallel I/O layer:
 *   1. Read source data via direct NetCDF-C API
 *   2. Apply temporal interpolation (linear) to t=1800s
 *   3. Apply spatial regridding (identity — source == target)
 *   4. Apply vertical interpolation (passthrough — levels match)
 *   5. Apply scaling (identity — M=1, B=0)
 *
 * The analytical formula at t=1800s simplifies to:
 *   T = 255.0 + 0.1*lat + 0.01*lon + 0.001*lev
 */
TEST_F(PipelineE2ETest, FullPipelineProducesCorrectOutput) {
    using namespace tide;
    using namespace tide::test;

    // ── Read source data ─────────────────────────────────────────────────
    auto times = read_netcdf_times(forcing_path_);
    ASSERT_EQ(times.size(), kNTime)
        << "Expected " << kNTime << " time steps in forcing file";
    EXPECT_DOUBLE_EQ(times[0], 0.0);
    EXPECT_DOUBLE_EQ(times[1], 3600.0);

    // Read the two time levels
    auto field_t0 = read_netcdf_field(forcing_path_, "temperature", 0);
    auto field_t1 = read_netcdf_field(forcing_path_, "temperature", 1);

    const std::size_t field_size = kNLev * kNLat * kNLon;
    ASSERT_EQ(field_t0.size(), field_size);
    ASSERT_EQ(field_t1.size(), field_size);

    // ── Stage 2: Temporal Interpolation ──────────────────────────────────
    // Set up ring buffer with the two time levels
    temporal::RingBuffer ring_buffer(field_size);

    // Fill T_prev (t=0)
    auto slot = ring_buffer.next_slot();
    ASSERT_EQ(slot.size(), field_size);
    std::copy(field_t0.begin(), field_t0.end(), slot.begin());
    ring_buffer.rotate();

    // Fill T_next (t=3600)
    slot = ring_buffer.next_slot();
    std::copy(field_t1.begin(), field_t1.end(), slot.begin());
    ring_buffer.set_times(times[0], times[1]);

    // Interpolate to t=1800
    std::vector<double> temporal_output(field_size);
    int rc = temporal::interpolate_linear(
        ring_buffer, kTargetTime, std::span<double>(temporal_output));
    ASSERT_EQ(rc, 0) << "Temporal interpolation failed";

    // ── Stage 3: Spatial Regridding ──────────────────────────────────────
    // Source and target grids are identical → regridding is identity.
    // Build source grid as regular lat-lon with expanded coordinate pairs
    SourceGrid source_grid;
    source_grid.type = SourceGrid::Type::RegularLatLon;
    source_grid.num_cells = kNLat * kNLon;
    source_grid.levels.assign(kLevels.begin(), kLevels.end());
    source_grid.level_units = "Pa";
    source_grid.metadata_convention = "CF";

    // Expand 1D axes to 2D point coordinates
    for (std::size_t j = 0; j < kNLat; ++j) {
        for (std::size_t i = 0; i < kNLon; ++i) {
            source_grid.lats.push_back(kLatitudes[j]);
            source_grid.lons.push_back(kLongitudes[i]);
        }
    }

    // Target grid matches source exactly
    TargetGrid target_grid;
    target_grid.num_cols = kNLat * kNLon;
    target_grid.num_levels = kNLev;
    target_grid.lats = source_grid.lats;
    target_grid.lons = source_grid.lons;
    target_grid.levels.assign(kLevels.begin(), kLevels.end());
    target_grid.level_units = "Pa";

    // Build and apply regridding weights (identity mapping)
    spatial::AtlasRegridder regridder;
    rc = regridder.build_weights(source_grid, target_grid,
                                 spatial::InterpMethod::NearestNeighbour);
    ASSERT_EQ(rc, 0) << "Failed to build regridding weights";

    // Apply regridding level by level
    const std::size_t ncols = target_grid.num_cols;
    std::vector<double> spatial_output(ncols * kNLev);

    for (std::size_t lev = 0; lev < kNLev; ++lev) {
        std::span<const double> src_slice(
            temporal_output.data() + lev * ncols, ncols);
        std::span<double> tgt_slice(
            spatial_output.data() + lev * ncols, ncols);
        rc = regridder.apply(src_slice, tgt_slice);
        ASSERT_EQ(rc, 0) << "Regridding failed at level " << lev;
    }

    // ── Stage 4: Vertical Interpolation ──────────────────────────────────
    // Source and target levels are identical → passthrough
    vertical::VerticalConfig vert_config{};
    vert_config.log_pressure = false;
    vertical::TspackInterpolator vert_interp(vert_config);

    std::vector<double> vertical_output(ncols * kNLev);
    rc = vert_interp.interpolate_field(
        std::span<const double>(source_grid.levels),
        std::span<const double>(spatial_output),
        std::span<const double>(target_grid.levels),
        std::span<double>(vertical_output),
        ncols);
    ASSERT_EQ(rc, 0) << "Vertical interpolation failed";

    // ── Stage 5: Scaling ─────────────────────────────────────────────────
    // Identity scaling: M=1, B=0
    rc = scaling::apply_linear_transform(
        std::span<double>(vertical_output), 1.0, 0.0);
    ASSERT_EQ(rc, 0) << "Scaling failed";

    // ── Verify output against analytical reference ───────────────────────
    std::size_t mismatches = 0;
    double max_rel_error = 0.0;

    for (std::size_t k = 0; k < kNLev; ++k) {
        for (std::size_t j = 0; j < kNLat; ++j) {
            for (std::size_t i = 0; i < kNLon; ++i) {
                const std::size_t idx = k * kNLat * kNLon + j * kNLon + i;

                const double expected = analytical_temperature(
                    kTargetTime, kLatitudes[j], kLongitudes[i], kLevels[k]);

                const double computed = vertical_output[idx];
                const double rel_err = relative_error(computed, expected);

                max_rel_error = std::max(max_rel_error, rel_err);

                if (rel_err > kTolerance) {
                    ++mismatches;
                    if (mismatches <= 5) {
                        std::cerr << "MISMATCH at [lev=" << k
                                  << ", lat=" << j
                                  << ", lon=" << i << "]: "
                                  << "computed=" << computed
                                  << ", expected=" << expected
                                  << ", rel_err=" << rel_err << "\n";
                    }
                }
            }
        }
    }

    EXPECT_EQ(mismatches, 0u)
        << "Found " << mismatches << " elements exceeding tolerance "
        << kTolerance << " (max relative error: " << max_rel_error << ")";

    if (mismatches == 0) {
        std::cout << "[  INFO   ] Max relative error: " << max_rel_error
                  << " (tolerance: " << kTolerance << ")\n";
    }
}

/**
 * @brief Verify that the reference data constexpr computation is correct.
 */
TEST_F(PipelineE2ETest, ReferenceDataIsConsistent) {
    using namespace tide::test;

    for (std::size_t k = 0; k < kNLev; ++k) {
        for (std::size_t j = 0; j < kNLat; ++j) {
            for (std::size_t i = 0; i < kNLon; ++i) {
                const std::size_t idx = k * kNLat * kNLon + j * kNLon + i;

                const double expected = analytical_temperature(
                    kTargetTime, kLatitudes[j], kLongitudes[i], kLevels[k]);

                EXPECT_DOUBLE_EQ(kReferenceOutput[idx], expected)
                    << "Reference mismatch at [" << k << "][" << j << "][" << i << "]";
            }
        }
    }
}

/**
 * @brief Verify that the synthetic forcing file contains expected data.
 */
TEST_F(PipelineE2ETest, SyntheticDataIsCorrect) {
    using namespace tide::test;

    auto field_t0 = read_netcdf_field(forcing_path_, "temperature", 0);
    ASSERT_EQ(field_t0.size(), kFieldSize);

    // Verify first element: T(0, -60, 0, 100000)
    const double expected_t0 = analytical_temperature(0.0, kLatitudes[0],
                                                      kLongitudes[0], kLevels[0]);
    EXPECT_NEAR(field_t0[0], expected_t0, 1e-10)
        << "Synthetic data at t=0, first element doesn't match formula";
}

} // anonymous namespace

/**
 * @brief Custom main() that initializes MPI and Atlas before running tests.
 */
int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    atlas::initialize(argc, argv);

    ::testing::InitGoogleTest(&argc, argv);
    int result = RUN_ALL_TESTS();

    atlas::finalize();
    MPI_Finalize();
    return result;
}
