/**
 * @file test_cf_property_24_invalid_regrid.cpp
 * @brief Property 2: Invalid Regrid Method Rejection
 *
 * Feature: tide-pio-regrid-output, Property 2: Invalid Regrid Method Rejection
 * Validates: Requirements 2.6
 *
 * For any string that is not a member of the set
 * {"bilinear","neareststod","nearestdtos","conserve"}, when that string is used
 * as the regrid_method value in a YAML output block, the parser SHALL return an
 * error (null pointer / non-zero return code).
 *
 * Test strategy: use a simple PRNG to generate random strings that are NOT valid
 * regrid methods. For each iteration, write a YAML file with a stream containing
 * an output block with the invalid regrid_method, call tide_parse_yaml, and verify
 * it returns nullptr. Minimum 100 iterations.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <fstream>
#include <iostream>
#include <set>

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

// Simple xorshift32 PRNG
static uint32_t prng_state = 12345;

static uint32_t xorshift32() {
    uint32_t x = prng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    prng_state = x;
    return x;
}

static void seed_prng(uint32_t seed) {
    prng_state = seed ? seed : 1;
}

// The set of valid regrid methods
static const std::set<std::string> VALID_METHODS = {
    "bilinear", "neareststod", "nearestdtos", "conserve"
};

/**
 * Generate a random string of length [min_len, max_len] using alphanumeric + underscore chars.
 */
static std::string random_string(int min_len, int max_len) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyz"
        "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
        "0123456789_";
    int len = min_len + static_cast<int>(xorshift32() % (max_len - min_len + 1));
    std::string result;
    result.reserve(len);
    for (int i = 0; i < len; ++i) {
        result += charset[xorshift32() % (sizeof(charset) - 1)];
    }
    return result;
}

/**
 * Generate a random string that is NOT a valid regrid method.
 * Keeps generating until the string is not in the valid set.
 */
static std::string random_invalid_regrid_method() {
    std::string s;
    do {
        s = random_string(1, 20);
    } while (VALID_METHODS.count(s) > 0);
    return s;
}

/**
 * Write a minimal valid YAML stream file with the given regrid_method in the output block.
 */
static bool write_yaml_file(const std::string& filepath, const std::string& regrid_method) {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) return false;

    ofs << "streams:\n";
    ofs << "  - name: test_stream\n";
    ofs << "    tax_mode: \"cycle\"\n";
    ofs << "    time_interp: \"linear\"\n";
    ofs << "    map_algo: \"none\"\n";
    ofs << "    year_first: 2000\n";
    ofs << "    year_last: 2000\n";
    ofs << "    year_align: 2000\n";
    ofs << "    input_files:\n";
    ofs << "      - \"dummy.nc\"\n";
    ofs << "    field_maps:\n";
    ofs << "      - { file_var: \"dummy_var\", model_var: \"dummy_model\" }\n";
    ofs << "    output:\n";
    ofs << "      target_grid_file: \"/grids/target.nc\"\n";
    ofs << "      output_file: \"/output/result.nc\"\n";
    ofs << "      output_frequency: 3600\n";
    ofs << "      regrid_method: \"" << regrid_method << "\"\n";

    ofs.close();
    return true;
}

/**
 * Run one iteration: generate an invalid regrid method, write YAML, parse, verify nullptr.
 * Returns true on pass (parser correctly rejected the invalid method).
 */
static bool run_iteration(int iter, std::string& fail_msg, std::string& tested_method) {
    tested_method = random_invalid_regrid_method();

    char tmpfile[256];
    std::snprintf(tmpfile, sizeof(tmpfile), "/tmp/tide_prop2_iter_%d.yaml", iter);

    if (!write_yaml_file(tmpfile, tested_method)) {
        fail_msg = "Failed to write temp YAML file";
        return false;
    }

    tide_config_t* parsed = tide_parse_yaml(tmpfile);
    std::remove(tmpfile);

    if (parsed != nullptr) {
        fail_msg = "tide_parse_yaml returned non-null for invalid regrid_method: \"" +
                   tested_method + "\"";
        tide_free_config(parsed);
        return false;
    }

    // Parser correctly returned nullptr — the invalid method was rejected
    return true;
}

int main() {
    // Feature: tide-pio-regrid-output, Property 2: Invalid Regrid Method Rejection
    const int NUM_ITERATIONS = 100;
    int npass = 0;
    int nfail = 0;

    seed_prng(42);

    std::cout << "Property 2: Invalid Regrid Method Rejection" << std::endl;
    std::cout << "Running " << NUM_ITERATIONS << " iterations..." << std::endl;

    for (int i = 1; i <= NUM_ITERATIONS; ++i) {
        std::string fail_msg;
        std::string tested_method;
        bool passed = run_iteration(i, fail_msg, tested_method);
        if (passed) {
            npass++;
            std::cout << "  Iteration " << i << ": PASS (method=\"" << tested_method << "\")"
                      << std::endl;
        } else {
            nfail++;
            std::cout << "  Iteration " << i << ": FAIL - " << fail_msg << std::endl;
        }
    }

    std::cout << "\nProperty 2: " << npass << "/" << NUM_ITERATIONS
              << " passed (" << nfail << " failed)" << std::endl;

    return (nfail > 0) ? 1 : 0;
}
