include(ExternalProject)
find_package(Git REQUIRED)
include(GNUInstallDirs)

# Point the tools ExternalProject at vcpkg's LLVM/Clang to avoid picking up
# a (potentially broken) system LLVM.  The tools sub-build does its own
# find_package(); we just need to seed LLVM_DIR and Clang_DIR.
set(_vcpkg_share "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux/share")
if(EXISTS "${_vcpkg_share}/llvm/LLVMConfig.cmake")
   set(LLVM_DIR "${_vcpkg_share}/llvm")
endif()
if(EXISTS "${_vcpkg_share}/clang/ClangConfig.cmake")
   set(Clang_DIR "${_vcpkg_share}/clang")
endif()

ExternalProject_Add(
  CDTTools
  CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER} -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER} -DVERSION_FULL=${VERSION_FULL} -DCMAKE_INSTALL_BINDIR=${CMAKE_INSTALL_BINDIR} -DVERSION_MAJOR=${VERSION_MAJOR} -DVERSION_MINOR=${VERSION_MINOR} -DVERSION_PATCH=${VERSION_PATCH} -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE} -DVCPKG_INSTALLED_DIR=${CMAKE_BINARY_DIR}/vcpkg_installed -DCDT_INSTALL_PREFIX=${CDT_INSTALL_PREFIX} -DLLVM_DIR=${LLVM_DIR} -DClang_DIR=${Clang_DIR}

  SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools"
  BINARY_DIR "${CMAKE_BINARY_DIR}/tools"
  UPDATE_COMMAND ""
  PATCH_COMMAND  ""
  TEST_COMMAND   ""
  INSTALL_COMMAND ""
  BUILD_ALWAYS 1
)

ExternalProject_Add(
  toolchain-tester
  SOURCE_DIR "${CMAKE_SOURCE_DIR}/tools/toolchain-tester"
  BINARY_DIR "${CMAKE_BINARY_DIR}/tools/toolchain-tester"
  UPDATE_COMMAND ""
  PATCH_COMMAND  ""
  TEST_COMMAND   ""
  INSTALL_COMMAND ""
  BUILD_ALWAYS 1
)
