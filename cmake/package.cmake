# Packaging configuration for wire-cdt. Produces:
#   TGZ -- portable toolchain tarball, top-level dir `cdt/`
#   DEB -- `cdt` + `cdt-dev` packages under /usr/cdt
#   RPM -- `cdt` + `cdt-dev` packages under /usr/cdt
# Per-generator differences live in cmake/cpack-project-config.cmake.
# The deb file name keeps the legacy `cdt_<version>-<release>_<arch>.deb`
# shape that wire-sysio CI globs (`cdt_*_amd64.deb`).

set(CPACK_GENERATOR "TGZ")
find_program(DPKG_FOUND "dpkg")
find_program(RPMBUILD_FOUND "rpmbuild")
if(DPKG_FOUND)
   list(APPEND CPACK_GENERATOR "DEB")
endif()
if(RPMBUILD_FOUND)
   list(APPEND CPACK_GENERATOR "RPM")
endif()

set(CPACK_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_PACKAGE_VENDOR "${VENDOR}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${DESC}")
set(CPACK_PACKAGE_VERSION "${VERSION_FULL}")
set(CPACK_PACKAGE_CONTACT "${EMAIL}")
set(CPACK_PACKAGE_HOMEPAGE_URL "${URL}")

# rpm forbids '-' in Version; '~' sorts pre-release suffixes before the final
# release in both dpkg and rpm. The '-1' package release token matches the
# legacy script-built deb naming.
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
set(CPACK_PACKAGE_FILE_NAME "${PROJECT_NAME}-${VERSION_FULL}-${CMAKE_SYSTEM_PROCESSOR}")

# ── Two variants for both system-package formats ──
set(CPACK_COMPONENTS_ALL "base" "dev")
set(CPACK_COMPONENT_BASE_DESCRIPTION "wasm toolchain and sysroot for building Wire contracts")
set(CPACK_COMPONENT_DEV_DESCRIPTION "native (host) contract-testing libraries and sources")

set(CPACK_DEB_COMPONENT_INSTALL ON)
set(CPACK_DEBIAN_BASE_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_DEBIAN_DEV_PACKAGE_NAME "${PROJECT_NAME}-dev")
set(CPACK_DEBIAN_BASE_FILE_NAME "${PROJECT_NAME}_${WIRE_PACKAGE_TILDE_VERSION}-1_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_DEV_FILE_NAME "${PROJECT_NAME}-dev_${WIRE_PACKAGE_TILDE_VERSION}-1_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libcurl4-gnutls-dev")
set(CPACK_DEBIAN_DEV_PACKAGE_DEPENDS "${PROJECT_NAME} (= ${WIRE_PACKAGE_TILDE_VERSION}-1), libcurl4-gnutls-dev")
set(CPACK_DEBIAN_PACKAGE_SECTION "devel")

set(CPACK_RPM_COMPONENT_INSTALL ON)
set(CPACK_RPM_BASE_PACKAGE_NAME "${PROJECT_NAME}")
set(CPACK_RPM_DEV_PACKAGE_NAME "${PROJECT_NAME}-dev")
set(CPACK_RPM_BASE_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.rpm")
string(REPLACE "${PROJECT_NAME}-" "${PROJECT_NAME}-dev-" CPACK_RPM_DEV_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}")
set(CPACK_RPM_DEV_FILE_NAME "${CPACK_RPM_DEV_FILE_NAME}.rpm")
set(CPACK_RPM_DEV_PACKAGE_REQUIRES "${PROJECT_NAME} = ${WIRE_PACKAGE_TILDE_VERSION}")
# /usr/cdt is wholly ours, but never claim /usr itself.
set(CPACK_RPM_EXCLUDE_FROM_AUTO_FILELIST_ADDITION "/usr")
# The CDT tools are installed read+execute-only, which breaks rpmbuild's
# brp-strip copy-back; packages ship the build output as-is (the deb and
# tarball are not stripped either), so disable the OS install-post hooks.
set(CPACK_RPM_SPEC_MORE_DEFINE "%define _build_id_links none\n%global __os_install_post %{nil}")
# Parity with CPACK_DEBIAN_PACKAGE_SHLIBDEPS OFF: no invented ELF requires.
set(CPACK_RPM_PACKAGE_AUTOREQPROV "no")

set(CPACK_PROJECT_CONFIG_FILE "${CMAKE_SOURCE_DIR}/cmake/cpack-project-config.cmake")

# Portable tarball with the versioned artifact name (see project-config for
# why plain `cpack -G TGZ` emits cdt.tar.gz).
add_custom_target(package-tgz
   COMMAND "${CMAKE_CPACK_COMMAND}" -G TGZ
   COMMAND "${CMAKE_COMMAND}" -E rename "${CMAKE_BINARY_DIR}/cdt.tar.gz"
           "${CMAKE_BINARY_DIR}/${CPACK_PACKAGE_FILE_NAME}.tar.gz"
   WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
   COMMENT "Packaging ${CPACK_PACKAGE_FILE_NAME}.tar.gz (portable toolchain)"
   VERBATIM)

set(CPACK_SET_DESTDIR OFF)
set(CPACK_PACKAGE_RELOCATABLE OFF)

include(CPack)
