/**
 * @file test_cf_property_23_output_roundtrip.cpp
 * @brief Property 1: YAML Output Configuration Round-Trip
 *
 * Feature: tide-pio-regrid-output, Property 1: YAML Output Configuration Round-Trip
 * Validates: Requirements 2.1, 2.2, 2.7, 2.8
 *
 * For any valid tide_output_config_t structure (with a non-empty target_grid_file,
 * a valid regrid_method from {"bilinear","neareststod","nearestdtos","conserve"},
 * a positive output_frequency, a non-empty output_file, and zero or more
 * output_fields), pretty-printing the structure to a YAML string and then parsing
 * that YAML string back into a tide_output_config_t SHALL produce a structure
 * equivalent to the original.
 *
 * Test strategy: use a simple PRNG to generate random valid output configs,
 * serialize via tide_output_config_to_yaml, wrap in a stream YAML block,
 * write to a temp file, parse with tide_parse_yaml, and compare all fields.
 * Minimum 100 iterations.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <string>
#include <vector>
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

char* tide_output_config_to_yaml(const tide_output_config_t* oc);
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

// Generate a random alphanumeric string of length [min_len, max_len]
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

// Generate a random file path string
static std::string random_path() {
    return "/" + random_string(3, 8) + "/" + random_string(3, 12) + ".nc";
}

// Pick a random valid regrid method
static const char* valid_methods[] = {"bilinear", "neareststod", "nearestdtos", "conserve"};

static std::string random_regrid_method() {
    return valid_methods[xorshift32() % 4];
}

// Build a random valid tide_output_config_t
struct TestOutputConfig {
    std::string target_grid_file;
    std::string output_file;
    int output_frequency;
    std::string regrid_method;
    std::vector<std::string> output_fields;

    tide_output_config_t to_c_struct() const {
        tide_output_config_t oc;
        oc.output_enabled = 1;
        oc.target_grid_file = strdup(target_grid_file.c_str());
        oc.output_file = strdup(output_file.c_str());
        oc.output_frequency = output_frequency;
        oc.regrid_method = strdup(regrid_method.c_str());
        oc.num_output_fields = static_cast<int>(output_fields.size());
        if (oc.num_output_fields > 0) {
            oc.output_fields = new char*[oc.num_output_fields];
            for (int i = 0; i < oc.num_output_fields; ++i) {
                oc.output_fields[i] = strdup(output_fields[i].c_str());
            }
        } else {
            oc.output_fields = nullptr;
        }
        return oc;
    }

    static void free_c_struct(tide_output_config_t& oc) {
        free(oc.target_grid_file);
        free(oc.output_file);
        free(oc.regrid_method);
        for (int i = 0; i < oc.num_output_fields; ++i) {
            free(oc.output_fields[i]);
        }
        if (oc.output_fields) delete[] oc.output_fields;
    }
};

static TestOutputConfig generate_random_config() {
    TestOutputConfig cfg;
    cfg.target_grid_file = random_path();
    cfg.output_file = random_path();
    cfg.output_frequency = 1 + static_cast<int>(xorshift32() % 86400); // 1..86400 seconds
    cfg.regrid_method = random_regrid_method();

    // 0 to 5 output fields
    int nfields = static_cast<int>(xorshift32() % 6);
    for (int i = 0; i < nfields; ++i) {
        cfg.output_fields.push_back(random_string(3, 16));
    }
    return cfg;
}


// Write a complete YAML file wrapping the output config YAML in a stream block
static bool write_yaml_file(const std::string& filepath, const char* output_yaml) {
    std::ofstream ofs(filepath);
    if (!ofs.is_open()) return false;

    // Build a minimal valid stream block that includes the output sub-block
    ofs << "streams:\n";
    ofs << "  - name: roundtrip_stream\n";
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

    // Indent each line of the output YAML by 6 spaces to nest under "output:"
    std::string yaml_str(output_yaml);
    size_t pos = 0;
    while (pos < yaml_str.size()) {
        size_t eol = yaml_str.find('\n', pos);
        if (eol == std::string::npos) eol = yaml_str.size();
        std::string line = yaml_str.substr(pos, eol - pos);
        ofs << "      " << line << "\n";
        pos = eol + 1;
    }

    ofs.close();
    return true;
}

// Compare two strings safely (handles nullptr)
static bool str_eq(const char* a, const char* b) {
    if (!a && !b) return true;
    if (!a || !b) return false;
    return std::strcmp(a, b) == 0;
}

// Run one round-trip iteration; returns true on pass
static bool run_iteration(int iter, std::string& fail_msg) {
    TestOutputConfig test_cfg = generate_random_config();
    tide_output_config_t oc = test_cfg.to_c_struct();

    // Step 1: Pretty-print to YAML
    char* yaml_str = tide_output_config_to_yaml(&oc);
    if (!yaml_str) {
        fail_msg = "tide_output_config_to_yaml returned nullptr";
        TestOutputConfig::free_c_struct(oc);
        return false;
    }

    // Step 2: Write to temp file wrapped in a stream block
    char tmpfile[256];
    std::snprintf(tmpfile, sizeof(tmpfile), "/tmp/tide_prop1_iter_%d.yaml", iter);
    if (!write_yaml_file(tmpfile, yaml_str)) {
        fail_msg = "Failed to write temp YAML file";
        free(yaml_str);
        TestOutputConfig::free_c_struct(oc);
        return false;
    }

    // Step 3: Parse back
    tide_config_t* parsed = tide_parse_yaml(tmpfile);
    if (!parsed) {
        fail_msg = "tide_parse_yaml returned nullptr for file: " + std::string(tmpfile);
        // Print the YAML for debugging
        std::cerr << "  Generated YAML:\n" << yaml_str << std::endl;
        free(yaml_str);
        TestOutputConfig::free_c_struct(oc);
        std::remove(tmpfile);
        return false;
    }

    // Step 4: Compare fields
    bool pass = true;
    if (parsed->num_streams != 1) {
        fail_msg = "Expected 1 stream, got " + std::to_string(parsed->num_streams);
        pass = false;
    }

    if (pass) {
        const tide_output_config_t& parsed_oc = parsed->streams[0].output;

        if (parsed_oc.output_enabled != 1) {
            fail_msg = "output_enabled != 1";
            pass = false;
        }
        if (pass && !str_eq(oc.target_grid_file, parsed_oc.target_grid_file)) {
            fail_msg = "target_grid_file mismatch: '" +
                       std::string(oc.target_grid_file ? oc.target_grid_file : "(null)") +
                       "' vs '" +
                       std::string(parsed_oc.target_grid_file ? parsed_oc.target_grid_file : "(null)") + "'";
            pass = false;
        }
        if (pass && !str_eq(oc.output_file, parsed_oc.output_file)) {
            fail_msg = "output_file mismatch: '" +
                       std::string(oc.output_file ? oc.output_file : "(null)") +
                       "' vs '" +
                       std::string(parsed_oc.output_file ? parsed_oc.output_file : "(null)") + "'";
            pass = false;
        }
        if (pass && oc.output_frequency != parsed_oc.output_frequency) {
            fail_msg = "output_frequency mismatch: " +
                       std::to_string(oc.output_frequency) + " vs " +
                       std::to_string(parsed_oc.output_frequency);
            pass = false;
        }
        if (pass && !str_eq(oc.regrid_method, parsed_oc.regrid_method)) {
            fail_msg = "regrid_method mismatch: '" +
                       std::string(oc.regrid_method ? oc.regrid_method : "(null)") +
                       "' vs '" +
                       std::string(parsed_oc.regrid_method ? parsed_oc.regrid_method : "(null)") + "'";
            pass = false;
        }
        if (pass && oc.num_output_fields != parsed_oc.num_output_fields) {
            fail_msg = "num_output_fields mismatch: " +
                       std::to_string(oc.num_output_fields) + " vs " +
                       std::to_string(parsed_oc.num_output_fields);
            pass = false;
        }
        if (pass) {
            for (int i = 0; i < oc.num_output_fields; ++i) {
                if (!str_eq(oc.output_fields[i], parsed_oc.output_fields[i])) {
                    fail_msg = "output_fields[" + std::to_string(i) + "] mismatch: '" +
                               std::string(oc.output_fields[i] ? oc.output_fields[i] : "(null)") +
                               "' vs '" +
                               std::string(parsed_oc.output_fields[i] ? parsed_oc.output_fields[i] : "(null)") + "'";
                    pass = false;
                    break;
                }
            }
        }
    }

    // Cleanup
    tide_free_config(parsed);
    free(yaml_str);
    TestOutputConfig::free_c_struct(oc);
    std::remove(tmpfile);

    return pass;
}

int main() {
    const int NUM_ITERATIONS = 100;
    int npass = 0;
    int nfail = 0;

    seed_prng(42);

    std::cout << "Property 1: YAML Output Configuration Round-Trip" << std::endl;
    std::cout << "Running " << NUM_ITERATIONS << " iterations..." << std::endl;

    for (int i = 1; i <= NUM_ITERATIONS; ++i) {
        std::string fail_msg;
        bool passed = run_iteration(i, fail_msg);
        if (passed) {
            npass++;
            std::cout << "  Iteration " << i << ": PASS" << std::endl;
        } else {
            nfail++;
            std::cout << "  Iteration " << i << ": FAIL - " << fail_msg << std::endl;
        }
    }

    std::cout << "\nProperty 1: " << npass << "/" << NUM_ITERATIONS
              << " passed (" << nfail << " failed)" << std::endl;

    return (nfail > 0) ? 1 : 0;
}
