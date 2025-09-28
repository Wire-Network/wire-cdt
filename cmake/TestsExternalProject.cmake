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
  PATCH_COMMAND  ""
  TEST_COMMAND   ""
  INSTALL_COMMAND ""
  BUILD_ALWAYS 1
  DEPENDS CDTWasmLibraries CDTTools
)


find_package(sysio QUIET)

if (sysio_FOUND)
   if(CMAKE_BUILD_TYPE STREQUAL "Debug")
      set(TEST_BUILD_TYPE "Debug")
   else()
      set(TEST_BUILD_TYPE ${CMAKE_BUILD_TYPE})
   endif()

   string(REPLACE ";" "|" TEST_FRAMEWORK_PATH "${CMAKE_FRAMEWORK_PATH}")
   string(REPLACE ";" "|" TEST_MODULE_PATH "${CMAKE_MODULE_PATH}")

   ExternalProject_Add(
     CDTIntegrationTests
     SOURCE_DIR "${CMAKE_SOURCE_DIR}/tests/integration"
     BINARY_DIR "${CMAKE_BINARY_DIR}/tests/integration"
     CMAKE_ARGS -DCMAKE_BUILD_TYPE=${TEST_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DCMAKE_FRAMEWORK_PATH=${TEST_FRAMEWORK_PATH} -DCMAKE_MODULE_PATH=${TEST_MODULE_PATH} -Dsysio_DIR=${sysio_DIR} -DLLVM_DIR=${LLVM_DIR} -DBOOST_ROOT=${BOOST_ROOT}
     UPDATE_COMMAND ""
     PATCH_COMMAND  ""
     TEST_COMMAND   ""
     INSTALL_COMMAND ""
     BUILD_ALWAYS 1
   )
else()
   message(STATUS "sysio package not found, skipping building integration tests")
endif()
