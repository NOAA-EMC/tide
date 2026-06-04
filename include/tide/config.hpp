/**
 * @file config.hpp
 * @brief TIDE YAML configuration parsing and data structures.
 *
 * Defines the configuration types (StreamConfig, TideConfig, FieldScaling,
 * TemporalMode) and parsing functions for YAML-based stream configuration.
 *
 * The configuration system supports:
 * - Multiple forcing streams, each independently configured
 * - Per-stream grid type, interpolation method, temporal mode
 * - Per-field linear scaling (Y = MX + B)
 * - Vertical interpolation options (log-pressure, extrapolation limit)
 * - Validation with descriptive error messages identifying missing fields
 *
 * @see Requirements 8.1–8.7 in the TIDE specification
 */

#ifndef TIDE_CONFIG_HPP
#define TIDE_CONFIG_HPP

#include <expected>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "tide/error.hpp"
#include "tide/types.hpp"

namespace tide::config {

/**
 * @brief Per-field scaling configuration.
 *
 * Defines the linear transform Y = M*X + B applied to each element
 * of an output field. Defaults to identity transform (M=1, B=0).
 */
struct FieldScaling {
    double multiplier = 1.0; ///< M in Y = MX + B
    double offset = 0.0;     ///< B in Y = MX + B
};

/**
 * @brief Temporal interpolation mode for a stream.
 *
 * Determines how the temporal engine selects and interpolates between
 * bounding time levels in the forcing file.
 */
enum class TemporalMode {
    Linear,              ///< Standard linear interpolation between time levels
    Cyclical,            ///< Day-of-year cyclical matching (wraps at year boundary)
    SeasonalPreservation ///< Snap to closest available year, match DOY exactly
};

/**
 * @brief Async prefetch configuration for a stream.
 *
 * Controls whether the stream pre-reads upcoming time levels
 * asynchronously via AMIO, and how many levels to read ahead.
 */
struct PrefetchConfig {
    /// @brief Whether async prefetch is enabled for this stream.
    bool enabled = false;

    /// @brief Number of time levels to read ahead (default: 1).
    int depth = 1;
};

/**
 * @brief Per-stream configuration.
 *
 * Holds all user-configurable parameters for a single forcing stream,
 * including file path, field mapping, grid types, interpolation options,
 * scaling, and temporal/vertical settings.
 */
struct StreamConfig {
    /// @brief User-assigned stream name (used in error messages).
    std::string name;

    /// @brief Path to the input forcing file (NetCDF or GRIB2).
    std::filesystem::path file_path;

    /// @brief Variable name of the field within the file.
    std::string field_name;

    /// @brief Source grid type identifier.
    ///
    /// One of: "regular_latlon", "gaussian", "reduced_gaussian",
    /// "curvilinear", "unstructured", "point_cloud".
    std::string source_grid_type;

    /// @brief Reference name for the target grid definition.
    std::string target_grid_ref;

    /// @brief Optional linear scaling parameters (default: identity).
    FieldScaling scaling;

    /// @brief Temporal interpolation mode (default: Linear).
    TemporalMode temporal_mode = TemporalMode::Linear;

    /// @brief Horizontal interpolation method string.
    ///
    /// One of: "finite-element", "k-nearest-neighbours", "nearest-neighbour",
    /// "structured-linear2D", "structured-cubic2D", "structured-quasicubic2D",
    /// "conservative". Defaults to "finite-element".
    std::string interp_method = "finite-element";

    /// @brief Missing data treatment mode string.
    ///
    /// One of: "missing-if-any-missing", "missing-if-all-missing",
    /// "missing-if-heaviest-missing". Defaults to "missing-if-heaviest-missing".
    std::string missing_data_mode = "missing-if-heaviest-missing";

    /// @brief Vertical extrapolation limit (-1 = default: one source interval).
    double extrap_limit = -1.0;

    /// @brief Perform vertical interpolation in ln(P) space for pressure coords.
    bool log_pressure = false;

    /// @brief Async prefetch configuration (optional, default: disabled).
    PrefetchConfig prefetch;

    /// @brief Optional path to a pre-computed SCRIP weight file.
    ///
    /// When specified, the stream uses this weight file for spatial regridding
    /// instead of computing Atlas weights at runtime.
    std::filesystem::path weight_file;

    /// @brief Optional path to an ESMF mesh file describing the source grid.
    std::filesystem::path source_esmf_mesh;

    /// @brief Optional path to an ESMF grid spec file describing the source grid.
    std::filesystem::path source_esmf_grid_spec;
};

/**
 * @brief Top-level TIDE configuration.
 *
 * Contains all stream configurations and shared target grid definitions
 * parsed from a YAML configuration file.
 */
struct TideConfig {
    /// @brief All configured forcing streams.
    std::vector<StreamConfig> streams;

    /// @brief Named target grid definitions (shared across streams).
    std::unordered_map<std::string, TargetGrid> target_grids;

    /// @brief Optional memory budget in megabytes (0 = unlimited).
    ///
    /// When set to a positive value, TIDE enforces a maximum memory allocation
    /// for pipeline buffers across all streams. Initialization fails if the
    /// computed total buffer requirement exceeds this limit.
    std::size_t memory_budget_mb = 0;

    /// @brief Whether per-stage performance timers are enabled (default: true).
    ///
    /// When disabled, timer measurement is skipped entirely for minimal
    /// overhead in production runs.
    bool enable_timers = true;
};

/**
 * @brief Parse a YAML configuration file from disk.
 *
 * Opens the file at @p path, parses it as YAML, validates all required
 * fields, and returns a populated TideConfig or an Error.
 *
 * @param path Filesystem path to the YAML configuration file.
 * @return Parsed TideConfig on success, or Error with:
 *         - ConfigFileNotFound (code 100) if path does not exist
 *         - ConfigParseError (code 101) with line/column for invalid YAML
 *         - ConfigMissingField (code 102) identifying the missing field and stream
 *         - ConfigInvalidValue (code 103) for unrecognized enum values
 *
 * @see Requirement 8.1 (YAML file or programmatic structure)
 * @see Requirement 8.5 (missing field error with identification)
 * @see Requirement 8.6 (parse failure with location)
 */
auto parse_yaml(const std::filesystem::path& path)
    -> std::expected<TideConfig, Error>;

/**
 * @brief Parse YAML configuration from an in-memory string.
 *
 * Parses @p yaml_content as YAML, validates all required fields, and
 * returns a populated TideConfig or an Error.
 *
 * @param yaml_content String containing valid YAML configuration.
 * @return Parsed TideConfig on success, or Error with location/field info.
 *
 * @see Requirement 8.1 (equivalent programmatic in-memory structure)
 */
auto parse_yaml_string(std::string_view yaml_content)
    -> std::expected<TideConfig, Error>;

} // namespace tide::config

#endif // TIDE_CONFIG_HPP
