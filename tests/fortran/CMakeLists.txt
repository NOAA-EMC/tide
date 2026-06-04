# =============================================================================
# TIDE Fortran Binding Tests
# =============================================================================
# Fortran test executables registered conditionally — missing source files
# do not break the build.

# --- Fortran binding integration test ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_fortran_binding.f90")
  add_executable(test_fortran_binding test_fortran_binding.f90)
  target_link_libraries(test_fortran_binding
    PRIVATE
      tide_fortran
      tide
      MPI::MPI_Fortran
  )
  # Ensure the Fortran module directory is accessible
  target_include_directories(test_fortran_binding
    PRIVATE
      ${CMAKE_BINARY_DIR}/mod
  )
  add_test(NAME test_fortran_binding COMMAND test_fortran_binding)
  set_tests_properties(test_fortran_binding PROPERTIES
    LABELS "fortran"
    TIMEOUT 60
    ENVIRONMENT "TIDE_TEST_DATA_DIR=${TIDE_TEST_DATA_DIR}"
    WORKING_DIRECTORY "${TIDE_TEST_DATA_DIR}"
  )
endif()

# --- Multi-stream Fortran binding test ---
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/test_multi_stream.f90")
  add_executable(test_multi_stream test_multi_stream.f90)
  target_link_libraries(test_multi_stream
    PRIVATE
      tide_fortran
      tide
      MPI::MPI_Fortran
  )
  # Ensure the Fortran module directory is accessible
  target_include_directories(test_multi_stream
    PRIVATE
      ${CMAKE_BINARY_DIR}/mod
  )
  add_test(NAME test_multi_stream COMMAND test_multi_stream)
  set_tests_properties(test_multi_stream PROPERTIES
    LABELS "fortran;multi-stream"
    TIMEOUT 120
    ENVIRONMENT "TIDE_TEST_DATA_DIR=${TIDE_TEST_DATA_DIR}"
    WORKING_DIRECTORY "${TIDE_TEST_DATA_DIR}"
  )
endif()
