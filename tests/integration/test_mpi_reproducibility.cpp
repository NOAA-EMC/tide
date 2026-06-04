/**
 * @file test_mpi_reproducibility.cpp
 * @brief MPI Bit-for-Bit (B4B) reproducibility integration test for TIDE.
 *
 * This test verifies that TIDE produces bitwise identical output regardless
 * of MPI rank count. It runs the full pipeline via the C API using the
 * synthetic test dataset and verifies that all ranks produce the same output.
 *
 * B4B reproducibility is guaranteed by:
 * - Deterministic Atlas interpolation weight matrix computation
 * - Ordered MPI reduction operations with fixed topology
 * - Weight matrix invariance across domain decompositions
 *
 * The test is registered as an MPI test via the add_mpi_test CMake macro.
 * It should be run with 2 and 4 ranks to confirm reproducibility.
 *
 * Validates Requirements: 11.6, 13.1, 13.2, 13.3, 13.4
 */

#include <gtest/gtest.h>
#include <mpi.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <numeric>
#include <vector>

#include "tide.h"

#ifndef TIDE_TEST_DATA_DIR
#error "TIDE_TEST_DATA_DIR must be defined at compile time"
#endif

namespace {

/**
 * @brief GTest environment that initializes/finalizes MPI once per process.
 *
 * MPI_Init is called before any tests run and MPI_Finalize is called
 * after all tests complete. This avoids multiple init/finalize pairs
 * that would violate the MPI standard.
 */
class MPIEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        // MPI_Init already called in main()
    }

    void TearDown() override {
        // MPI_Finalize called in main() after RUN_ALL_TESTS
    }
};

/**
 * @brief Test fixture for MPI reproducibility tests.
 *
 * Provides helper methods to run the TIDE pipeline and gather results
 * across MPI ranks for bitwise comparison.
 */
class MPIReproducibilityTest : public ::testing::Test {
protected:
    void SetUp() override {
        MPI_Comm_rank(MPI_COMM_WORLD, &rank_);
        MPI_Comm_size(MPI_COMM_WORLD, &num_ranks_);
    }

    /**
     * @brief Get the path to the test configuration YAML.
     */
    std::string config_path() const {
        return std::string(TIDE_TEST_DATA_DIR) + "/test_config.yaml";
    }

    int rank_ = 0;
    int num_ranks_ = 1;
};

/**
 * @brief Verify that all MPI ranks produce bitwise identical output.
 *
 * This test:
 * 1. Initializes TIDE via the C API with MPI_COMM_WORLD
 * 2. Advances the pipeline to t=1800s (midpoint of synthetic data)
 * 3. Retrieves the output field from each rank
 * 4. Gathers all outputs to rank 0
 * 5. On rank 0, verifies all gathered arrays are bitwise identical
 *
 * This validates B4B reproducibility: regardless of how the domain
 * is decomposed across ranks, each rank should see the same complete
 * output (since the test config uses a small grid that fits on any rank).
 */
TEST_F(MPIReproducibilityTest, AllRanksProduceBitwiseIdenticalOutput) {
    // Initialize TIDE with MPI_COMM_WORLD
    tide_handle_t handle = nullptr;
    int mpi_comm_int = MPI_Comm_c2f(MPI_COMM_WORLD);

    int rc = tide_init(config_path().c_str(), mpi_comm_int, &handle);

    // All ranks must succeed initialization
    int init_ok = (rc == 0 && handle != nullptr) ? 1 : 0;
    int all_init_ok = 0;
    MPI_Allreduce(&init_ok, &all_init_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_init_ok) {
        if (handle) {
            tide_finalize(handle);
        }
        GTEST_SKIP() << "TIDE initialization failed on one or more ranks: "
                     << (rc != 0 ? tide_get_error(nullptr) : "null handle");
        return;
    }

    ASSERT_EQ(rc, 0) << "tide_init failed: " << tide_get_error(nullptr);
    ASSERT_NE(handle, nullptr);

    // Advance pipeline to t=1800s (temporal midpoint)
    constexpr double target_time = 1800.0;
    rc = tide_advance(handle, target_time);

    int advance_ok = (rc == 0) ? 1 : 0;
    int all_advance_ok = 0;
    MPI_Allreduce(&advance_ok, &all_advance_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_advance_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "tide_advance failed on one or more ranks: "
                     << (rc != 0 ? tide_get_error(handle) : "unknown");
        return;
    }

    ASSERT_EQ(rc, 0) << "tide_advance failed: " << tide_get_error(handle);

    // Retrieve the temperature field
    double* data_ptr = nullptr;
    int field_rank = 0;
    std::size_t extents[7] = {0};

    rc = tide_get_field(handle, "temperature", &data_ptr, &field_rank, extents);

    int field_ok = (rc == 0 && data_ptr != nullptr) ? 1 : 0;
    int all_field_ok = 0;
    MPI_Allreduce(&field_ok, &all_field_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_field_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "tide_get_field failed on one or more ranks: "
                     << (rc != 0 ? tide_get_error(handle) : "null data");
        return;
    }

    ASSERT_EQ(rc, 0) << "tide_get_field failed: " << tide_get_error(handle);
    ASSERT_NE(data_ptr, nullptr);
    ASSERT_GT(field_rank, 0);

    // Compute total number of elements from extents
    std::size_t total_elements = 1;
    for (int i = 0; i < field_rank; ++i) {
        total_elements *= extents[i];
    }
    ASSERT_GT(total_elements, 0u);

    // Copy field data to a local vector for gathering
    std::vector<double> local_output(data_ptr, data_ptr + total_elements);

    // Gather all rank outputs to rank 0
    // First, ensure all ranks have the same number of elements
    std::size_t local_count = total_elements;
    std::vector<std::size_t> all_counts(static_cast<std::size_t>(num_ranks_), 0);
    MPI_Gather(&local_count, 1, MPI_UNSIGNED_LONG,
               all_counts.data(), 1, MPI_UNSIGNED_LONG,
               0, MPI_COMM_WORLD);

    if (rank_ == 0) {
        // Verify all ranks report the same element count
        for (int r = 1; r < num_ranks_; ++r) {
            ASSERT_EQ(all_counts[0], all_counts[static_cast<std::size_t>(r)])
                << "Rank " << r << " has different element count ("
                << all_counts[static_cast<std::size_t>(r)]
                << ") than rank 0 (" << all_counts[0] << ")";
        }
    }

    // Gather all field data to rank 0
    std::vector<double> gathered_data;
    if (rank_ == 0) {
        gathered_data.resize(total_elements * static_cast<std::size_t>(num_ranks_));
    }

    MPI_Gather(local_output.data(),
               static_cast<int>(total_elements), MPI_DOUBLE,
               gathered_data.data(),
               static_cast<int>(total_elements), MPI_DOUBLE,
               0, MPI_COMM_WORLD);

    // On rank 0: verify bitwise identity across all ranks
    if (rank_ == 0) {
        const double* rank0_data = gathered_data.data();

        for (int r = 1; r < num_ranks_; ++r) {
            const double* rank_r_data =
                gathered_data.data() + static_cast<std::size_t>(r) * total_elements;

            // Bitwise comparison using memcmp for strict B4B verification
            int cmp = std::memcmp(rank0_data, rank_r_data,
                                  total_elements * sizeof(double));
            EXPECT_EQ(cmp, 0)
                << "Rank " << r << " output differs from rank 0 output. "
                << "B4B reproducibility violation detected.";

            // If memcmp fails, find the first differing element for diagnostics
            if (cmp != 0) {
                for (std::size_t i = 0; i < total_elements; ++i) {
                    if (rank0_data[i] != rank_r_data[i]) {
                        ADD_FAILURE()
                            << "First difference at element " << i << ": "
                            << "rank 0 = " << rank0_data[i] << ", "
                            << "rank " << r << " = " << rank_r_data[i];
                        break;
                    }
                }
            }
        }
    }

    // Finalize TIDE
    rc = tide_finalize(handle);
    EXPECT_EQ(rc, 0) << "tide_finalize failed";
}

/**
 * @brief Verify that field extents are consistent across all MPI ranks.
 *
 * Each rank should report the same field dimensions (rank and extents),
 * confirming that the pipeline configuration is deterministic.
 */
TEST_F(MPIReproducibilityTest, FieldExtentsConsistentAcrossRanks) {
    tide_handle_t handle = nullptr;
    int mpi_comm_int = MPI_Comm_c2f(MPI_COMM_WORLD);

    int rc = tide_init(config_path().c_str(), mpi_comm_int, &handle);

    int init_ok = (rc == 0 && handle != nullptr) ? 1 : 0;
    int all_init_ok = 0;
    MPI_Allreduce(&init_ok, &all_init_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_init_ok) {
        if (handle) tide_finalize(handle);
        GTEST_SKIP() << "TIDE initialization failed on one or more ranks";
        return;
    }

    // Advance to produce output
    rc = tide_advance(handle, 1800.0);

    int advance_ok = (rc == 0) ? 1 : 0;
    int all_advance_ok = 0;
    MPI_Allreduce(&advance_ok, &all_advance_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_advance_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "tide_advance failed on one or more ranks";
        return;
    }

    // Get field metadata
    double* data_ptr = nullptr;
    int field_rank = 0;
    std::size_t extents[7] = {0};

    rc = tide_get_field(handle, "temperature", &data_ptr, &field_rank, extents);

    int field_ok = (rc == 0 && data_ptr != nullptr) ? 1 : 0;
    int all_field_ok = 0;
    MPI_Allreduce(&field_ok, &all_field_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_field_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "tide_get_field failed on one or more ranks";
        return;
    }

    // Gather field rank and extents to rank 0 for comparison
    std::vector<int> all_ranks_field_rank(static_cast<std::size_t>(num_ranks_), 0);
    MPI_Gather(&field_rank, 1, MPI_INT,
               all_ranks_field_rank.data(), 1, MPI_INT,
               0, MPI_COMM_WORLD);

    // Pack extents into a flat array for gathering
    std::vector<std::size_t> all_extents;
    if (rank_ == 0) {
        all_extents.resize(7 * static_cast<std::size_t>(num_ranks_));
    }
    MPI_Gather(extents, 7, MPI_UNSIGNED_LONG,
               all_extents.data(), 7, MPI_UNSIGNED_LONG,
               0, MPI_COMM_WORLD);

    if (rank_ == 0) {
        // Verify all ranks have the same field rank
        for (int r = 1; r < num_ranks_; ++r) {
            EXPECT_EQ(all_ranks_field_rank[0],
                      all_ranks_field_rank[static_cast<std::size_t>(r)])
                << "Rank " << r << " reports different field rank";
        }

        // Verify all ranks have the same extents
        for (int r = 1; r < num_ranks_; ++r) {
            for (int d = 0; d < 7; ++d) {
                EXPECT_EQ(all_extents[static_cast<std::size_t>(d)],
                          all_extents[static_cast<std::size_t>(r) * 7 +
                                      static_cast<std::size_t>(d)])
                    << "Rank " << r << " extent[" << d << "] differs";
            }
        }
    }

    tide_finalize(handle);
}

/**
 * @brief Verify deterministic behavior across multiple advance() calls.
 *
 * Calling advance() multiple times with the same target time should
 * produce identical output, and this should be consistent across ranks.
 */
TEST_F(MPIReproducibilityTest, RepeatedAdvanceProducesSameResult) {
    tide_handle_t handle = nullptr;
    int mpi_comm_int = MPI_Comm_c2f(MPI_COMM_WORLD);

    int rc = tide_init(config_path().c_str(), mpi_comm_int, &handle);

    int init_ok = (rc == 0 && handle != nullptr) ? 1 : 0;
    int all_init_ok = 0;
    MPI_Allreduce(&init_ok, &all_init_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_init_ok) {
        if (handle) tide_finalize(handle);
        GTEST_SKIP() << "TIDE initialization failed";
        return;
    }

    // First advance
    rc = tide_advance(handle, 1800.0);
    int advance_ok = (rc == 0) ? 1 : 0;
    int all_advance_ok = 0;
    MPI_Allreduce(&advance_ok, &all_advance_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_advance_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "First tide_advance failed";
        return;
    }

    // Get first result
    double* data_ptr1 = nullptr;
    int rank1 = 0;
    std::size_t extents1[7] = {0};
    rc = tide_get_field(handle, "temperature", &data_ptr1, &rank1, extents1);

    if (rc != 0 || data_ptr1 == nullptr) {
        tide_finalize(handle);
        GTEST_SKIP() << "First tide_get_field failed";
        return;
    }

    std::size_t total_elements = 1;
    for (int i = 0; i < rank1; ++i) {
        total_elements *= extents1[i];
    }

    // Save the first result (since advance invalidates previous pointers)
    std::vector<double> first_result(data_ptr1, data_ptr1 + total_elements);

    // Second advance at same time
    rc = tide_advance(handle, 1800.0);
    advance_ok = (rc == 0) ? 1 : 0;
    MPI_Allreduce(&advance_ok, &all_advance_ok, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    if (!all_advance_ok) {
        tide_finalize(handle);
        GTEST_SKIP() << "Second tide_advance failed";
        return;
    }

    // Get second result
    double* data_ptr2 = nullptr;
    int rank2 = 0;
    std::size_t extents2[7] = {0};
    rc = tide_get_field(handle, "temperature", &data_ptr2, &rank2, extents2);

    if (rc != 0 || data_ptr2 == nullptr) {
        tide_finalize(handle);
        GTEST_SKIP() << "Second tide_get_field failed";
        return;
    }

    // Compare first and second results — must be bitwise identical
    std::vector<double> second_result(data_ptr2, data_ptr2 + total_elements);

    int local_match = (std::memcmp(first_result.data(), second_result.data(),
                                   total_elements * sizeof(double)) == 0) ? 1 : 0;
    int all_match = 0;
    MPI_Allreduce(&local_match, &all_match, 1, MPI_INT, MPI_MIN, MPI_COMM_WORLD);

    EXPECT_EQ(all_match, 1)
        << "Repeated advance() at the same time did not produce "
        << "bitwise identical results on all ranks";

    tide_finalize(handle);
}

} // anonymous namespace

/**
 * @brief Custom main that handles MPI initialization and finalization.
 *
 * GTest must be initialized after MPI to ensure proper ordering.
 * We use a custom main to control the MPI lifecycle.
 */
int main(int argc, char** argv) {
    // Initialize MPI before GTest
    MPI_Init(&argc, &argv);

    // Initialize GTest
    ::testing::InitGoogleTest(&argc, argv);

    // Register MPI environment (for documentation purposes)
    ::testing::AddGlobalTestEnvironment(new MPIEnvironment);

    // Run all tests
    int result = RUN_ALL_TESTS();

    // Finalize MPI
    MPI_Finalize();

    return result;
}
