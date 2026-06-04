/**
 * @file config.cpp
 * @brief Implementation of the TIDE YAML configuration parser.
 *
 * Parses YAML configuration files (or in-memory strings) into TideConfig
 * structures using yaml-cpp. Validates required fields per-stream and
 * returns descriptive errors identifying missing fields, invalid values,
 * or parse failures with line/column location.
 *
 * @see Requirements 8.1–8.7
 */

#include "tide/config.hpp"

#include <fstream>
#include <sstream>
#include <string>

#include <yaml-cpp/yaml.h>

namespace tide::config {

namespace {

/// @brief Valid source grid type values.
constexpr std::string_view kValidGridTypes[] = {
    "regular_latlon", "gaussian", "reduced_gaussian",
    "curvilinear",    "unstructured", "point_cloud"};

/// @brief Valid temporal mode values.
constexpr std::string_view kValidTemporalModes[] = {
    "linear", "cyclical", "seasonal_preservation"};

/// @brief Valid interpolation method values.
constexpr std::string_view kValidInterpMethods[] = {
    "finite-element",        "k-nearest-neighbours",
    "nearest-neighbour",     "structured-linear2D",
    "structured-cubic2D",    "structured-quasicubic2D",
    "conservative"};

/// @brief Valid missing data mode values.
constexpr std::string_view kValidMissingModes[] = {
    "missing-if-any-missing", "missing-if-all-missing",
    "missing-if-heaviest-missing"};

/**
 * @brief Check if a string is in a list of valid values.
 */
template <std::size_t N>
bool is_valid_value(std::string_view value,
                    const std::string_view (&valid)[N]) {
    for (const auto& v : valid) {
        if (value == v) return true;
    }
    return false;
}

/**
 * @brief Convert a temporal mode string to the TemporalMode enum.
 *
 * @param mode_str The string representation (e.g., "linear", "cyclical").
 * @return The corresponding TemporalMode enum value.
 *
 * @pre mode_str has already been validated via is_valid_value().
 */
TemporalMode parse_temporal_mode(std::string_view mode_str) {
    if (mode_str == "cyclical") return TemporalMode::Cyclical;
    if (mode_str == "seasonal_preservation") return TemporalMode::SeasonalPreservation;
    return TemporalMode::Linear;
}

/**
 * @brief Build a comma-separated list of valid values for error messages.
 */
template <std::size_t N>
std::string valid_values_string(const std::string_view (&valid)[N]) {
    std::string result;
    for (std::size_t i = 0; i < N; ++i) {
        if (i > 0) result += ", ";
        result += valid[i];
    }
    return result;
}

/**
 * @brief Create a missing-field error with stream name and field identification.
 */
Error make_missing_field_error(std::string_view stream_name,
                               std::string_view field_name) {
    std::string msg = "Missing required field '";
    msg += field_name;
    msg += "' in stream '";
    msg += stream_name;
    msg += "'";
    return Error{
        .code = to_int(ErrorCode::ConfigMissingField),
        .message = std::move(msg),
        .context = "config"};
}

/**
 * @brief Create an invalid-value error with stream name and field identification.
 */
Error make_invalid_value_error(std::string_view stream_name,
                               std::string_view field_name,
                               std::string_view value,
                               std::string_view valid_options) {
    std::string msg = "Invalid value '";
    msg += value;
    msg += "' for field '";
    msg += field_name;
    msg += "' in stream '";
    msg += stream_name;
    msg += "'. Valid values: ";
    msg += valid_options;
    return Error{
        .code = to_int(ErrorCode::ConfigInvalidValue),
        .message = std::move(msg),
        .context = "config"};
}

/**
 * @brief Create a parse error with line/column location.
 */
Error make_parse_error(const YAML::ParserException& ex) {
    std::string msg = "YAML parse error at line ";
    msg += std::to_string(ex.mark.line + 1);
    msg += ", column ";
    msg += std::to_string(ex.mark.column + 1);
    msg += ": ";
    msg += ex.msg;
    return Error{
        .code = to_int(ErrorCode::ConfigParseError),
        .message = std::move(msg),
        .context = "config"};
}

/**
 * @brief Parse a single stream node from the YAML configuration.
 *
 * Validates all required fields and optional fields, returning
 * a StreamConfig on success or an Error on validation failure.
 *
 * @param node The YAML node representing a single stream.
 * @param index The zero-based index of the stream (for fallback naming).
 * @return StreamConfig or Error.
 */
auto parse_stream(const YAML::Node& node, std::size_t index)
    -> std::expected<StreamConfig, Error> {

    StreamConfig config;

    // Extract stream name (optional, but used in error messages)
    if (node["name"]) {
        config.name = node["name"].as<std::string>();
    } else {
        config.name = "stream_" + std::to_string(index);
    }

    // --- Required fields ---

    // file_path (mapped from "file" key in YAML)
    if (!node["file"]) {
        return std::unexpected(
            make_missing_field_error(config.name, "file"));
    }
    config.file_path = node["file"].as<std::string>();

    // field_name (mapped from "field" key in YAML)
    if (!node["field"]) {
        return std::unexpected(
            make_missing_field_error(config.name, "field"));
    }
    config.field_name = node["field"].as<std::string>();

    // source_grid_type
    if (!node["source_grid_type"]) {
        // source_grid_type is required unless an ESMF source grid is specified
        // (checked after ESMF fields are parsed below)
        config.source_grid_type = "";
    } else {
        config.source_grid_type = node["source_grid_type"].as<std::string>();
        if (!is_valid_value(config.source_grid_type, kValidGridTypes)) {
            return std::unexpected(make_invalid_value_error(
                config.name, "source_grid_type", config.source_grid_type,
                valid_values_string(kValidGridTypes)));
        }
    }

    // target_grid (mapped from "target_grid" key)
    if (!node["target_grid"]) {
        return std::unexpected(
            make_missing_field_error(config.name, "target_grid"));
    }
    config.target_grid_ref = node["target_grid"].as<std::string>();

    // --- Optional fields ---

    // temporal_mode (default: Linear per Requirement 8.7)
    if (node["temporal_mode"]) {
        auto mode_str = node["temporal_mode"].as<std::string>();
        if (!is_valid_value(mode_str, kValidTemporalModes)) {
            return std::unexpected(make_invalid_value_error(
                config.name, "temporal_mode", mode_str,
                valid_values_string(kValidTemporalModes)));
        }
        config.temporal_mode = parse_temporal_mode(mode_str);
    }

    // interp_method (default: "finite-element")
    if (node["interp_method"]) {
        config.interp_method = node["interp_method"].as<std::string>();
        if (!is_valid_value(config.interp_method, kValidInterpMethods)) {
            return std::unexpected(make_invalid_value_error(
                config.name, "interp_method", config.interp_method,
                valid_values_string(kValidInterpMethods)));
        }
    }

    // missing_data_mode (default: "missing-if-heaviest-missing")
    if (node["missing_data_mode"]) {
        config.missing_data_mode = node["missing_data_mode"].as<std::string>();
        if (!is_valid_value(config.missing_data_mode, kValidMissingModes)) {
            return std::unexpected(make_invalid_value_error(
                config.name, "missing_data_mode", config.missing_data_mode,
                valid_values_string(kValidMissingModes)));
        }
    }

    // scaling (optional sub-object)
    if (node["scaling"]) {
        auto scaling_node = node["scaling"];
        if (scaling_node["multiplier"]) {
            config.scaling.multiplier = scaling_node["multiplier"].as<double>();
        }
        if (scaling_node["offset"]) {
            config.scaling.offset = scaling_node["offset"].as<double>();
        }
    }

    // vertical interpolation options (optional sub-object or top-level)
    if (node["vertical"]) {
        auto vert_node = node["vertical"];
        if (vert_node["extrap_limit"]) {
            config.extrap_limit = vert_node["extrap_limit"].as<double>();
        }
        if (vert_node["log_pressure"]) {
            config.log_pressure = vert_node["log_pressure"].as<bool>();
        }
    }
    // Also support top-level log_pressure for convenience
    if (node["log_pressure"]) {
        config.log_pressure = node["log_pressure"].as<bool>();
    }

    // --- New production-readiness fields ---

    // prefetch sub-section (optional)
    if (node["prefetch"]) {
        auto prefetch_node = node["prefetch"];
        if (prefetch_node["enabled"]) {
            config.prefetch.enabled = prefetch_node["enabled"].as<bool>();
        }
        if (prefetch_node["depth"]) {
            config.prefetch.depth = prefetch_node["depth"].as<int>();
            if (config.prefetch.depth < 1) {
                return std::unexpected(make_invalid_value_error(
                    config.name, "prefetch.depth",
                    std::to_string(config.prefetch.depth),
                    "positive integer (>= 1)"));
            }
        }
    }

    // weight_file (optional SCRIP weight path)
    if (node["weight_file"]) {
        config.weight_file = node["weight_file"].as<std::string>();
    }

    // source_esmf_mesh (optional ESMF mesh file for source grid)
    if (node["source_esmf_mesh"]) {
        config.source_esmf_mesh = node["source_esmf_mesh"].as<std::string>();
    }

    // source_esmf_grid_spec (optional ESMF grid spec file for source grid)
    if (node["source_esmf_grid_spec"]) {
        config.source_esmf_grid_spec = node["source_esmf_grid_spec"].as<std::string>();
    }

    // Validate: cannot specify both source_esmf_mesh and source_esmf_grid_spec
    if (!config.source_esmf_mesh.empty() && !config.source_esmf_grid_spec.empty()) {
        return std::unexpected(Error{
            .code = to_int(ErrorCode::ConfigInvalidValue),
            .message = "Stream '" + config.name +
                       "' specifies both 'source_esmf_mesh' and 'source_esmf_grid_spec'; "
                       "only one ESMF source grid type is allowed",
            .context = "config"});
    }

    // source_grid_type is required when no ESMF source grid is specified
    if (config.source_grid_type.empty() &&
        config.source_esmf_mesh.empty() &&
        config.source_esmf_grid_spec.empty()) {
        return std::unexpected(
            make_missing_field_error(config.name, "source_grid_type"));
    }

    return config;
}

/**
 * @brief Parse the YAML root node into a TideConfig.
 *
 * Expects a top-level "tide" key with "streams" array and optional
 * "target_grids" map.
 *
 * @param root The parsed YAML root node.
 * @return TideConfig or Error.
 */
auto parse_root(const YAML::Node& root) -> std::expected<TideConfig, Error> {
    TideConfig config;

    // The root should contain a "tide" key
    YAML::Node tide_node;
    if (root["tide"]) {
        tide_node = root["tide"];
    } else {
        // Allow root-level streams for simpler configs
        tide_node = root;
    }

    // --- New top-level production-readiness fields ---

    // memory_budget_mb (optional, default 0 = unlimited)
    if (tide_node["memory_budget_mb"]) {
        config.memory_budget_mb = tide_node["memory_budget_mb"].as<std::size_t>();
    }

    // enable_timers (optional, default true)
    if (tide_node["enable_timers"]) {
        config.enable_timers = tide_node["enable_timers"].as<bool>();
    }

    // Parse streams
    if (!tide_node["streams"]) {
        return std::unexpected(Error{
            .code = to_int(ErrorCode::ConfigMissingField),
            .message = "Missing required 'streams' section in configuration",
            .context = "config"});
    }

    auto streams_node = tide_node["streams"];
    if (!streams_node.IsSequence()) {
        return std::unexpected(Error{
            .code = to_int(ErrorCode::ConfigInvalidValue),
            .message = "'streams' must be a YAML sequence (array)",
            .context = "config"});
    }

    for (std::size_t i = 0; i < streams_node.size(); ++i) {
        auto result = parse_stream(streams_node[i], i);
        if (!result) {
            return std::unexpected(result.error());
        }
        config.streams.push_back(std::move(*result));
    }

    // Parse target_grids (optional section)
    if (tide_node["target_grids"]) {
        auto grids_node = tide_node["target_grids"];
        if (grids_node.IsMap()) {
            for (auto it = grids_node.begin(); it != grids_node.end(); ++it) {
                auto grid_name = it->first.as<std::string>();
                TargetGrid grid;
                auto grid_node = it->second;

                // --- New: ESMF mesh-based target grid ---
                if (grid_node["esmf_mesh"]) {
                    grid.esmf_mesh = grid_node["esmf_mesh"].as<std::string>();
                }

                // --- New: ESMF grid spec-based target grid ---
                if (grid_node["esmf_grid_spec"]) {
                    grid.esmf_grid_spec = grid_node["esmf_grid_spec"].as<std::string>();
                }

                // Validate: cannot specify both esmf_mesh and esmf_grid_spec
                if (!grid.esmf_mesh.empty() && !grid.esmf_grid_spec.empty()) {
                    return std::unexpected(Error{
                        .code = to_int(ErrorCode::ConfigInvalidValue),
                        .message = "Target grid '" + grid_name +
                                   "' specifies both 'esmf_mesh' and 'esmf_grid_spec'; "
                                   "only one ESMF grid type is allowed",
                        .context = "config"});
                }

                if (grid_node["num_cols"]) {
                    grid.num_cols = grid_node["num_cols"].as<std::size_t>();
                }
                if (grid_node["num_levels"]) {
                    grid.num_levels = grid_node["num_levels"].as<std::size_t>();
                }
                if (grid_node["level_units"]) {
                    grid.level_units = grid_node["level_units"].as<std::string>();
                }
                // Parse coordinate arrays if provided
                if (grid_node["lats"] && grid_node["lats"].IsSequence()) {
                    for (const auto& v : grid_node["lats"]) {
                        grid.lats.push_back(v.as<double>());
                    }
                }
                if (grid_node["lons"] && grid_node["lons"].IsSequence()) {
                    for (const auto& v : grid_node["lons"]) {
                        grid.lons.push_back(v.as<double>());
                    }
                }
                if (grid_node["levels"] && grid_node["levels"].IsSequence()) {
                    for (const auto& v : grid_node["levels"]) {
                        grid.levels.push_back(v.as<double>());
                    }
                }
                // For regular_latlon grids, the lats and lons arrays are 1D
                // axis arrays. Expand them into the full set of (lat, lon) point
                // pairs by computing the Cartesian product: every lat paired
                // with every lon.
                std::string grid_type;
                if (grid_node["type"]) {
                    grid_type = grid_node["type"].as<std::string>();
                }
                if (grid_type == "regular_latlon" &&
                    !grid.lats.empty() && !grid.lons.empty()) {
                    std::vector<double> axis_lats = grid.lats;
                    std::vector<double> axis_lons = grid.lons;
                    grid.lats.clear();
                    grid.lons.clear();
                    grid.lats.reserve(axis_lats.size() * axis_lons.size());
                    grid.lons.reserve(axis_lats.size() * axis_lons.size());
                    for (const auto& lat : axis_lats) {
                        for (const auto& lon : axis_lons) {
                            grid.lats.push_back(lat);
                            grid.lons.push_back(lon);
                        }
                    }
                }
                // Derive num_cols from coordinate arrays
                if (grid.num_cols == 0 && !grid.lats.empty()) {
                    grid.num_cols = grid.lats.size();
                }
                // Derive num_levels from levels array if not explicitly set
                if (grid.num_levels == 0 && !grid.levels.empty()) {
                    grid.num_levels = grid.levels.size();
                }
                config.target_grids[grid_name] = std::move(grid);
            }
        }
    }

    return config;
}

} // anonymous namespace

auto parse_yaml(const std::filesystem::path& path)
    -> std::expected<TideConfig, Error> {

    // Check if file exists
    if (!std::filesystem::exists(path)) {
        std::string msg = "Configuration file not found: ";
        msg += path.string();
        return std::unexpected(Error{
            .code = to_int(ErrorCode::ConfigFileNotFound),
            .message = std::move(msg),
            .context = "config"});
    }

    // Attempt to parse the YAML file
    YAML::Node root;
    try {
        root = YAML::LoadFile(path.string());
    } catch (const YAML::ParserException& ex) {
        return std::unexpected(make_parse_error(ex));
    } catch (const YAML::BadFile& ex) {
        std::string msg = "Cannot read configuration file: ";
        msg += path.string();
        return std::unexpected(Error{
            .code = to_int(ErrorCode::ConfigFileNotFound),
            .message = std::move(msg),
            .context = "config"});
    }

    return parse_root(root);
}

auto parse_yaml_string(std::string_view yaml_content)
    -> std::expected<TideConfig, Error> {

    YAML::Node root;
    try {
        root = YAML::Load(std::string(yaml_content));
    } catch (const YAML::ParserException& ex) {
        return std::unexpected(make_parse_error(ex));
    }

    return parse_root(root);
}

} // namespace tide::config
