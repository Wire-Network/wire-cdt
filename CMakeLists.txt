cmake_minimum_required(VERSION 3.15)

# Sanity check our source directory to make sure that we are not trying to
# generate an in-source build, and to make
# sure that we don't have any stray generated files lying around in the tree
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
   message(FATAL_ERROR "In-source builds are not allowed.
Please create a directory and run cmake from there, passing the path
to this source directory as the last argument.
This process created the file `CMakeCache.txt' and the directory `CMakeFiles'.
Please delete them.")
endif()

project(cdt)

set(VERSION_MAJOR 4)
set(VERSION_MINOR 1)
set(VERSION_PATCH 1)
# set(VERSION_SUFFIX "") 

if(VERSION_SUFFIX)
   set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}-${VERSION_SUFFIX}")
else()
   set(VERSION_FULL "${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH}")
endif()

set(CDT_INSTALL_PREFIX ${CMAKE_INSTALL_PREFIX}/cdt)

option(ENABLE_NATIVE_COMPILER "enable native builds with the CDT toolchain" ON)
option(ENABLE_TESTS "enable building tests" ON)
include(CMakeDependentOption)
cmake_dependent_option(ENABLE_INTEGRATION_TESTS "Build integration tests" OFF "ENABLE_TESTS" OFF)
option(ENABLE_PACKAGE "enable cpack" ON)

include(GNUInstallDirs)

if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
endif()

if(NOT TOOLS_BUILD_TYPE)
   set(TOOLS_BUILD_TYPE "Release")
endif()

if(NOT LIBS_BUILD_TYPE)
   set(LIBS_BUILD_TYPE "Release")
endif()

include(cmake/ToolsExternalProject.cmake)

set(WASM_SDK_BUILD true)

### Configure the CDTWasmToolchain.cmakes
set(CDT_ROOT_DIR ${CMAKE_BINARY_DIR})

configure_file(${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in ${CMAKE_BINARY_DIR}/lib/cmake/cdt/cdt-config.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTMacros.cmake.in ${CMAKE_BINARY_DIR}/lib/cmake/cdt/CDTMacros.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in ${CMAKE_BINARY_DIR}/lib/cmake/cdt/CDTWasmToolchain.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTInternalMacros.cmake ${CMAKE_BINARY_DIR}/lib/cmake/cdt/CDTInternalMacros.cmake COPYONLY)

set(CDT_ROOT_DIR ${CDT_INSTALL_PREFIX})

configure_file(${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in ${CMAKE_BINARY_DIR}/cmake/cdt-config.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTMacros.cmake.in ${CMAKE_BINARY_DIR}/cmake/CDTMacros.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in ${CMAKE_BINARY_DIR}/cmake/CDTWasmToolchain.cmake @ONLY)

set(CDT_ROOT_DIR "_PREFIX_")
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTMacros.cmake.in ${CMAKE_BINARY_DIR}/cmake/CDTMacrosPackage.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in ${CMAKE_BINARY_DIR}/cmake/CDTWasmToolchainPackage.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in ${CMAKE_BINARY_DIR}/cmake/cdt-config.cmake.package @ONLY)

include(cmake/LibrariesExternalProject.cmake)

include(cmake/InstallCDT.cmake)

# Ensure `ninja clean` also cleans ExternalProject sub-builds and their output artifacts.
# NOTE: Do NOT add lib/ itself — lib/cmake/cdt/ contains configure-time files
# (CDTWasmToolchain.cmake, etc.) that only cmake can regenerate, not ninja.
file(GLOB _lib_artifacts "${CMAKE_BINARY_DIR}/lib/*.a")
set_property(DIRECTORY APPEND PROPERTY ADDITIONAL_CLEAN_FILES
   "${CMAKE_BINARY_DIR}/tools"
   "${CMAKE_BINARY_DIR}/libraries"
   "${CMAKE_BINARY_DIR}/bin"
   "${CMAKE_BINARY_DIR}/include"
   ${_lib_artifacts}
)

configure_file(${CMAKE_SOURCE_DIR}/imports/cdt.imports.in ${CMAKE_BINARY_DIR}/cdt.imports COPYONLY)
install(FILES ${CMAKE_BINARY_DIR}/cdt.imports DESTINATION ${CDT_INSTALL_PREFIX})

configure_file(${CMAKE_SOURCE_DIR}/scripts/ricardeos/ricardeos.py ${CMAKE_BINARY_DIR}/scripts/ricardeos.py COPYONLY)
install(FILES ${CMAKE_BINARY_DIR}/scripts/ricardeos.py DESTINATION ${CDT_INSTALL_PREFIX}/scripts)

configure_file(${CMAKE_SOURCE_DIR}/scripts/gen_native_dispatch.py ${CMAKE_BINARY_DIR}/scripts/gen_native_dispatch.py COPYONLY)
install(FILES ${CMAKE_BINARY_DIR}/scripts/gen_native_dispatch.py DESTINATION ${CDT_INSTALL_PREFIX}/scripts)

# Copy sysiolib source files needed for native contract compilation (host compiler)
foreach(_src crypto.cpp sysiolib.cpp base64.cpp)
  configure_file(${CMAKE_SOURCE_DIR}/libraries/sysiolib/${_src}
                 ${CMAKE_BINARY_DIR}/share/cdt/native-contract-src/${_src} COPYONLY)
endforeach()
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/cdt/native-contract-src/
        DESTINATION ${CDT_INSTALL_PREFIX}/share/cdt/native-contract-src)

set(VENDOR "WireNetwork")
set(PROJECT_NAME "cdt")
set(DESC "Toolchain and supporting tools to facilitate C/C++ development of contracts for Wire blockchains")
set(URL "https://github.com/Wire-Network/wire-cdt")
set(EMAIL "support@wire.network")
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_package.sh.in ${CMAKE_BINARY_DIR}/packages/generate_package.sh @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_deb.sh ${CMAKE_BINARY_DIR}/packages/generate_deb.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_bottle.sh ${CMAKE_BINARY_DIR}/packages/generate_bottle.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_rpm.sh ${CMAKE_BINARY_DIR}/packages/generate_rpm.sh COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/scripts/generate_tarball.sh ${CMAKE_BINARY_DIR}/packages/generate_tarball.sh COPYONLY)

# add licenses
configure_file(${CMAKE_SOURCE_DIR}/libraries/boost/boost.license ${CMAKE_BINARY_DIR}/licenses/boost.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/libraries/meta_refl/LICENSE ${CMAKE_BINARY_DIR}/licenses/meta_refl.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/tools/external/wabt/LICENSE ${CMAKE_BINARY_DIR}/licenses/wabt.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/tools/jsoncons/LICENSE ${CMAKE_BINARY_DIR}/licenses/jsoncons.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/LICENSE.md ${CMAKE_BINARY_DIR}/licenses/cdt.license COPYONLY)


if(ENABLE_TESTS)
   include(cmake/TestsExternalProject.cmake)
   include(CTest)
   enable_testing()
   add_subdirectory(tests)
endif()

if(ENABLE_PACKAGE)
   # section for package construction
   set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
   set(CPACK_PACKAGE_VENDOR "${VENDOR}")
   set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${DESC}")
   set(CPACK_PACKAGE_VERSION "${VERSION_FULL}")
   set(CPACK_PACKAGE_CONTACT "${EMAIL}")

   # Optionally specify license file, URL, etc.
   # set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")
   # set(CPACK_PACKAGE_DESCRIPTION_FILE "${CMAKE_SOURCE_DIR}/README.md")
   # set(CPACK_PACKAGE_INSTALL_DIRECTORY "CDT-${VERSION_FULL}")

   # Define the package generators, for example, TGZ, DEB, and RPM:
   set(CPACK_GENERATOR "DEB")

   include(CPack)
endif()
