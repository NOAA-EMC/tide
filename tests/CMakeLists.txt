# ---------------------------------------------------------------------------
# TIDE test suite
# ---------------------------------------------------------------------------

# Build system integration test (shell script)
add_test(NAME test_build_system
  COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/test_build_system.sh
  WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)
set_tests_properties(test_build_system PROPERTIES
  LABELS "build_system;integration"
  TIMEOUT 600
)
