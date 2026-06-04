/**
 * @file test_config.cpp
 * @brief Property-based tests for the TIDE configuration parser.
 *
 * Validates Requirements: 8.3, 8.5, 8.6
 */

#include <gtest/gtest.h>
#include <rapidcheck.h>
#include <rapidcheck/gtest.h>

#include <tide/config.hpp>
#include <tide/error.hpp>

#include <string>
#include <vector>

namespace tide::config {
namespace {

// =============================================================================
// Helpers: valid YAML generation
// =============================================================================

/// @brief Valid source grid type values for generating test configs.
static const std::vector<std::string> kGridTypes = {
    "regular_latlon", "gaussian", "reduced_gaussian",
    "curvilinear", "unstructured", "point_cloud"};

/**
 * @brief Build a complete valid YAML stream entry with all required fields.
 *
 * @param name Stream name
 * @param file File path value
 * @param field Field name value
 * @param grid_type Source grid type value
 * @param target_grid Target grid reference value
 * @return YAML string for a single stream entry
 */
std::string make_stream_yaml(const std::string& name,
                             const std::string& file,
                             const std::string& field,
                             const std::string& grid_type,
                             const std::string& target_grid) {
    std::string yaml;
    yaml += "    - name: \"" + name + "\"\n";
    yaml += "      file: \"" + file + "\"\n";
    yaml += "      field: \"" + field + "\"\n";
    yaml += "      source_grid_type: \"" + grid_type + "\"\n";
    yaml += "      target_grid: \"" + target_grid + "\"\n";
    return yaml;
}

/**
 * @brief Build a complete valid YAML config string with one stream.
 */
std::string make_valid_config(const std::string& name = "test_stream",
                              const std::string& file = "/data/file.nc",
                              const std::string& field = "temperature",
                              const std::string& grid_type = "regular_latlon",
                              const std::string& target_grid = "host_grid") {
    std::string yaml = "tide:\n  streams:\n";
    yaml += make_stream_yaml(name, file, field, grid_type, target_grid);
    return yaml;
}

/**
 * @brief Build a YAML config with one required field removed from the stream.
 *
 * @param field_index Which required field to remove (0-3):
 *   0 = file, 1 = field, 2 = source_grid_type, 3 = target_grid
 * @param stream_name The stream name to use (appears in error messages)
 * @return Pair of (YAML string, removed field name)
 */
std::pair<std::string, std::string> make_config_missing_field(
    int field_index, const std::string& stream_name) {

    // Required field names (as they appear in the YAML keys)
    static const std::vector<std::string> kFieldNames = {
        "file", "field", "source_grid_type", "target_grid"};

    std::string yaml = "tide:\n  streams:\n";
    yaml += "    - name: \"" + stream_name + "\"\n";

    // Add all required fields except the one at field_index
    if (field_index != 0) yaml += "      file: \"/data/test.nc\"\n";
    if (field_index != 1) yaml += "      field: \"T\"\n";
    if (field_index != 2) yaml += "      source_grid_type: \"regular_latlon\"\n";
    if (field_index != 3) yaml += "      target_grid: \"host_grid\"\n";

    return {yaml, kFieldNames[field_index]};
}

// =============================================================================
// Unit tests — baseline correctness
// =============================================================================

TEST(ConfigParserTest, ValidConfigParses) {
    auto yaml = make_valid_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value()) << result.error().message;
    EXPECT_EQ(result->streams.size(), 1);
    EXPECT_EQ(result->streams[0].name, "test_stream");
    EXPECT_EQ(result->streams[0].file_path, "/data/file.nc");
    EXPECT_EQ(result->streams[0].field_name, "temperature");
    EXPECT_EQ(result->streams[0].source_grid_type, "regular_latlon");
    EXPECT_EQ(result->streams[0].target_grid_ref, "host_grid");
}

TEST(ConfigParserTest, MissingFileFieldReturnsError) {
    auto [yaml, _] = make_config_missing_field(0, "my_stream");
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigMissingField));
    EXPECT_NE(result.error().message.find("file"), std::string::npos);
    EXPECT_NE(result.error().message.find("my_stream"), std::string::npos);
}

TEST(ConfigParserTest, MissingFieldFieldReturnsError) {
    auto [yaml, _] = make_config_missing_field(1, "my_stream");
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigMissingField));
    EXPECT_NE(result.error().message.find("field"), std::string::npos);
    EXPECT_NE(result.error().message.find("my_stream"), std::string::npos);
}

TEST(ConfigParserTest, MissingSourceGridTypeReturnsError) {
    auto [yaml, _] = make_config_missing_field(2, "my_stream");
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigMissingField));
    EXPECT_NE(result.error().message.find("source_grid_type"), std::string::npos);
    EXPECT_NE(result.error().message.find("my_stream"), std::string::npos);
}

TEST(ConfigParserTest, MissingTargetGridReturnsError) {
    auto [yaml, _] = make_config_missing_field(3, "my_stream");
    auto result = parse_yaml_string(yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigMissingField));
    EXPECT_NE(result.error().message.find("target_grid"), std::string::npos);
    EXPECT_NE(result.error().message.find("my_stream"), std::string::npos);
}

TEST(ConfigParserTest, InvalidYamlReturnsParseError) {
    // Tabs are invalid in YAML, and unmatched braces cause parse failures
    std::string invalid_yaml = "tide:\n  streams:\n    - [\n";
    auto result = parse_yaml_string(invalid_yaml);
    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error().code, to_int(ErrorCode::ConfigParseError));
    // Should contain line/column location info
    EXPECT_NE(result.error().message.find("line"), std::string::npos);
    EXPECT_NE(result.error().message.find("column"), std::string::npos);
}

TEST(ConfigParserTest, DefaultTemporalModeIsLinear) {
    auto yaml = make_valid_config();
    auto result = parse_yaml_string(yaml);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->streams[0].temporal_mode, TemporalMode::Linear);
}

// =============================================================================
// Property-based tests (RapidCheck)
// =============================================================================

/**
 * **Validates: Requirements 8.3, 8.5**
 *
 * Property 19: Missing required config field produces identifying error.
 *
 * For any stream configuration with exactly one required field
 * (file, field, source_grid_type, or target_grid) removed,
 * the configuration parser SHALL return an error whose code is
 * ConfigMissingField (102) and whose message contains both the
 * missing field name and the stream name.
 */
RC_GTEST_PROP(ConfigProperty, MissingRequiredFieldProducesIdentifyingError, ()) {
    // Pick which required field to remove (0=file, 1=field, 2=source_grid_type, 3=target_grid)
    const int field_index = *rc::gen::inRange(0, 4);

    // Generate a non-empty stream name (alphanumeric, 1-20 chars)
    const auto stream_name = *rc::gen::nonEmpty(
        rc::gen::container<std::string>(
            rc::gen::map(rc::gen::inRange(0, 37), [](int idx) -> char {
                static constexpr char chars[] =
                    "abcdefghijklmnopqrstuvwxyz0123456789_";
                return chars[idx];
            })));

    // Build config YAML with one required field missing
    auto [yaml, missing_field_name] = make_config_missing_field(field_index, stream_name);

    // Parse should fail
    auto result = parse_yaml_string(yaml);
    RC_ASSERT(!result.has_value());

    // Error code must be ConfigMissingField (102)
    RC_ASSERT(result.error().code == to_int(ErrorCode::ConfigMissingField));

    // Error message must contain the missing field name
    RC_ASSERT(result.error().message.find(missing_field_name) != std::string::npos);

    // Error message must contain the stream name
    RC_ASSERT(result.error().message.find(stream_name) != std::string::npos);
}

/**
 * **Validates: Requirements 8.6**
 *
 * Property 20: Invalid YAML produces parse error with location.
 *
 * For any syntactically invalid YAML input, the configuration parser
 * SHALL return a non-zero error code (ConfigParseError = 101) and an
 * error message containing line and column location information.
 */
RC_GTEST_PROP(ConfigProperty, InvalidYamlProducesParseErrorWithLocation, ()) {
    // Strategy: generate syntactically invalid YAML by injecting syntax errors.
    // We use several known-invalid patterns that yaml-cpp will reject.
    const int pattern = *rc::gen::inRange(0, 5);

    std::string invalid_yaml;
    switch (pattern) {
        case 0:
            // Unmatched bracket in a flow sequence
            invalid_yaml = "tide:\n  streams:\n    - [\n";
            break;
        case 1:
            // Unmatched brace in a flow mapping
            invalid_yaml = "tide:\n  streams:\n    - {\n";
            break;
        case 2:
            // Invalid indentation with tab characters mixed with spaces
            // (yaml-cpp treats this as an error for certain constructions)
            invalid_yaml = "tide:\n  streams:\n    - name: x\n\t\tfile: bad\n";
            break;
        case 3:
            // Duplicate key indicator that creates a parse error
            invalid_yaml = ":\n:\n- [\n";
            break;
        case 4:
            // A document marker followed by invalid content
            invalid_yaml = "---\n*invalid_alias\n";
            break;
    }

    auto result = parse_yaml_string(invalid_yaml);

    // Must fail with a parse error
    RC_ASSERT(!result.has_value());
    RC_ASSERT(result.error().code == to_int(ErrorCode::ConfigParseError));

    // Error message must contain line and column location information
    const auto& msg = result.error().message;
    RC_ASSERT(msg.find("line") != std::string::npos);
    RC_ASSERT(msg.find("column") != std::string::npos);
}

} // anonymous namespace
} // namespace tide::config
