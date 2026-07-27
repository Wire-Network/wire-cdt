# CPack per-generator configuration (CPACK_PROJECT_CONFIG_FILE).
#
# Two packaged roots, and NEITHER is /usr/cdt:
#   DEB / RPM -> /usr/lib/cdt (self-contained tree) + /usr-level entry points
#   TGZ       -> /opt/wire-cdt (the `wire-cdt/` archive root extracted at /opt)
# Both carry the SAME relative layout; only the home and the extras differ.
if(CPACK_GENERATOR STREQUAL "TGZ")
   # Portable toolchain tarball: plain `wire-cdt` top-level directory (the
   # distribution name -- CPACK_WIRE_PACKAGE_NAME, set in cmake/package.cmake).
   # Monolithic archives take both the artifact name and top dir from
   # CPACK_PACKAGE_FILE_NAME; the package-tgz target restores the versioned
   # artifact name afterwards. Base component only.
   set(CPACK_PACKAGING_INSTALL_PREFIX "/")
   set(CPACK_PACKAGE_FILE_NAME "${CPACK_WIRE_PACKAGE_NAME}")
   string(REPLACE ";ALL;/" ";base;/" CPACK_INSTALL_CMAKE_PROJECTS "${CPACK_INSTALL_CMAKE_PROJECTS}")
   # The install rules stage the /usr/lib/cdt-baked cmake files (right for the
   # deb and the rpm, wrong for the tarball, whose home is /opt/wire-cdt). This
   # hook runs after staging and before the archive is written, and swaps in the
   # portable variants so both find_package(cdt) and the toolchain file resolve
   # the EXTRACTED tree instead of a system install that merely happens to exist.
   # The tarball stays fully self-contained -- it grows no /usr-level extras and
   # keeps its licenses/ at its own root.
   set(CPACK_PRE_BUILD_SCRIPTS "${CPACK_WIRE_TGZ_PRE_BUILD_SCRIPT}")
elseif(CPACK_GENERATOR MATCHES "^(DEB|RPM)$")
   # DISTRO-TOOLCHAIN layout, the shape distros use for a bundled compiler
   # (/usr/lib/llvm-18/...): the whole self-contained toolchain homes at
   # /usr/lib/cdt, so the bundled clang / lld / llvm-* binaries stay OUT of
   # /usr/bin, where they would collide head-on with the distro's own clang,
   # llvm and lld packages. There is no /usr/cdt subtree.
   #
   # cpack-system-layout.cmake then adds the three /usr-level pieces that live
   # outside this prefix: /usr/bin symlinks for the PUBLIC ENTRY POINTS ONLY,
   # the CMake-default-searched /usr/lib/cmake/cdt/cdt-config.cmake (so
   # find_package(cdt) needs no CMAKE_PREFIX_PATH and no cdt_DIR), and
   # /usr/share/licenses/wire-cdt.
   set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/lib/cdt")
   set(CPACK_SET_DESTDIR OFF)
   set(CPACK_PRE_BUILD_SCRIPTS "${CPACK_WIRE_SYSTEM_LAYOUT_SCRIPT}")
endif()
