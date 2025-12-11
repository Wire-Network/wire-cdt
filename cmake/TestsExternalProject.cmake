include(ExternalProject)
find_package(Git REQUIRED)
include(GNUInstallDirs)

#######################
# Debugging messages
#######################
message(STATUS "===== Debugging Info =====")
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
message(STATUS "CMAKE_BINARY_DIR: ${CMAKE_BINARY_DIR}")
message(STATUS "CMAKE_SOURCE_DIR: ${CMAKE_SOURCE_DIR}")
message(STATUS "CMAKE_MODULE_PATH: ${CMAKE_MODULE_PATH}")
message(STATUS "CMAKE_PREFIX_PATH: ${CMAKE_PREFIX_PATH}")
message(STATUS "sysio_DIR: ${sysio_DIR}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")
message(STATUS "BOOST_ROOT: ${BOOST_ROOT}")
message(STATUS "==========================")

ExternalProject_Add(
        CDTWasmTests
        SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/unit"
        BINARY_DIR "${CMAKE_BINARY_DIR}/tests/unit"
        CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_BINARY_DIR}/lib/cmake/cdt/CDTWasmToolchain.cmake -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCDT_BIN=${CMAKE_BINARY_DIR}/lib/cmake/cdt/ -DBASE_BINARY_DIR=${CMAKE_BINARY_DIR} -D__APPLE=${APPLE} -DCMAKE_MODULE_PATH=${CMAKE_MODULE_PATH} -DCMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH}
        UPDATE_COMMAND ""
        PATCH_COMMAND ""
        TEST_COMMAND ""
        INSTALL_COMMAND ""
        BUILD_ALWAYS 1
        DEPENDS CDTWasmLibraries CDTTools
)

if (ENABLE_INTEGRATION_TESTS)
    message(STATUS "Building integration tests as BUILD_INTEGRATION_TESTS is ON")

    if (CMAKE_BUILD_TYPE STREQUAL "Debug")
        set(TEST_BUILD_TYPE "Debug")
    else ()
        set(TEST_BUILD_TYPE ${CMAKE_BUILD_TYPE})
    endif ()

    # Prepare list variables using ExternalProject's LIST_SEPARATOR to avoid token splitting
    set(EP_LIST_SEP "|")
    # Normalize potential space-separated values into proper lists first
    string(REPLACE " " ";" _PP_LIST "${CMAKE_PREFIX_PATH}")
    string(REPLACE " " ";" _MP_LIST "${CMAKE_MODULE_PATH}")
    string(REPLACE " " ";" _FP_LIST "${CMAKE_FRAMEWORK_PATH}")
    # Join with custom separator that ExternalProject will respect
    string(REPLACE ";" "${EP_LIST_SEP}" EP_PREFIX_PATH "${_PP_LIST}")
    string(REPLACE ";" "${EP_LIST_SEP}" EP_MODULE_PATH "${_MP_LIST}")
    string(REPLACE ";" "${EP_LIST_SEP}" EP_FRAMEWORK_PATH "${_FP_LIST}")

    # Try to detect vcpkg prefix to help Boost discovery inside the ExternalProject
    set(VCPKG_PREFIX_HINT "")
    set(VCPKG_PREFIX_HINT_DEBUG "")
    foreach (_p IN LISTS _PP_LIST)
        if (_p MATCHES "/vcpkg_installed/")
            if (_p MATCHES "/debug$")
                if (NOT VCPKG_PREFIX_HINT_DEBUG)
                    set(VCPKG_PREFIX_HINT_DEBUG "${_p}")
                endif ()
            else ()
                # Prefer non-debug triplet root
                if (NOT VCPKG_PREFIX_HINT)
                    set(VCPKG_PREFIX_HINT "${_p}")
                endif ()
            endif ()
        endif ()
    endforeach ()
    if (NOT VCPKG_PREFIX_HINT AND VCPKG_PREFIX_HINT_DEBUG)
        set(VCPKG_PREFIX_HINT "${VCPKG_PREFIX_HINT_DEBUG}")
    endif ()
    if (VCPKG_PREFIX_HINT)
        string(REPLACE "/debug" "" VCPKG_PREFIX_ND "${VCPKG_PREFIX_HINT}")
        set(VCPKG_INCLUDE_DIR "${VCPKG_PREFIX_ND}/include")
    endif ()

    # Also try to discover wire-sysio's vcpkg install by deriving it from sysio_DIR
    set(SYSIO_VCPKG_INCLUDE "")
    set(SYSIO_VCPKG_PREFIX "")
    if (EXISTS "${sysio_DIR}")
        # sysio_DIR typically points to <sysio-build>/lib/cmake/sysio
        get_filename_component(_SYSIO_CMAKE_DIR "${sysio_DIR}" DIRECTORY)         # .../lib/cmake
        get_filename_component(_SYSIO_LIB_DIR "${_SYSIO_CMAKE_DIR}" DIRECTORY)    # .../lib
        get_filename_component(_SYSIO_BUILD_DIR "${_SYSIO_LIB_DIR}" DIRECTORY)    # .../cmake-build-*
        set(_SYSIO_VCPKG_BASE "${_SYSIO_BUILD_DIR}/vcpkg_installed/x64-linux")
        if (EXISTS "${_SYSIO_VCPKG_BASE}")
            set(SYSIO_VCPKG_PREFIX "${_SYSIO_VCPKG_BASE}")
            set(SYSIO_VCPKG_INCLUDE "${_SYSIO_VCPKG_BASE}/include")
            # Prepend sysio's vcpkg roots to the prefix path list
            if (EP_PREFIX_PATH)
                set(EP_PREFIX_PATH "${SYSIO_VCPKG_PREFIX}${EP_LIST_SEP}${EP_PREFIX_PATH}")
            else ()
                set(EP_PREFIX_PATH "${SYSIO_VCPKG_PREFIX}")
            endif ()
        endif ()
    endif ()

    ExternalProject_Add(
            CDTIntegrationTests
            SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/integration"
            BINARY_DIR "${CMAKE_BINARY_DIR}/tests/integration"
            LIST_SEPARATOR ${EP_LIST_SEP}
            CMAKE_ARGS
            -DCMAKE_BUILD_TYPE:STRING=${TEST_BUILD_TYPE}
            -DCMAKE_CXX_COMPILER:FILEPATH=${CMAKE_CXX_COMPILER}
            -DCMAKE_C_COMPILER:FILEPATH=${CMAKE_C_COMPILER}
            -DCMAKE_FRAMEWORK_PATH:PATH=${EP_FRAMEWORK_PATH}
            -DCMAKE_MODULE_PATH:PATH=${EP_MODULE_PATH}
            -DCMAKE_PREFIX_PATH:PATH=${EP_PREFIX_PATH}
            -Dsysio_DIR:PATH=${sysio_DIR}
            -DLLVM_DIR:PATH=${LLVM_DIR}
            -DBOOST_ROOT:PATH=${BOOST_ROOT}
            $<$<BOOL:${VCPKG_PREFIX_HINT}>:-DBOOST_ROOT:PATH=${VCPKG_PREFIX_HINT}>
            $<$<BOOL:${VCPKG_INCLUDE_DIR}>:-DBOOST_INCLUDEDIR:PATH=${VCPKG_INCLUDE_DIR}>
            $<$<BOOL:${SYSIO_VCPKG_INCLUDE}>:-DBOOST_INCLUDEDIR:PATH=${SYSIO_VCPKG_INCLUDE}>
            # Provide SysioTester with the extra include/library dirs derived from sysio_DIR
            $<$<BOOL:${_SYSIO_BUILD_DIR}>:-DSYSIO_TESTER_EXTRA_INCLUDE_DIRS:PATH=${_SYSIO_SRC_DIR}/libraries/chain/include|${_SYSIO_BUILD_DIR}/libraries/chain/include|${_SYSIO_SRC_DIR}/libraries/chaindb/include|${_SYSIO_SRC_DIR}/libraries/libfc/include|${_SYSIO_SRC_DIR}/libraries/testing/include>
            $<$<BOOL:${_SYSIO_BUILD_DIR}>:-DSYSIO_TESTER_EXTRA_LIBRARY_DIRS:PATH=${_SYSIO_BUILD_DIR}/libraries/chain|${_SYSIO_BUILD_DIR}/libraries/libfc|${_SYSIO_BUILD_DIR}/libraries/chaindb|${_SYSIO_BUILD_DIR}/libraries/builtins|${_SYSIO_BUILD_DIR}/libraries/testing|${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/WAST|${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/WASM|${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/IR|${_SYSIO_BUILD_DIR}/libraries/wasm-jit/Source/Logging|${_SYSIO_BUILD_DIR}/vcpkg_installed/x64-linux/debug/lib>
            -DBoost_NO_BOOST_CMAKE:BOOL=ON
            -DBoost_USE_STATIC_LIBS:BOOL=ON
            -DBoost_USE_MULTITHREADED:BOOL=ON
            UPDATE_COMMAND ""
            PATCH_COMMAND ""
            TEST_COMMAND ""
            INSTALL_COMMAND ""
            BUILD_ALWAYS 1
    )

else ()
    message(STATUS "Skipping building integration tests as BUILD_INTEGRATION_TESTS is OFF")
    return()
endif ()