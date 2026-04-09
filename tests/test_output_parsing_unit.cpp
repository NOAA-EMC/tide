/**
 * @file test_output_parsing_unit.cpp
 * @brief Unit tests for YAML output parsing edge cases.
 *
 * Feature: tide-pio-regrid-output
 * Validates: Requirements 2.3, 2.4, 2.5, 2.6
 *
 * Test cases:
 *   1. Parse stream without `output` block → output_enabled == 0
 *   2. Parse with `output` but no `target_grid_file` → error (nullptr)
 *   3. Parse with each valid `regrid_method` → success
 *   4. Parse with empty `output_fields` → num_output_fields == 0
 *
 * Each test writes a specific YAML file to /tmp/, calls tide_parse_yaml,
 * verifies the expected result, and cleans up.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <fstream>
#include <iostream>

// Declare the extern "C" functions from tide_yaml_c.cpp
extern "C" {

typedef struct {
    char* target_grid_file;
    char* output_file;
    int   output_frequency;
    char* regrid_method;
    char** output_fields;
    int   num_output_fields;
    int   output_enabled;
} tide_output_config_t;

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

typedef struct {
    tide_stream_config_t* streams;
    int num_streams;
} tide_config_t;

tide_config_t* tide_parse_yaml(const char* filename);
void tide_free_config(tide_config_t* cfg);

} // extern "C"


// Helper: write a string to a file, return true on success
static bool write_yaml(const std::string& filepath, const std::string& content) {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) return false;
    ofs << content;
    ofs.close();
    return true;
}

// Minimal stream YAML boilerplate (no output block)
static const char* STREAM_HEADER =
    "streams:\n"
    "  - name: test_stream\n"
    "    tax_mode: \"cycle\"\n"
    "    time_interp: \"linear\"\n"
    "    map_algo: \"none\"\n"
    "    year_first: 2000\n"
    "    year_last: 2000\n"
    "    year_align: 2000\n"
    "    input_files:\n"
    "      - \"dummy.nc\"\n"
    "    field_maps:\n"
    "      - { file_var: \"sst\", model_var: \"SST\" }\n";

/**
 * Test 1: Parse stream without `output` block → output_enabled == 0
 * Validates: Requirement 2.3
 */
static bool test_no_output_block() {
    const char* tmpfile = "/tmp/tide_unit_no_output.yaml";

    // Write YAML with no output block
    if (!write_yaml(tmpfile, STREAM_HEADER)) {
        std::cerr << "  ERROR: Failed to write YAML file" << std::endl;
        return false;
    }

    tide_config_t* cfg = tide_parse_yaml(tmpfile);
    std::remove(tmpfile);

    if (!cfg) {
        std::cerr << "  ERROR: tide_parse_yaml returned nullptr for valid YAML without output"
                  << std::endl;
        return false;
    }

    bool pass = true;
    if (cfg->num_streams != 1) {
        std::cerr << "  ERROR: Expected 1 stream, got " << cfg->num_streams << std::endl;
        pass = false;
    }
    if (pass && cfg->streams[0].output.output_enabled != 0) {
        std::cerr << "  ERROR: Expected output_enabled == 0, got "
                  << cfg->streams[0].output.output_enabled << std::endl;
        pass = false;
    }

    tide_free_config(cfg);
    return pass;
}

/**
 * Test 2: Parse with `output` but no `target_grid_file` → error (nullptr)
 * Validates: Requirement 2.4
 */
static bool test_output_missing_target_grid_file() {
    const char* tmpfile = "/tmp/tide_unit_no_target_grid.yaml";

    std::string yaml = std::string(STREAM_HEADER) +
        "    output:\n"
        "      output_file: \"/output/result.nc\"\n"
        "      output_frequency: 3600\n"
        "      regrid_method: \"bilinear\"\n";

    if (!write_yaml(tmpfile, yaml)) {
        std::cerr << "  ERROR: Failed to write YAML file" << std::endl;
        return false;
    }

    tide_config_t* cfg = tide_parse_yaml(tmpfile);
    std::remove(tmpfile);

    if (cfg != nullptr) {
        std::cerr << "  ERROR: Expected nullptr for output block missing target_grid_file, "
                  << "but got valid config" << std::endl;
        tide_free_config(cfg);
        return false;
    }

    // nullptr is the expected result — parser correctly rejected the config
    return true;
}

/**
 * Test 3: Parse with each valid `regrid_method` → success
 * Validates: Requirement 2.5
 */
static bool test_valid_regrid_methods() {
    const char* methods[] = {"bilinear", "neareststod", "nearestdtos", "conserve"};
    const int num_methods = 4;
    bool all_pass = true;

    for (int i = 0; i < num_methods; ++i) {
        char tmpfile[256];
        std::snprintf(tmpfile, sizeof(tmpfile),
                      "/tmp/tide_unit_regrid_%s.yaml", methods[i]);

        std::string yaml = std::string(STREAM_HEADER) +
            "    output:\n"
            "      target_grid_file: \"/grids/target.nc\"\n"
            "      output_file: \"/output/result.nc\"\n"
            "      output_frequency: 3600\n"
            "      regrid_method: \"" + methods[i] + "\"\n";

        if (!write_yaml(tmpfile, yaml)) {
            std::cerr << "  ERROR: Failed to write YAML file for method " << methods[i]
                      << std::endl;
            all_pass = false;
            continue;
        }

        tide_config_t* cfg = tide_parse_yaml(tmpfile);
        std::remove(tmpfile);

        if (!cfg) {
            std::cerr << "  ERROR: tide_parse_yaml returned nullptr for valid regrid_method \""
                      << methods[i] << "\"" << std::endl;
            all_pass = false;
            continue;
        }

        // Verify the parsed regrid_method matches
        const char* parsed_method = cfg->streams[0].output.regrid_method;
        if (!parsed_method || std::strcmp(parsed_method, methods[i]) != 0) {
            std::cerr << "  ERROR: regrid_method mismatch for \"" << methods[i]
                      << "\": got \"" << (parsed_method ? parsed_method : "(null)") << "\""
                      << std::endl;
            all_pass = false;
        }

        if (cfg->streams[0].output.output_enabled != 1) {
            std::cerr << "  ERROR: output_enabled != 1 for method \"" << methods[i] << "\""
                      << std::endl;
            all_pass = false;
        }

        std::cout << "    regrid_method \"" << methods[i] << "\": OK" << std::endl;
        tide_free_config(cfg);
    }

    return all_pass;
}

/**
 * Test 4: Parse with empty `output_fields` → num_output_fields == 0
 * Validates: Requirement 2.6 (output_fields absent means write all)
 */
static bool test_empty_output_fields() {
    const char* tmpfile = "/tmp/tide_unit_empty_fields.yaml";

    // output block present but no output_fields key at all
    std::string yaml = std::string(STREAM_HEADER) +
        "    output:\n"
        "      target_grid_file: \"/grids/target.nc\"\n"
        "      output_file: \"/output/result.nc\"\n"
        "      output_frequency: 3600\n"
        "      regrid_method: \"bilinear\"\n";

    if (!write_yaml(tmpfile, yaml)) {
        std::cerr << "  ERROR: Failed to write YAML file" << std::endl;
        return false;
    }

    tide_config_t* cfg = tide_parse_yaml(tmpfile);
    std::remove(tmpfile);

    if (!cfg) {
        std::cerr << "  ERROR: tide_parse_yaml returned nullptr for valid config without "
                  << "output_fields" << std::endl;
        return false;
    }

    bool pass = true;
    if (cfg->streams[0].output.num_output_fields != 0) {
        std::cerr << "  ERROR: Expected num_output_fields == 0, got "
                  << cfg->streams[0].output.num_output_fields << std::endl;
        pass = false;
    }
    if (cfg->streams[0].output.output_fields != nullptr) {
        std::cerr << "  ERROR: Expected output_fields == nullptr" << std::endl;
        pass = false;
    }

    tide_free_config(cfg);
    return pass;
}

int main() {
    int npass = 0;
    int nfail = 0;

    std::cout << "=== TIDE YAML Output Parsing Unit Tests ===" << std::endl;
    std::cout << "Validates: Requirements 2.3, 2.4, 2.5, 2.6" << std::endl;
    std::cout << std::endl;

    // Test 1
    std::cout << "Test 1: No output block → output_enabled == 0" << std::endl;
    if (test_no_output_block()) {
        std::cout << "  PASS" << std::endl;
        npass++;
    } else {
        std::cout << "  FAIL" << std::endl;
        nfail++;
    }

    // Test 2
    std::cout << "Test 2: Output block without target_grid_file → error" << std::endl;
    if (test_output_missing_target_grid_file()) {
        std::cout << "  PASS" << std::endl;
        npass++;
    } else {
        std::cout << "  FAIL" << std::endl;
        nfail++;
    }

    // Test 3
    std::cout << "Test 3: Each valid regrid_method → success" << std::endl;
    if (test_valid_regrid_methods()) {
        std::cout << "  PASS" << std::endl;
        npass++;
    } else {
        std::cout << "  FAIL" << std::endl;
        nfail++;
    }

    // Test 4
    std::cout << "Test 4: Empty output_fields → num_output_fields == 0" << std::endl;
    if (test_empty_output_fields()) {
        std::cout << "  PASS" << std::endl;
        npass++;
    } else {
        std::cout << "  FAIL" << std::endl;
        nfail++;
    }

    std::cout << std::endl;
    std::cout << "=== Results: " << npass << "/" << (npass + nfail)
              << " passed (" << nfail << " failed) ===" << std::endl;

    return (nfail > 0) ? 1 : 0;
}
