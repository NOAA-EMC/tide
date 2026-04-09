/**
 * @file tide_yaml_c.cpp
 * @brief C++ implementation of the TIDE YAML parser using yaml-cpp.
 */

#include <yaml-cpp/yaml.h>

#include <cstring>
#include <iostream>
#include <string>
#include <vector>

extern "C" {
/**
 * @struct tide_output_config_t
 * @brief Configuration for output/regridding of a single TIDE stream.
 */
typedef struct {
    char* target_grid_file;   // Path to SCRIP/GRIDSPEC file
    char* output_file;        // Path to output NetCDF file
    int   output_frequency;   // Write interval in seconds
    char* regrid_method;      // "bilinear", "neareststod", "nearestdtos", "conserve"
    char** output_fields;     // List of field names to output
    int   num_output_fields;  // Length of output_fields array
    int   output_enabled;     // 1 if output block present, 0 otherwise
} tide_output_config_t;

/**
 * @struct tide_stream_config_t
 * @brief Configuration for a single TIDE data stream.
 */
typedef struct {
    char* name;
    char* mesh_file;
    char* lev_dimname;
    char* tax_mode;
    char* time_interp;
    char* map_algo;
    char* read_mode;
    double dt_limit;
    int year_first;
    int year_last;
    int year_align;
    int offset;
    char** input_files;
    int num_files;
    char** file_vars;
    char** model_vars;
    int num_fields;
    char* cf_detection_mode;
    int cf_cache_enabled;
    int cf_log_level;
    tide_output_config_t output;
} tide_stream_config_t;

/**
 * @struct tide_config_t
 * @brief Top-level configuration containing multiple TIDE streams.
 */
typedef struct {
    tide_stream_config_t* streams;
    int num_streams;
} tide_config_t;

/**
 * @brief Forward declaration for tide_free_config (used in error paths during parsing).
 */
void tide_free_config(tide_config_t* cfg);

/**
 * @brief Parses a TIDE YAML configuration file.
 * @param filename The path to the YAML file.
 * @return A pointer to a tide_config_t structure, or nullptr on failure.
 */
tide_config_t* tide_parse_yaml(const char* filename) {
    try {
        // Load YAML file
        YAML::Node config = YAML::LoadFile(filename);

        // Ensure streams key exists
        if (!config["streams"]) {
            std::cerr << "ERROR: [TIDE] 'streams' key missing from YAML configuration file: "
                      << filename << std::endl;
            return nullptr;
        }

        auto streams_node = config["streams"];
        int num_streams = streams_node.size();

        tide_config_t* cfg = new tide_config_t();
        cfg->num_streams = num_streams;
        cfg->streams = new tide_stream_config_t[num_streams];

        // Parse each stream configuration
        for (int i = 0; i < num_streams; ++i) {
            auto s = streams_node[i];
            tide_stream_config_t& sc = cfg->streams[i];

            // Mandatory string attributes (no default)
            sc.name = strdup(s["name"].as<std::string>().c_str());

            // Optional attributes with defaults
            if (s["lev_dimname"]) {
                std::cout << "DEBUG: lev_dimname: " << s["lev_dimname"].as<std::string>()
                          << std::endl;
            }

            if (s["mesh_file"]) {
                sc.mesh_file = strdup(s["mesh_file"].as<std::string>().c_str());
            } else {
                sc.mesh_file = strdup("none");
            }

            sc.lev_dimname = s["lev_dimname"] ? strdup(s["lev_dimname"].as<std::string>().c_str())
                                              : strdup("null");
            sc.tax_mode =
                s["tax_mode"] ? strdup(s["tax_mode"].as<std::string>().c_str()) : strdup("cycle");
            sc.time_interp = s["time_interp"] ? strdup(s["time_interp"].as<std::string>().c_str())
                                              : strdup("linear");
            sc.map_algo = s["map_algo"] ? strdup(s["map_algo"].as<std::string>().c_str())
                                        : strdup("bilinear");
            sc.read_mode = s["read_mode"] ? strdup(s["read_mode"].as<std::string>().c_str())
                                          : strdup("single");
            sc.dt_limit = s["dt_limit"] ? s["dt_limit"].as<double>() : 1.5;
            sc.year_first = s["year_first"].as<int>();
            sc.year_last = s["year_last"].as<int>();
            sc.year_align = s["year_align"].as<int>();
            sc.offset = s["offset"] ? s["offset"].as<int>() : 0;

            // Parse input files list
            auto files = s["input_files"];
            sc.num_files = files.size();
            sc.input_files = new char*[sc.num_files];
            for (int j = 0; j < sc.num_files; ++j) {
                sc.input_files[j] = strdup(files[j].as<std::string>().c_str());
            }

            // Parse field maps list
            auto fields = s["field_maps"];
            sc.num_fields = fields.size();
            sc.file_vars = new char*[sc.num_fields];
            sc.model_vars = new char*[sc.num_fields];
            for (int j = 0; j < sc.num_fields; ++j) {
                sc.file_vars[j] = strdup(fields[j]["file_var"].as<std::string>().c_str());
                sc.model_vars[j] = strdup(fields[j]["model_var"].as<std::string>().c_str());
            }

            // CF detection configuration (Task 11)
            sc.cf_detection_mode = s["cf_detection_mode"]
                                       ? strdup(s["cf_detection_mode"].as<std::string>().c_str())
                                       : strdup("auto");
            sc.cf_cache_enabled =
                s["cf_cache_enabled"] ? s["cf_cache_enabled"].as<bool>() ? 1 : 0 : 1;
            sc.cf_log_level = s["cf_log_level"] ? s["cf_log_level"].as<int>() : 2;

            // Parse output sub-block
            if (s["output"]) {
                auto out = s["output"];
                sc.output.output_enabled = 1;

                // Validate: target_grid_file is required when output block is present
                if (!out["target_grid_file"] ||
                    out["target_grid_file"].as<std::string>().empty()) {
                    std::cerr << "ERROR: [TIDE] 'target_grid_file' is missing or empty in "
                              << "'output' block for stream '" << sc.name << "' in file: "
                              << filename << std::endl;
                    tide_free_config(cfg);
                    return nullptr;
                }
                sc.output.target_grid_file =
                    strdup(out["target_grid_file"].as<std::string>().c_str());

                // output_file (required when output block present)
                sc.output.output_file =
                    out["output_file"]
                        ? strdup(out["output_file"].as<std::string>().c_str())
                        : strdup("");

                // output_frequency: default 3600
                sc.output.output_frequency =
                    out["output_frequency"] ? out["output_frequency"].as<int>() : 3600;

                // regrid_method: default "bilinear", validate if present
                if (out["regrid_method"]) {
                    std::string method = out["regrid_method"].as<std::string>();
                    if (method != "bilinear" && method != "neareststod" &&
                        method != "nearestdtos" && method != "conserve") {
                        std::cerr << "ERROR: [TIDE] Invalid regrid_method '" << method
                                  << "' in 'output' block for stream '" << sc.name
                                  << "' in file: " << filename
                                  << ". Must be one of: bilinear, neareststod, nearestdtos, "
                                     "conserve"
                                  << std::endl;
                        tide_free_config(cfg);
                        return nullptr;
                    }
                    sc.output.regrid_method = strdup(method.c_str());
                } else {
                    sc.output.regrid_method = strdup("bilinear");
                }

                // output_fields: optional list; num_output_fields=0 means write all fields
                if (out["output_fields"] && out["output_fields"].size() > 0) {
                    sc.output.num_output_fields = out["output_fields"].size();
                    sc.output.output_fields = new char*[sc.output.num_output_fields];
                    for (int j = 0; j < sc.output.num_output_fields; ++j) {
                        sc.output.output_fields[j] =
                            strdup(out["output_fields"][j].as<std::string>().c_str());
                    }
                } else {
                    sc.output.num_output_fields = 0;
                    sc.output.output_fields = nullptr;
                }
            } else {
                // No output block: output disabled
                sc.output.output_enabled = 0;
                sc.output.target_grid_file = nullptr;
                sc.output.output_file = nullptr;
                sc.output.output_frequency = 0;
                sc.output.regrid_method = nullptr;
                sc.output.output_fields = nullptr;
                sc.output.num_output_fields = 0;
            }
        }
        return cfg;
    } catch (const YAML::BadFile& e) {
        std::cerr << "ERROR: [TIDE] Failed to load YAML configuration file: " << filename << ". "
                  << e.what() << std::endl;
        return nullptr;
    } catch (const YAML::ParserException& e) {
        std::cerr << "ERROR: [TIDE] YAML Parsing Error in file " << filename << ": " << e.what()
                  << std::endl;
        return nullptr;
    } catch (const YAML::Exception& e) {
        std::cerr << "ERROR: [TIDE] YAML Exception while reading " << filename << ": " << e.what()
                  << std::endl;
        return nullptr;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: [TIDE] Unexpected error parsing YAML file " << filename << ": "
                  << e.what() << std::endl;
        return nullptr;
    }
}

/**
 * @brief Frees the memory allocated by tide_parse_yaml.
 * @param cfg The configuration structure to free.
 */
void tide_free_config(tide_config_t* cfg) {
    if (!cfg) return;

    // Free memory for each stream
    for (int i = 0; i < cfg->num_streams; ++i) {
        tide_stream_config_t& sc = cfg->streams[i];
        free(sc.name);
        free(sc.mesh_file);
        free(sc.lev_dimname);
        free(sc.tax_mode);
        free(sc.time_interp);
        free(sc.map_algo);
        free(sc.read_mode);
        free(sc.cf_detection_mode);
        for (int j = 0; j < sc.num_files; ++j) free(sc.input_files[j]);
        delete[] sc.input_files;
        for (int j = 0; j < sc.num_fields; ++j) {
            free(sc.file_vars[j]);
            free(sc.model_vars[j]);
        }
        delete[] sc.file_vars;
        delete[] sc.model_vars;

        // Free output config memory
        if (sc.output.target_grid_file) free(sc.output.target_grid_file);
        if (sc.output.output_file) free(sc.output.output_file);
        if (sc.output.regrid_method) free(sc.output.regrid_method);
        for (int j = 0; j < sc.output.num_output_fields; ++j) {
            free(sc.output.output_fields[j]);
        }
        if (sc.output.output_fields) delete[] sc.output.output_fields;
    }
    delete[] cfg->streams;
    delete cfg;
}

/**
 * @brief Pretty-prints a tide_output_config_t structure as a YAML string.
 * @param oc Pointer to the output config structure.
 * @return A newly allocated C string containing the YAML representation, or nullptr if disabled.
 *         The caller must free the returned string with free().
 */
char* tide_output_config_to_yaml(const tide_output_config_t* oc) {
    if (!oc || !oc->output_enabled) return nullptr;

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "target_grid_file" << YAML::Value
        << (oc->target_grid_file ? oc->target_grid_file : "");
    out << YAML::Key << "output_file" << YAML::Value
        << (oc->output_file ? oc->output_file : "");
    out << YAML::Key << "output_frequency" << YAML::Value << oc->output_frequency;
    out << YAML::Key << "regrid_method" << YAML::Value
        << (oc->regrid_method ? oc->regrid_method : "bilinear");
    if (oc->num_output_fields > 0 && oc->output_fields) {
        out << YAML::Key << "output_fields" << YAML::Value << YAML::BeginSeq;
        for (int i = 0; i < oc->num_output_fields; ++i) {
            out << oc->output_fields[i];
        }
        out << YAML::EndSeq;
    }
    out << YAML::EndMap;

    return strdup(out.c_str());
}
}
