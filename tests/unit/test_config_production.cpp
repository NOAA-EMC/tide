/**
 * @file test_config_production.cpp
 * @brief Unit tests for production-readiness configuration parsing.
 *
 * Tests parsing of weight_file, prefetch, memory_budget_mb, enable_timers,
 * source_esmf_mesh, source_esmf_grid_spec, and target grid ESMF fields.
 *
 * Validates Requirements: 1.1, 5.6, 6.1
 */

#include <gtest/gtest.h>

#include <tide/config.hpp>
#include <tide/error.hpp>

#include <string>

namespace tide::config {
namespace {

// =============================================================================
// Helpers
// =============================================================================

/**
 * @brief Build a minimal valid YAML config with optional extra stream fields.
 *
 * @param extra_stream_fields Additional YAML lines to insert in the stream block.
 * @param extra_top_fields Additional YAML lines at the top (tide) level.
 * @param target_grids_block Optional target_grids YAML block.
 * @return Complete YAML string suitable for parse_yaml_string().
 */
std::string make_config(const std::string& extra_stream_fields = "",
                        const std::string& extra_top_fields = "",
                        const std::string& target_grids_block = "") {
    std::string yaml = "tide:\n";
    if (!extra_top_fields.empty()) {
        yaml += extra_top_fields;
    }
    yaml += "  streams:\n";
    yaml += "    - name: test_stream\n";
    yaml += "      file: /data/forcing.nc\n";
    yaml += "      field: temperature\n";
    yaml += "      source_grid_type: regular_latlon\n";
    yaml += "      target_grid: host_grid\n";
    if (!extra_stream_fields.empty()) {
        yaml += extra_stream_fields;
    }
    if (!target_grids_block.empty()) {
        yaml += target_grids_block;
    }
    return yaml;
}

/**
 * @brief Build a stream config that uses an ESMF source instead of source_grid_type.
 */
std::string make_esmf_source_config(const std::string& esmf_field,
                                    const std::string& esmf_value) {
    std::string yaml = "tide:\n";
    yaml += "  streams:\n";
    yaml += "    - name: esmf_stream\n";
    yaml += "      file: /data/forcing.nc\n";
    yaml += "      field: temperature\n";
    yaml += "      target_grid: host_grid\n";
    yaml += "      " + esmf_field + ": " + esmf_value + "\n";
    return yaml;
}

// =============================================================================
// Test: weight_file parsing in stream config
// =============================================================================

TEST(ConfigProductionTest, ParsesWeightFileInStream) {
    auto yaml = make_config("      weight_file: /weights/scrip_conservative.nc\n");
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->streams[0].weight_file, "/weights/scrip_conservative.nc");
}

TEST(ConfigProductionTest, WeightFileDefaultsToEmpty) {
    auto yaml = make_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->streams[0].weight_file.empty());
}

// =============================================================================
// Test: prefetch parsing
// =============================================================================

TEST(ConfigProductionTest, ParsesPrefetchEnabledAndDepth) {
    std::string extra =
        "      prefetch:\n"
        "        enabled: true\n"
        "        depth: 2\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->streams[0].prefetch.enabled);
    EXPECT_EQ(result->streams[0].prefetch.depth, 2);
}

TEST(ConfigProductionTest, PrefetchDefaultsToDisabled) {
    auto yaml = make_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->streams[0].prefetch.enabled);
    EXPECT_EQ(result->streams[0].prefetch.depth, 1);
}

TEST(ConfigProductionTest, PrefetchDepthZeroReturnsError) {
    std::string extra =
        "      prefetch:\n"
        "        enabled: true\n"
        "        depth: 0\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigInvalidValue));
    EXPECT_NE(result.error().message.find("prefetch.depth"), std::string::npos);
}

TEST(ConfigProductionTest, PrefetchDepthNegativeReturnsError) {
    std::string extra =
        "      prefetch:\n"
        "        enabled: true\n"
        "        depth: -3\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigInvalidValue));
    EXPECT_NE(result.error().message.find("prefetch.depth"), std::string::npos);
}

// =============================================================================
// Test: memory_budget_mb at top level
// =============================================================================

TEST(ConfigProductionTest, ParsesMemoryBudgetMb) {
    auto yaml = make_config("", "  memory_budget_mb: 4096\n");
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->memory_budget_mb, 4096u);
}

TEST(ConfigProductionTest, MemoryBudgetDefaultsToZero) {
    auto yaml = make_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->memory_budget_mb, 0u);
}

// =============================================================================
// Test: enable_timers at top level
// =============================================================================

TEST(ConfigProductionTest, ParsesEnableTimersFalse) {
    auto yaml = make_config("", "  enable_timers: false\n");
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_FALSE(result->enable_timers);
}

TEST(ConfigProductionTest, EnableTimersDefaultsToTrue) {
    auto yaml = make_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->enable_timers);
}

// =============================================================================
// Test: source_esmf_mesh and source_esmf_grid_spec in stream config
// =============================================================================

TEST(ConfigProductionTest, ParsesSourceEsmfMesh) {
    std::string extra = "      source_esmf_mesh: /grids/source_mesh.nc\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->streams[0].source_esmf_mesh, "/grids/source_mesh.nc");
}

TEST(ConfigProductionTest, ParsesSourceEsmfGridSpec) {
    std::string extra = "      source_esmf_grid_spec: /grids/source_gridspec.nc\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->streams[0].source_esmf_grid_spec, "/grids/source_gridspec.nc");
}

TEST(ConfigProductionTest, BothSourceEsmfMeshAndGridSpecReturnsError) {
    std::string extra =
        "      source_esmf_mesh: /grids/mesh.nc\n"
        "      source_esmf_grid_spec: /grids/gridspec.nc\n";
    auto yaml = make_config(extra);
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigInvalidValue));
    EXPECT_NE(result.error().message.find("source_esmf_mesh"), std::string::npos);
    EXPECT_NE(result.error().message.find("source_esmf_grid_spec"), std::string::npos);
}

// =============================================================================
// Test: target grid ESMF fields (esmf_mesh and esmf_grid_spec)
// =============================================================================

TEST(ConfigProductionTest, ParsesTargetGridEsmfMesh) {
    std::string target_grids =
        "  target_grids:\n"
        "    host_grid:\n"
        "      esmf_mesh: /grids/target_mesh.nc\n";
    auto yaml = make_config("", "", target_grids);
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->target_grids.count("host_grid"));
    EXPECT_EQ(result->target_grids.at("host_grid").esmf_mesh, "/grids/target_mesh.nc");
}

TEST(ConfigProductionTest, ParsesTargetGridEsmfGridSpec) {
    std::string target_grids =
        "  target_grids:\n"
        "    host_grid:\n"
        "      esmf_grid_spec: /grids/target_gridspec.nc\n";
    auto yaml = make_config("", "", target_grids);
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    ASSERT_TRUE(result->target_grids.count("host_grid"));
    EXPECT_EQ(result->target_grids.at("host_grid").esmf_grid_spec, "/grids/target_gridspec.nc");
}

TEST(ConfigProductionTest, BothTargetGridEsmfMeshAndGridSpecReturnsError) {
    std::string target_grids =
        "  target_grids:\n"
        "    host_grid:\n"
        "      esmf_mesh: /grids/mesh.nc\n"
        "      esmf_grid_spec: /grids/gridspec.nc\n";
    auto yaml = make_config("", "", target_grids);
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigInvalidValue));
    EXPECT_NE(result.error().message.find("esmf_mesh"), std::string::npos);
    EXPECT_NE(result.error().message.find("esmf_grid_spec"), std::string::npos);
}

// =============================================================================
// Test: source_grid_type not required when source_esmf_mesh is provided
// =============================================================================

TEST(ConfigProductionTest, MissingSourceGridTypeOkWithSourceEsmfMesh) {
    auto yaml = make_esmf_source_config("source_esmf_mesh", "/grids/source_mesh.nc");
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->streams[0].source_grid_type.empty());
    EXPECT_EQ(result->streams[0].source_esmf_mesh, "/grids/source_mesh.nc");
}

TEST(ConfigProductionTest, MissingSourceGridTypeOkWithSourceEsmfGridSpec) {
    auto yaml = make_esmf_source_config("source_esmf_grid_spec", "/grids/source_gridspec.nc");
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_TRUE(result->streams[0].source_grid_type.empty());
    EXPECT_EQ(result->streams[0].source_esmf_grid_spec, "/grids/source_gridspec.nc");
}

} // anonymous namespace
} // namespace tide::config
