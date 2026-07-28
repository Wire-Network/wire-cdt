cmake_minimum_required(VERSION 3.19)

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

# Version of the wire-cdt DISTRIBUTION (packages, tarballs, release tags). The
# CMake project / find_package name stays `cdt` -- see project() above and
# WIRE_PACKAGE_NAME in cmake/package.cmake for the distribution-identity split.
#
# VERSION_SUFFIX is the release channel: non-empty (`dev`, `rc1`, ...) => a
# pre-release; empty => stable. These four lines are the single source of truth
# for the version: prepare-release.yaml rewrites them, release.yaml cross-checks
# them against the tag.
set(VERSION_MAJOR 1)
set(VERSION_MINOR 0)
set(VERSION_PATCH 0)
set(VERSION_SUFFIX dev)

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

if(NOT DEFINED CMAKE_EXPORT_COMPILE_COMMANDS)
   set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

include(cmake/Tools.cmake)

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

# SYSTEM-PACKAGE variants of the cmake config/toolchain files, configured for
# the deb/rpm root: `/usr/lib/cdt`.
#
# That is the DISTRO-TOOLCHAIN layout -- the same shape Debian/Fedora use for a
# bundled compiler toolchain (/usr/lib/llvm-18/...): the ENTIRE self-contained
# tree (bin/, lib/, include/, lib/cmake/cdt/, scripts/, share/, cdt.imports)
# homes at /usr/lib/cdt, and /usr/bin receives ONLY symlinks for the public
# entry points. The bundled clang/lld/llvm-* binaries therefore never appear in
# /usr/bin, where they would collide head-on with the distro's own clang, llvm
# and lld packages. There is NO /usr/cdt subtree: nothing installs there, ever.
#
# The /usr-level extras the deb/rpm need beside this tree -- the /usr/bin
# symlinks, the CMake-default-searched copy of cdt-config.cmake at
# /usr/lib/cmake/cdt/, and the licenses at /usr/share/licenses/wire-cdt/ -- are
# produced by cmake/cpack-system-layout.cmake, a CPack pre-build hook registered
# for DEB/RPM only. They cannot be plain install() rules: the destinations sit
# OUTSIDE this prefix, and the tarball must not grow them.
#
# Templates are untouched; this is purely installation configuration, same
# mechanism as the build-tree variants above.
set(CDT_ROOT_DIR "/usr/lib/cdt")
configure_file(${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in ${CMAKE_BINARY_DIR}/packaging/lib/cmake/cdt/cdt-config.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTMacros.cmake.in ${CMAKE_BINARY_DIR}/packaging/lib/cmake/cdt/CDTMacros.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in ${CMAKE_BINARY_DIR}/packaging/lib/cmake/cdt/CDTWasmToolchain.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTInternalMacros.cmake ${CMAKE_BINARY_DIR}/packaging/lib/cmake/cdt/CDTInternalMacros.cmake COPYONLY)

# THE lib/cmake/cdt INSTALL RULE -- configured at INSTALL time, against the
# EFFECTIVE prefix.
#
# WHY NOT a plain install(DIRECTORY) of one of the pre-configured variants above:
# whichever variant were chosen would bake ITS root into every install. That is
# what shipped before, and it made the documented `cmake --install build
# --prefix X` path (BUILD.md) produce a toolchain naming /usr/lib/cdt -- so on a
# machine without the deb, a consumer died immediately with
# "The CMAKE_CXX_COMPILER: is not a full path to an existing compiler tool",
# and a prefix-installed cdt-config.cmake preferred a coincidentally-present deb
# over its own relative discovery.
#
# A CONFIGURE-time bake cannot fix this either: `--prefix` is chosen at INSTALL
# time, long after configure_file has run. So the templates are configured HERE,
# inside install(CODE), where ${CMAKE_INSTALL_PREFIX} holds the prefix actually
# in effect -- the configure-time default, or whatever `--prefix` overrode it
# with. DESTDIR only relocates the OUTPUT path; the baked root stays the logical
# prefix, which is what the files must name once installed.
#
# The two PACKAGED roots are applied strictly at cpack staging, by the pre-build
# hooks, and never here:
#   deb / rpm -> /usr/lib/cdt   cmake/cpack-system-layout.cmake
#   tarball   -> /opt/wire-cdt  cmake/cpack-tgz-toolchain-root.cmake
# Under cpack, ${CMAKE_INSTALL_PREFIX} is the STAGING directory, so the files
# this rule writes carry a staging path -- meaningless in a package, and exactly
# why both hooks overwrite them unconditionally rather than only when they look
# wrong.
install(CODE "
   set(CDT_ROOT_DIR \"\${CMAKE_INSTALL_PREFIX}\")
   set(VERSION_FULL \"${VERSION_FULL}\")
   set(SYSROOT_DIR \"${SYSROOT_DIR}\")
   set(_cdt_cmake_dir \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/lib/cmake/cdt\")
   message(STATUS \"wire-cdt: configuring lib/cmake/cdt for prefix \${CMAKE_INSTALL_PREFIX}\")
   configure_file(\"${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in\"        \"\${_cdt_cmake_dir}/cdt-config.cmake\"        @ONLY)
   configure_file(\"${CMAKE_SOURCE_DIR}/cmake/CDTMacros.cmake.in\"         \"\${_cdt_cmake_dir}/CDTMacros.cmake\"         @ONLY)
   configure_file(\"${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in\"  \"\${_cdt_cmake_dir}/CDTWasmToolchain.cmake\" @ONLY)
   configure_file(\"${CMAKE_SOURCE_DIR}/cmake/CDTInternalMacros.cmake\"    \"\${_cdt_cmake_dir}/CDTInternalMacros.cmake\" COPYONLY)
" COMPONENT base)

# PORTABLE-TARBALL variants. The tarball is rooted at `wire-cdt/` and its
# DEFAULT install location is /opt -- extracted there the tree lands on exactly
# `/opt/wire-cdt`, so that is the root its configs bake. (The old
# `/nonexistent/...` sentinel is gone: the tarball now has a real documented
# home, and baking it lets the toolchain FILE work straight out of the box too.)
# The tarball's INTERNAL layout is identical to the deb/rpm's /usr/lib/cdt tree
# -- same relative paths -- it simply keeps everything self-contained under its
# own root and grows no /usr-level extras.
#
# Both files that bake @CDT_ROOT_DIR@ ABSOLUTELY get a variant:
#   * cdt-config.cmake      -- short-circuits discovery when /opt/wire-cdt exists
#   * CDTWasmToolchain.cmake -- has no discovery at all; every compiler path is
#     baked, so it must name /opt/wire-cdt/bin/... to be usable from the tarball.
# CDTMacros.cmake needs no variant: it consumes the RUNTIME ${CDT_ROOT} that
# cdt-config resolves, so it is already relocatable.
#
# An extraction ANYWHERE ELSE is still handled: cdt-config.cmake.in's
# relative-discovery fallback walks up from the config file's own location
# whenever the baked root is not a directory. That fallback is deliberately
# retained -- do not remove it.
#
# Staged only, NOT installed: cmake/cpack-project-config.cmake registers a CPack
# pre-build hook that swaps these over the staged system-package variants for the
# TGZ generator alone, so the deb/rpm payload is untouched.
set(CDT_ROOT_DIR "/opt/wire-cdt")
configure_file(${CMAKE_SOURCE_DIR}/cmake/cdt-config.cmake.in ${CMAKE_BINARY_DIR}/packaging/tgz/lib/cmake/cdt/cdt-config.cmake @ONLY)
configure_file(${CMAKE_SOURCE_DIR}/cmake/CDTWasmToolchain.cmake.in ${CMAKE_BINARY_DIR}/packaging/tgz/lib/cmake/cdt/CDTWasmToolchain.cmake @ONLY)

# Find zpp_bits header (from vcpkg) for contract compilation
find_path(ZPP_BITS_INCLUDE_DIR zpp_bits.h
   HINTS ${CMAKE_PREFIX_PATH}/include ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include
   REQUIRED)


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
install(FILES ${CMAKE_BINARY_DIR}/cdt.imports DESTINATION . COMPONENT base)

configure_file(${CMAKE_SOURCE_DIR}/scripts/ricardeos/ricardeos.py ${CMAKE_BINARY_DIR}/scripts/ricardeos.py COPYONLY)
install(FILES ${CMAKE_BINARY_DIR}/scripts/ricardeos.py DESTINATION scripts COMPONENT base)

configure_file(${CMAKE_SOURCE_DIR}/scripts/gen_native_dispatch.py ${CMAKE_BINARY_DIR}/scripts/gen_native_dispatch.py COPYONLY)
install(FILES ${CMAKE_BINARY_DIR}/scripts/gen_native_dispatch.py DESTINATION scripts COMPONENT dev)

# Copy sysiolib source files needed for native contract compilation (host compiler)
foreach(_src crypto.cpp sysiolib.cpp base64.cpp)
  configure_file(${CMAKE_SOURCE_DIR}/libraries/sysiolib/${_src}
                 ${CMAKE_BINARY_DIR}/share/cdt/native-contract-src/${_src} COPYONLY)
endforeach()
install(DIRECTORY ${CMAKE_BINARY_DIR}/share/cdt/native-contract-src/
        DESTINATION share/cdt/native-contract-src COMPONENT dev)
# Third-party + CDT license texts ship in the base package (the legacy script
# pipeline copied these manually; the cmake install rules previously did not).
install(DIRECTORY ${CMAKE_BINARY_DIR}/licenses/ DESTINATION licenses COMPONENT base)

set(VENDOR "WireNetwork")
set(PROJECT_NAME "cdt")
set(DESC "Toolchain and supporting tools to facilitate C/C++ development of contracts for Wire blockchains")
set(URL "https://github.com/Wire-Network/wire-cdt")
set(EMAIL "support@wire.network")
# Packaging is CPack-only (cmake/package.cmake); the legacy shell package
# generators (scripts/generate_*.sh) were removed with their configure_file
# staging.

# add licenses
configure_file(${CMAKE_SOURCE_DIR}/libraries/boost/boost.license ${CMAKE_BINARY_DIR}/licenses/boost.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/libraries/meta_refl/LICENSE ${CMAKE_BINARY_DIR}/licenses/meta_refl.license COPYONLY)
configure_file(${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/wabt/copyright ${CMAKE_BINARY_DIR}/licenses/wabt.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/tools/jsoncons/LICENSE ${CMAKE_BINARY_DIR}/licenses/jsoncons.license COPYONLY)
configure_file(${CMAKE_SOURCE_DIR}/LICENSE.md ${CMAKE_BINARY_DIR}/licenses/cdt.license COPYONLY)


if(ENABLE_TESTS)
   include(cmake/TestsExternalProject.cmake)
   include(CTest)
   enable_testing()
   add_subdirectory(tests)
endif()

# Merge compile_commands.json from ExternalProject sub-builds into a single
# database at the root build directory so that IDEs can navigate all sources.
include(cmake/MergeCompileCommands.cmake)

if(ENABLE_PACKAGE)
   include(cmake/package.cmake)
endif()
