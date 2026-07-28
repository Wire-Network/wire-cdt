# CPack per-generator configuration (CPACK_PROJECT_CONFIG_FILE).
#
# Two packaged roots, and NEITHER is /usr/cdt:
#   DEB / RPM -> /usr/lib/cdt (self-contained tree) + /usr-level entry points
#   TGZ       -> /opt/wire-cdt (the `wire-cdt/` archive root extracted at /opt)
# Both carry the SAME relative layout; only the home and the extras differ.
if(CPACK_GENERATOR STREQUAL "TGZ")
   # Portable toolchain tarball: plain `wire-cdt` top-level directory (the
   # distribution name -- CPACK_WIRE_PACKAGE_NAME, set in cmake/package.cmake),
   # in a VERSIONED archive file.
   #
   # DECOUPLING THE ARCHIVE NAME FROM THE ROOT DIRECTORY. For a MONOLITHIC
   # archive CPack takes BOTH from CPACK_PACKAGE_FILE_NAME (the root is the
   # basename of the staging dir, which is derived from it) and does NOT consult
   # CPACK_ARCHIVE_FILE_NAME -- that is component-mode only, verified against
   # this generator. Setting it alone therefore still produced `wire-cdt.tar.gz`.
   #
   # So the two are separated at the source instead:
   #   CPACK_PACKAGE_FILE_NAME          -> the VERSIONED artifact name
   #   CPACK_INCLUDE_TOPLEVEL_DIRECTORY -> 0, suppressing the automatic root
   #                                       (which would otherwise be that same
   #                                       versioned name)
   #   CPACK_PACKAGING_INSTALL_PREFIX   -> /wire-cdt, so the payload lands under
   #                                       a `wire-cdt/` root inside the archive
   #
   # Net effect: plain `cpack -G TGZ` / `ninja package` now emits
   # wire-cdt-<VERSION_FULL>-<arch-tag>.tar.gz directly -- matching the CI upload
   # glob (build/wire-cdt-*.tar.gz) and verify-tgz.sh's wire-cdt-*-<arch>.tar.gz
   # -- while the root inside stays `wire-cdt/`. Previously only the package-tgz
   # target's post-hoc rename produced that name, so anyone not running CI's
   # exact command sequence got an artifact no glob picked up.
   set(CPACK_PACKAGE_FILE_NAME "${CPACK_WIRE_TGZ_FILE_NAME}")
   set(CPACK_INCLUDE_TOPLEVEL_DIRECTORY 0)
   set(CPACK_PACKAGING_INSTALL_PREFIX "/${CPACK_WIRE_PACKAGE_NAME}")
   # base AND dev. CDTMacros.cmake ships in base and its native-test macros
   # reference ${CDT_ROOT}/scripts/gen_native_dispatch.py and
   # ${CDT_ROOT}/share/cdt/native-contract-src -- both COMPONENT dev. A
   # base-only tarball therefore advertised native contract testing while
   # omitting everything it needs, which also made native testing unreachable
   # on macOS entirely, the tarball being the only macOS artifact.
   # Enumerated per component rather than left as ALL so a component added
   # later joins the tarball deliberately, not silently.
   string(REPLACE ";ALL;/" ";base;/" _wire_tgz_base "${CPACK_INSTALL_CMAKE_PROJECTS}")
   string(REPLACE ";ALL;/" ";dev;/"  _wire_tgz_dev  "${CPACK_INSTALL_CMAKE_PROJECTS}")
   set(CPACK_INSTALL_CMAKE_PROJECTS "${_wire_tgz_base};${_wire_tgz_dev}")
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
