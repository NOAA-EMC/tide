#!/bin/bash
# =============================================================================
# Build System Integration Test for TIDE
#
# Validates Requirements: 1.1, 1.2, 1.3, 1.4, 1.5
#
# Tests:
#   1. Top-level CMake configure succeeds
#   2. CMake build succeeds
#   3. CMake install produces lib/ and include/ artifacts
#   4. BUILD_TESTING=ON adds test targets
#
# This script is designed to run inside the jcsda/docker-gnu-openmpi-dev:1.9
# Docker image and is registered as a CTest test.
# =============================================================================
set -e

PASS_COUNT=0
FAIL_COUNT=0
TESTS_RUN=0

pass() {
  PASS_COUNT=$((PASS_COUNT + 1))
  TESTS_RUN=$((TESTS_RUN + 1))
  echo "  PASS: $1"
}

fail() {
  FAIL_COUNT=$((FAIL_COUNT + 1))
  TESTS_RUN=$((TESTS_RUN + 1))
  echo "  FAIL: $1"
}

# ---------------------------------------------------------------------------
# Locate project root (the directory containing the top-level CMakeLists.txt)
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]; then
  echo "ERROR: Cannot find top-level CMakeLists.txt at ${PROJECT_ROOT}"
  exit 1
fi

# ---------------------------------------------------------------------------
# Create temporary directories for the out-of-source build and install
# ---------------------------------------------------------------------------
BUILD_DIR=$(mktemp -d /tmp/tide_build_test.XXXXXX)
INSTALL_DIR=$(mktemp -d /tmp/tide_install_test.XXXXXX)

cleanup() {
  rm -rf "${BUILD_DIR}" "${INSTALL_DIR}"
}
trap cleanup EXIT

echo "============================================="
echo " TIDE Build System Integration Tests"
echo "============================================="
echo "Project root : ${PROJECT_ROOT}"
echo "Build dir    : ${BUILD_DIR}"
echo "Install dir  : ${INSTALL_DIR}"
echo ""

# ---------------------------------------------------------------------------
# Test 1: CMake configure with BUILD_TESTING=ON succeeds
# ---------------------------------------------------------------------------
echo "--- Test 1: CMake configure with BUILD_TESTING=ON ---"
if cmake -S "${PROJECT_ROOT}" -B "${BUILD_DIR}" \
    -DBUILD_TESTING=ON \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_DIR}" \
    > "${BUILD_DIR}/cmake_configure.log" 2>&1; then
  pass "CMake configure succeeded (Req 1.1, 1.3, 1.4)"
else
  echo "  CMake configure output:"
  tail -30 "${BUILD_DIR}/cmake_configure.log"
  fail "CMake configure failed (Req 1.1, 1.3, 1.4)"
fi

# ---------------------------------------------------------------------------
# Test 2: CMake build succeeds
# ---------------------------------------------------------------------------
echo "--- Test 2: CMake build ---"
if cmake --build "${BUILD_DIR}" -j"$(nproc)" \
    > "${BUILD_DIR}/cmake_build.log" 2>&1; then
  pass "CMake build succeeded (Req 1.2, 1.4)"
else
  echo "  CMake build output (last 30 lines):"
  tail -30 "${BUILD_DIR}/cmake_build.log"
  fail "CMake build failed (Req 1.2, 1.4)"
fi

# ---------------------------------------------------------------------------
# Test 3: CMake install produces lib/ and include/ artifacts
# ---------------------------------------------------------------------------
echo "--- Test 3: CMake install artifacts ---"
if cmake --install "${BUILD_DIR}" --prefix "${INSTALL_DIR}" \
    > "${BUILD_DIR}/cmake_install.log" 2>&1; then
  pass "CMake install succeeded"
else
  echo "  CMake install output:"
  tail -20 "${BUILD_DIR}/cmake_install.log"
  fail "CMake install failed"
fi

# Check lib/ directory contains library files
echo "--- Test 3a: lib/ directory with library files ---"
if [ -d "${INSTALL_DIR}/lib" ]; then
  LIB_COUNT=$(find "${INSTALL_DIR}/lib" -type f \( -name "*.a" -o -name "*.so" -o -name "*.dylib" \) | wc -l)
  if [ "${LIB_COUNT}" -gt 0 ]; then
    pass "lib/ contains ${LIB_COUNT} library file(s) (Req 1.5)"
  else
    fail "lib/ exists but contains no library files (Req 1.5)"
  fi
else
  fail "lib/ directory not found after install (Req 1.5)"
fi

# Check include/ directory contains .mod files
echo "--- Test 3b: include/ directory with .mod files ---"
if [ -d "${INSTALL_DIR}/include" ]; then
  MOD_COUNT=$(find "${INSTALL_DIR}/include" -type f -name "*.mod" | wc -l)
  if [ "${MOD_COUNT}" -gt 0 ]; then
    pass "include/ contains ${MOD_COUNT} .mod file(s) (Req 1.5)"
  else
    fail "include/ exists but contains no .mod files (Req 1.5)"
  fi
else
  fail "include/ directory not found after install (Req 1.5)"
fi

# ---------------------------------------------------------------------------
# Test 4: BUILD_TESTING=ON adds test targets
# ---------------------------------------------------------------------------
echo "--- Test 4: BUILD_TESTING=ON creates test infrastructure ---"
if [ -f "${BUILD_DIR}/CTestTestfile.cmake" ] || \
   find "${BUILD_DIR}" -name "CTestTestfile.cmake" -print -quit | grep -q .; then
  pass "CTestTestfile.cmake found — test targets registered (Req 1.3)"
else
  fail "No CTestTestfile.cmake found — BUILD_TESTING may not be working (Req 1.3)"
fi

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
echo ""
echo "============================================="
echo " Summary: ${PASS_COUNT} passed, ${FAIL_COUNT} failed (${TESTS_RUN} total)"
echo "============================================="

if [ "${FAIL_COUNT}" -gt 0 ]; then
  exit 1
fi

exit 0
