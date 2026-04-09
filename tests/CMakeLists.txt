# ---------------------------------------------------------------------------
# TIDE test suite
# ---------------------------------------------------------------------------

# ---------------------------------------------------------------------------
# C++ tests (link against yaml-cpp and the tide_yaml_c object)
# ---------------------------------------------------------------------------

# Property 1: YAML output config round-trip
add_executable(test_prop_23_roundtrip test_cf_property_23_output_roundtrip.cpp
               ${CMAKE_SOURCE_DIR}/src/tide_yaml_c.cpp)
target_link_libraries(test_prop_23_roundtrip PRIVATE yaml-cpp::yaml-cpp)
add_test(NAME property_1_yaml_roundtrip COMMAND test_prop_23_roundtrip)
set_tests_properties(property_1_yaml_roundtrip PROPERTIES
  LABELS "property;yaml" TIMEOUT 120)

# Property 2: Invalid regrid method rejection
add_executable(test_prop_24_invalid_regrid test_cf_property_24_invalid_regrid.cpp
               ${CMAKE_SOURCE_DIR}/src/tide_yaml_c.cpp)
target_link_libraries(test_prop_24_invalid_regrid PRIVATE yaml-cpp::yaml-cpp)
add_test(NAME property_2_invalid_regrid COMMAND test_prop_24_invalid_regrid)
set_tests_properties(property_2_invalid_regrid PROPERTIES
  LABELS "property;yaml" TIMEOUT 120)

# Unit: YAML output parsing edge cases
add_executable(test_output_parsing_unit test_output_parsing_unit.cpp
               ${CMAKE_SOURCE_DIR}/src/tide_yaml_c.cpp)
target_link_libraries(test_output_parsing_unit PRIVATE yaml-cpp::yaml-cpp)
add_test(NAME unit_output_parsing COMMAND test_output_parsing_unit)
set_tests_properties(unit_output_parsing PROPERTIES
  LABELS "unit;yaml" TIMEOUT 60)

# ---------------------------------------------------------------------------
# Fortran tests (link against tide library + ESMF + PIO)
# ---------------------------------------------------------------------------

# Helper: add a Fortran test executable linked to tide + ESMF + PIO
function(add_tide_fortran_test TEST_NAME SOURCE_FILE)
  add_executable(${TEST_NAME} ${SOURCE_FILE})
  target_link_libraries(${TEST_NAME} PRIVATE tide)
  target_include_directories(${TEST_NAME} PRIVATE
    ${ESMF_F90COMPILEPATHS}
    ${PIO_Fortran_INCLUDE_DIR}
  )
  add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})
endfunction()

# Property 3: Output write frequency trigger
add_tide_fortran_test(test_prop_25_write_freq test_cf_property_25_write_freq.F90)
set_tests_properties(test_prop_25_write_freq PROPERTIES
  LABELS "property;output" TIMEOUT 120)

# Property 4: Field selection and ordering preservation
add_tide_fortran_test(test_prop_26_field_ordering test_cf_property_26_field_ordering.F90)
set_tests_properties(test_prop_26_field_ordering PROPERTIES
  LABELS "property;output" TIMEOUT 120)

# Property 5: Invalid output field rejection
add_tide_fortran_test(test_prop_27_invalid_field test_cf_property_27_invalid_field.F90)
set_tests_properties(test_prop_27_invalid_field PROPERTIES
  LABELS "property;field_validation" TIMEOUT 120)

# Unit: Regrid manager error paths
add_tide_fortran_test(test_regrid_error_paths test_regrid_error_paths.F90)
set_tests_properties(test_regrid_error_paths PROPERTIES
  LABELS "unit;regrid" TIMEOUT 120)

# Unit: Output manager error paths
add_tide_fortran_test(test_output_error_paths test_output_error_paths.F90)
set_tests_properties(test_output_error_paths PROPERTIES
  LABELS "unit;output" TIMEOUT 120)

# Unit: Field selection edge cases
add_tide_fortran_test(test_field_selection_unit test_field_selection_unit.F90)
set_tests_properties(test_field_selection_unit PROPERTIES
  LABELS "unit;field_validation" TIMEOUT 60)

# Unit: API integration
add_tide_fortran_test(test_api_integration test_api_integration.F90)
set_tests_properties(test_api_integration PROPERTIES
  LABELS "unit;api" TIMEOUT 60)

# Integration: End-to-end output pipeline
add_tide_fortran_test(test_integration_output test_integration_output.F90)
set_tests_properties(test_integration_output PROPERTIES
  LABELS "integration;output" TIMEOUT 180)

# ---------------------------------------------------------------------------
# Build system integration test (shell script)
# ---------------------------------------------------------------------------
add_test(NAME test_build_system
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_build_system.sh
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(test_build_system PROPERTIES
  LABELS "build_system;integration" TIMEOUT 600)
