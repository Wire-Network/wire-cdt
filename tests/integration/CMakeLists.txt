cmake_minimum_required(VERSION 3.5)
project(integration_tests)

set(SYSIO_VERSION_MIN "3.1")
set(SYSIO_VERSION_SOFT_MAX "6.0")
#set(SYSIO_VERSION_HARD_MAX "")

# Provide extra include dirs to SysioTester (from wire-sysio source/build trees)
get_filename_component(_SYSIO_CMAKE_DIR "${sysio_DIR}" DIRECTORY)         # .../lib/cmake
get_filename_component(_SYSIO_LIB_DIR "${_SYSIO_CMAKE_DIR}" DIRECTORY)    # .../lib
get_filename_component(_SYSIO_BUILD_DIR "${_SYSIO_LIB_DIR}" DIRECTORY)    # .../cmake-build-*
get_filename_component(_SYSIO_SRC_DIR "${_SYSIO_BUILD_DIR}" DIRECTORY)    # .../wire-sysio
if (EXISTS "${_SYSIO_SRC_DIR}/libraries/chain/include")
    set(SYSIO_TESTER_EXTRA_INCLUDE_DIRS
            "${_SYSIO_SRC_DIR}/libraries/chain/include;${_SYSIO_BUILD_DIR}/libraries/chain/include;${_SYSIO_SRC_DIR}/libraries/chaindb/include;${_SYSIO_SRC_DIR}/libraries/libfc/include;${_SYSIO_SRC_DIR}/libraries/testing/include"
            CACHE INTERNAL "Extra include directories for SysioTester")
    # Help SysioTester locate build-tree libraries (when not installed yet)
    set(SYSIO_TESTER_EXTRA_LIBRARY_DIRS
            "${_SYSIO_BUILD_DIR}/libraries/chain;${_SYSIO_BUILD_DIR}/libraries/libfc;${_SYSIO_BUILD_DIR}/libraries/chaindb;${_SYSIO_BUILD_DIR}/libraries/builtins;${_SYSIO_BUILD_DIR}/libraries/testing;${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/WAST;${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/WASM;${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/IR;${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/Logging;${_SYSIO_BUILD_DIR}/vcpkg_installed/x64-linux/debug/lib"
            CACHE INTERNAL "Extra library directories for SysioTester")
    # Workaround older generated SysioTester.cmake that still consumes SYSIO_TESTER_ATOMIC_LIB
    set(SYSIO_TESTER_ATOMIC_LIB atomic CACHE INTERNAL "Atomic lib name for SysioTester fallback")
endif ()

# Now consume sysio (which picks up SYSIO_TESTER_EXTRA_INCLUDE_DIRS)
find_package(sysio REQUIRED)

find_path(GMP_INCLUDE_DIR NAMES gmp.h)
find_library(GMP_LIBRARY gmp)

### Check the version of sysio
set(VERSION_MATCH_ERROR_MSG "")
SYSIO_CHECK_VERSION(VERSION_OUTPUT "${SYSIO_VERSION}"
        "${SYSIO_VERSION_MIN}"
        "${SYSIO_VERSION_SOFT_MAX}"
        "${SYSIO_VERSION_HARD_MAX}"
        VERSION_MATCH_ERROR_MSG)
if (VERSION_OUTPUT STREQUAL "MATCH")
    message(STATUS "Using wire-sysio version ${SYSIO_VERSION}")
elseif (VERSION_OUTPUT STREQUAL "WARN")
    message(WARNING "Using wire-sysio version ${SYSIO_VERSION} even though it exceeds the maximum supported version of ${SYSIO_VERSION_SOFT_MAX}; continuing with configuration, however build may fail.\nIt is recommended to use Leap version ${SYSIO_VERSION_SOFT_MAX}.x")
else () # INVALID OR MISMATCH
    message(FATAL_ERROR "Found wire-sysio version ${SYSIO_VERSION} but it does not satisfy version requirements: ${VERSION_MATCH_ERROR_MSG}\nPlease use Leap version ${SYSIO_VERSION_SOFT_MAX}.x")
endif (VERSION_OUTPUT STREQUAL "MATCH")


enable_testing()

configure_file(${CMAKE_SOURCE_DIR}/contracts.hpp.in ${CMAKE_BINARY_DIR}/contracts.hpp)
include_directories(${CMAKE_BINARY_DIR})

file(GLOB INT_TESTS "*.cpp" "*.hpp")

add_sysio_test_executable(integration_tests ${INT_TESTS})

foreach (TEST_SUITE ${INT_TESTS}) # create an independent target for each test suite
    execute_process(COMMAND bash -c "grep -E 'BOOST_AUTO_TEST_SUITE\\s*[(]' ${TEST_SUITE} | grep -vE '//.*BOOST_AUTO_TEST_SUITE\\s*[(]' | cut -d ')' -f 1 | cut -d '(' -f 2" OUTPUT_VARIABLE SUITE_NAME OUTPUT_STRIP_TRAILING_WHITESPACE) # get the test suite name from the *.cpp file
    if (NOT "" STREQUAL "${SUITE_NAME}") # ignore empty lines
        execute_process(COMMAND bash -c "echo ${SUITE_NAME} | sed -e 's/s$//' | sed -e 's/_test$//'" OUTPUT_VARIABLE TRIMMED_SUITE_NAME OUTPUT_STRIP_TRAILING_WHITESPACE) # trim "_test" or "_tests" from the end of ${SUITE_NAME}
        # to run unit_test with all log from blockchain displayed, put "--verbose" after "--", i.e. "unit_test -- --verbose"
        add_test(NAME ${TRIMMED_SUITE_NAME}_integration_test COMMAND integration_tests --run_test=${SUITE_NAME} --report_level=detailed --color_output)
        # build list of tests to run during coverage testing
        if (ctest_tests)
            string(APPEND ctest_tests "|")
        endif ()
        string(APPEND ctest_tests ${TRIMMED_SUITE_NAME}_integration_test)
    endif ()
endforeach (TEST_SUITE)
set(ctest_tests "'${ctest_tests}' -j8") # surround test list string in apostrophies
