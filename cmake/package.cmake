# Packaging configuration for wire-cdt. Produces:
#   TGZ -- portable toolchain tarball, top-level dir `wire-cdt/`
#   DEB -- `wire-cdt` + `wire-cdt-dev` packages homed at /usr/lib/cdt
#   RPM -- `wire-cdt` + `wire-cdt-dev` packages homed at /usr/lib/cdt
# Per-generator differences live in cmake/cpack-project-config.cmake.
#
# PACKAGED LAYOUT -- two homes, and NEITHER is /usr/cdt (nothing installs there,
# ever). Both carry the SAME relative tree; only the home and the extras differ:
#
#   deb / rpm  DISTRO-TOOLCHAIN layout (cf. /usr/lib/llvm-18):
#     /usr/lib/cdt/{bin,lib,include,lib/cmake/cdt,scripts,share,cdt.imports}
#                                   the entire self-contained toolchain, with
#                                   ALL real binaries -- including the bundled
#                                   clang/lld/llvm-* set -- private to bin/
#     /usr/bin/<tool> -> ../lib/cdt/bin/<tool>   public entry points ONLY
#     /usr/lib/cmake/cdt/cdt-config.cmake        zero-setup find_package(cdt)
#     /usr/share/licenses/wire-cdt/              license texts
#
#   tarball    self-contained, identical relative tree under `wire-cdt/`,
#              home /opt/wire-cdt, licenses at its own root, no /usr extras.
#
# Keeping the bundled llvm/clang binaries out of /usr/bin is structural, not
# cosmetic: /usr/bin/clang, /usr/bin/lld, /usr/bin/llvm-ar ... are owned by the
# distro's clang / lld / llvm packages, so shipping ours there either fails the
# dpkg/rpm transaction on a file conflict or hijacks the system toolchain.
#
# DISTRIBUTION IDENTITY vs CMAKE IDENTITY -- the two are deliberately different:
#   * WIRE_PACKAGE_NAME (`wire-cdt`) names the DISTRIBUTION: deb/rpm package
#     names, every artifact file name, the tarball's top-level directory, the
#     release assets, and the CI artifact names. It matches wire-sysio's
#     `wire-sysio` packaging and disambiguates the toolchain from any other
#     `cdt` on a system.
#   * PROJECT_NAME (`cdt`) names the CMAKE PACKAGE and stays untouched:
#     `find_package(cdt)`, the `cdt::` target namespace, the lib/cmake/cdt/
#     subtree INSIDE the prefix, and the cdt-cc / cdt-cpp / cdt-protoc*
#     binaries + cdt.imports are the toolchain's consumed interface
#     (wire-sysio's contracts build resolves them) and must NOT be renamed.
#     Only the PREFIX they live under changes per artifact (see above).
set(WIRE_PACKAGE_NAME "wire-cdt")

set(CPACK_GENERATOR "TGZ")
find_program(DPKG_FOUND "dpkg")
find_program(RPMBUILD_FOUND "rpmbuild")
if(DPKG_FOUND)
   list(APPEND CPACK_GENERATOR "DEB")
endif()
if(RPMBUILD_FOUND)
   list(APPEND CPACK_GENERATOR "RPM")
endif()

set(CPACK_PACKAGE_NAME "${WIRE_PACKAGE_NAME}")
set(CPACK_PACKAGE_VENDOR "${VENDOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${DESC}")
set(CPACK_PACKAGE_VERSION "${VERSION_FULL}")
set(CPACK_PACKAGE_CONTACT "${EMAIL}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${URL}")

# rpmbuild rejects '-' in the Version tag, and '~' sorts a pre-release suffix
# BEFORE the final release in both dpkg and rpm comparisons -- the correct
# ordering for `1.0.0~dev` < `1.0.0`. So package METADATA carries the tilde
# form while artifact FILE names keep VERSION_FULL verbatim (`1.0.0-dev`),
# matching wire-sysio. `-1` is the package release token.
string(REPLACE "-" "~" WIRE_PACKAGE_TILDE_VERSION "${VERSION_FULL}")
set(CPACK_DEBIAN_PACKAGE_VERSION "${WIRE_PACKAGE_TILDE_VERSION}")
set(CPACK_DEBIAN_PACKAGE_RELEASE "1")
set(CPACK_RPM_PACKAGE_VERSION "${WIRE_PACKAGE_TILDE_VERSION}")
set(CPACK_RPM_PACKAGE_RELEASE "1")

if(DPKG_FOUND)
   execute_process(COMMAND "${DPKG_FOUND}" --print-architecture
      OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
      OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
   set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "${CMAKE_SYSTEM_PROCESSOR}")
endif()
# Platform tag carried by every artifact name. Linux keeps the bare processor
# (`x86_64`) so its file names stay byte-identical to the pre-macOS naming;
# macOS gets `macos-<processor>` (`macos-arm64`) so the two platforms' tarballs
# can coexist in one release without a glob collision. Deliberately NOT
# CPACK_SYSTEM_NAME, which CPack would also splice into the Linux names.
if(APPLE)
   set(WIRE_ARCH_TAG "macos-${CMAKE_SYSTEM_PROCESSOR}")
else()
   set(WIRE_ARCH_TAG "${CMAKE_SYSTEM_PROCESSOR}")
endif()
set(CPACK_PACKAGE_FILE_NAME "${WIRE_PACKAGE_NAME}-${VERSION_FULL}-${WIRE_ARCH_TAG}")

# ── Two variants for both system-package formats ──
set(CPACK_COMPONENTS_ALL "base" "dev")
set(CPACK_COMPONENT_BASE_DESCRIPTION "wasm toolchain and sysroot for building Wire contracts")
set(CPACK_COMPONENT_DEV_DESCRIPTION "native (host) contract-testing libraries and sources")

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_BASE_PACKAGE_NAME "${WIRE_PACKAGE_NAME}")
set(CPACK_DEBIAN_DEV_PACKAGE_NAME "${WIRE_PACKAGE_NAME}-dev")
# `<name>_<VERSION_FULL>_<arch>.deb` -- wire-sysio's artifact shape (the version
# in the FILE name is VERSION_FULL, not the tilde metadata version).
set(CPACK_DEBIAN_BASE_FILE_NAME "${WIRE_PACKAGE_NAME}_${VERSION_FULL}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_DEV_FILE_NAME "${WIRE_PACKAGE_NAME}-dev_${VERSION_FULL}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4-gnutls-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "${WIRE_PACKAGE_NAME} (= ${WIRE_PACKAGE_TILDE_VERSION}-1), libcurl4-gnutls-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")
# Rename continuity: `wire-cdt` supersedes the previously-published `cdt`
# package. The old `cdt` deb owned a /usr/cdt subtree and this one owns ordinary
# /usr paths, so the two no longer overlap file-for-file -- but the trio is still
# correct and still wanted: Conflicts+Replaces make dpkg REMOVE the superseded
# `cdt` rather than leave a stale second toolchain installed (and cover the
# residual overlap if any old path is ever re-added), and Provides keeps any
# `Depends: cdt` satisfiable. Same shape as wire-sysio/cmake/package.cmake's
# sysio/mandel conflict declarations. BASE-scoped on purpose: only the base
# package was ever published as `cdt`, and an unscoped Conflicts would make
# wire-cdt-dev conflict with wire-cdt's own `Provides: cdt`, blocking their
# co-install.
set(CPACK_DEBIAN_BASE_PACKAGE_CONFLICTS "cdt")
set(CPACK_DEBIAN_BASE_PACKAGE_REPLACES "cdt")
set(CPACK_DEBIAN_BASE_PACKAGE_PROVIDES "cdt")

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_BASE_PACKAGE_NAME "${WIRE_PACKAGE_NAME}")
set(CPACK_RPM_DEV_PACKAGE_NAME "${WIRE_PACKAGE_NAME}-dev")
set(CPACK_RPM_BASE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.rpm")
string(REPLACE "${WIRE_PACKAGE_NAME}-" "${WIRE_PACKAGE_NAME}-dev-" CPACK_RPM_DEV_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")
set(CPACK_RPM_DEV_FILE_NAME "${CPACK_RPM_DEV_FILE_NAME}.rpm")
set(CPACK_RPM_DEV_PACKAGE_REQUIRES "${WIRE_PACKAGE_NAME} = ${WIRE_PACKAGE_TILDE_VERSION}")
# rpm's rename idiom is Obsoletes + Provides -- deliberately WITHOUT a matching
# Conflicts, unlike the deb trio above. `Obsoletes: cdt` already tells rpm to
# remove the old package as part of installing this one, and `Provides: cdt`
# keeps any `Requires: cdt` satisfiable. Adding `Conflicts: cdt` on top makes rpm
# treat the very package it is obsoleting as a hard conflict instead of an
# upgrade, so the transaction can fail rather than replace. (dpkg is different:
# there Conflicts+Replaces together are what authorise the file takeover, which
# is why the Debian side keeps all three.) Base-scoped: only the base package was
# ever published as `cdt`.
set(CPACK_RPM_BASE_PACKAGE_OBSOLETES "cdt")
set(CPACK_RPM_BASE_PACKAGE_PROVIDES "cdt")
# /usr/lib/cdt is wholly ours, but the payload also reaches into shared system
# directories (/usr/bin for the entry-point symlinks, /usr/lib/cmake for the
# discoverable config, /usr/share/licenses for the license texts). None of those
# may be CLAIMED by this rpm -- they belong to `filesystem` (and to cmake, for
# /usr/lib/cmake). CPack's built-in exclusion list already covers /usr and its
# top-level children; the deeper shared dirs are added because it does not.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION
   "/usr;/usr/bin;/usr/include;/usr/lib;/usr/share;/usr/lib/cmake;/usr/share/licenses")
# The CDT tools are installed read+execute-only, which breaks rpmbuild's
# brp-strip copy-back; packages ship the build output as-is (the deb and
# tarball are not stripped either), so disable the OS install-post hooks.
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none\n%global __os_install_post %{nil}")
# Parity with CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF: no invented ELF requires.
set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")

set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack-project-config.cmake")
# CPack only forwards CPACK_-prefixed variables into CPackConfig.cmake / the
# project config file, so the distribution name is re-exported under the prefix
# rather than re-spelled as a literal over there.
set(CPACK_WIRE_PACKAGE_NAME "${WIRE_PACKAGE_NAME}")
# Same reason: the TGZ-only toolchain-root swap (see cpack-project-config.cmake)
# needs the hook script plus both portable cmake files staged by the
# packaging/tgz configure_file block in CMakeLists.txt.
set(CPACK_WIRE_TGZ_PRE_BUILD_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/cpack-tgz-toolchain-root.cmake")
set(CPACK_WIRE_TGZ_CDT_CONFIG "${CMAKE_BINARY_DIR}/packaging/tgz/lib/cmake/cdt/cdt-config.cmake")
set(CPACK_WIRE_TGZ_CDT_TOOLCHAIN "${CMAKE_BINARY_DIR}/packaging/tgz/lib/cmake/cdt/CDTWasmToolchain.cmake")
# ... and the DEB/RPM-only distro-toolchain layout hook needs its script plus the
# public entry-point list that cmake/InstallCDT.cmake collected from the install
# rules themselves (one source of truth for "what gets a /usr/bin symlink").
set(CPACK_WIRE_SYSTEM_LAYOUT_SCRIPT "${CMAKE_SOURCE_DIR}/cmake/cpack-system-layout.cmake")
list(REMOVE_DUPLICATES CDT_PUBLIC_ENTRY_POINTS)
list(SORT CDT_PUBLIC_ENTRY_POINTS)
set(CPACK_WIRE_PUBLIC_ENTRY_POINTS "${CDT_PUBLIC_ENTRY_POINTS}")

# Portable tarball with the versioned artifact name (see project-config for
# why plain `cpack -G TGZ` emits wire-cdt.tar.gz).
add_custom_target(package-tgz
   COMMAND "${CMAKE_CPACK_COMMAND}" -G TGZ
   COMMAND "${CMAKE_COMMAND}" -E rename "${CMAKE_BINARY_DIR}/${WIRE_PACKAGE_NAME}.tar.gz"
           "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz"
   WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
   COMMENT "Packaging ${CPACK_PACKAGE_FILE_NAME}.tar.gz (portable toolchain)"
   VERBATIM)

set(CPACK_SET_DESTDIR OFF)
set(CPACK_PACKAGE_RELOCATABLE OFF)

include(CPack)
