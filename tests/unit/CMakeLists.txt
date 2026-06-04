# =============================================================================
# TIDE Unit Tests
# =============================================================================
# Unit test executables registered conditionally — missing source files
# do not break the build.

# --- Core types and error handling tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_types.cpp")
  add_tide_test(NAME test_types SOURCES test_types.cpp)
endif()

# --- Config parser tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_config.cpp")
  add_tide_test(NAME test_config SOURCES test_config.cpp)
endif()

# --- Config production-readiness tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_config_production.cpp")
  add_tide_test(NAME test_config_production SOURCES test_config_production.cpp)
endif()

# --- Scaling engine tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_scaling.cpp")
  add_tide_test(NAME test_scaling SOURCES test_scaling.cpp)
endif()

# --- Temporal interpolation engine tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_temporal.cpp")
  add_tide_test(NAME test_temporal SOURCES test_temporal.cpp)
endif()

# --- Vertical interpolation tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_vertical.cpp")
  add_tide_test(NAME test_vertical SOURCES test_vertical.cpp)
endif()

# --- Ring buffer tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_ring_buffer.cpp")
  add_tide_test(NAME test_ring_buffer SOURCES test_ring_buffer.cpp)
endif()

# --- Stream (pipeline orchestrator) tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_stream.cpp")
  add_tide_test(NAME test_stream SOURCES test_stream.cpp)
endif()

# --- Spatial regridding tests ---
# test_spatial provides its own main() for Atlas initialization,
# so it links gtest (without gtest_main) directly.
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_spatial.cpp")
  add_executable(test_spatial test_spatial.cpp)
  target_link_libraries(test_spatial
    PRIVATE
      tide
      GTest::gtest
      GTest::gmock
      rapidcheck
      rapidcheck_gtest
  )
  target_include_directories(test_spatial
    PRIVATE
      ${CMAKE_SOURCE_DIR}/include
      ${CMAKE_SOURCE_DIR}/src
  )
  gtest_discover_tests(test_spatial
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES
      LABELS "unit"
  )
endif()

# --- Memory budget tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_memory_budget.cpp")
  add_tide_test(NAME test_memory_budget SOURCES test_memory_budget.cpp)
endif()

# --- SCRIP weight file reader tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_scrip.cpp")
  add_tide_test(NAME test_scrip SOURCES test_scrip.cpp)
endif()

# --- Memory budget property tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_memory_budget_props.cpp")
  add_tide_test(NAME test_memory_budget_props SOURCES test_memory_budget_props.cpp)
endif()

# --- SCRIP weight matrix property tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_scrip_props.cpp")
  add_tide_test(NAME test_scrip_props SOURCES test_scrip_props.cpp)
endif()

# --- Performance timer tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_timers.cpp")
  add_tide_test(NAME test_timers SOURCES test_timers.cpp)
endif()

# --- ESMF grid reader tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_esmf_reader.cpp")
  add_tide_test(NAME test_esmf_reader SOURCES test_esmf_reader.cpp)
endif()

# --- Prefetch manager tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_prefetch.cpp")
  add_tide_test(NAME test_prefetch SOURCES test_prefetch.cpp)
endif()

# --- Stream manager tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_stream_manager.cpp")
  add_tide_test(NAME test_stream_manager SOURCES test_stream_manager.cpp)
endif()

# --- Prefetch equivalence property tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_prefetch_props.cpp")
  add_tide_test(NAME test_prefetch_props SOURCES test_prefetch_props.cpp)
endif()

# --- Stream manager property tests ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_stream_manager_props.cpp")
  add_tide_test(NAME test_stream_manager_props SOURCES test_stream_manager_props.cpp)
endif()
