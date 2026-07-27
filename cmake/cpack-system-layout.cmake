# CPACK_PRE_BUILD_SCRIPTS hook, registered for the DEB and RPM generators ONLY
# by cmake/cpack-project-config.cmake.
#
# Turns the staged self-contained tree into the DISTRO-TOOLCHAIN layout -- the
# shape Debian/Fedora use for a bundled compiler (/usr/lib/llvm-18/...):
#
#   /usr/lib/cdt/                     the ENTIRE self-contained toolchain
#     bin/                            ALL real binaries, INCLUDING the bundled
#                                     clang / clang++ / lld / ld.lld / opt /
#                                     llc / wasm-ld / llvm-* set
#     lib/  include/  scripts/  share/cdt/native-contract-src/
#     lib/cmake/cdt/                  self-contained cmake package
#     cdt.imports
#   /usr/bin/<tool> -> ../lib/cdt/bin/<tool>
#                                     ONLY the public entry points
#   /usr/lib/cmake/cdt/cdt-config.cmake
#                                     CMake-default-searched copy => zero-setup
#                                     find_package(cdt)
#   /usr/share/licenses/wire-cdt/     license texts (FHS)
#
# WHY A HOOK AND NOT install() RULES: all three destinations above sit OUTSIDE
# CPACK_PACKAGING_INSTALL_PREFIX (/usr/lib/cdt), and the install rules are shared
# with the TGZ generator, whose tarball must stay self-contained under its own
# `wire-cdt/` root with no /usr-level extras. CPack globs the staging tree AFTER
# pre-build scripts run, so everything created here is packaged.
#
# Fails loudly rather than silently shipping a broken layout.

if(NOT DEFINED CPACK_WIRE_PUBLIC_ENTRY_POINTS OR "${CPACK_WIRE_PUBLIC_ENTRY_POINTS}" STREQUAL "")
   message(FATAL_ERROR
      "CPACK_WIRE_PUBLIC_ENTRY_POINTS is empty -- cmake/InstallCDT.cmake should "
      "have collected the tool names exported by cmake/package.cmake")
endif()

# Names that must NEVER appear in /usr/bin: they belong to the distro's own
# clang / llvm / lld packages and installing ours over them either fails the
# dpkg/rpm transaction on a file conflict or hijacks the system toolchain.
set(_wire_banned_bin_names
   clang clang++ opt llc lld ld.lld wasm-ld
   llvm-ranlib llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-readobj
   llvm-readelf llvm-strip)

# Component packaging stages each component under <toplevel>/<component>/; a
# monolithic package stages straight into <toplevel>. Probe both, and act only on
# roots that actually carry the toolchain home.
set(_wire_roots)
file(GLOB _wire_candidates LIST_DIRECTORIES true "${CPACK_TEMPORARY_DIRECTORY}/*")
list(APPEND _wire_candidates "${CPACK_TEMPORARY_DIRECTORY}")
foreach(_wire_candidate IN LISTS _wire_candidates)
   if(IS_DIRECTORY "${_wire_candidate}/usr/lib/cdt")
      list(APPEND _wire_roots "${_wire_candidate}")
   endif()
endforeach()

if(NOT _wire_roots)
   message(FATAL_ERROR
      "no staged usr/lib/cdt tree found under '${CPACK_TEMPORARY_DIRECTORY}' -- "
      "the deb/rpm would ship without the toolchain home")
endif()
list(REMOVE_DUPLICATES _wire_roots)

foreach(_wire_root IN LISTS _wire_roots)
   set(_wire_home "${_wire_root}/usr/lib/cdt")

   # ---- /usr/bin symlinks, public entry points only -------------------------
   # Only the component that actually carries bin/ (base) grows them.
   if(IS_DIRECTORY "${_wire_home}/bin")
      file(MAKE_DIRECTORY "${_wire_root}/usr/bin")
      set(_wire_linked)
      foreach(_wire_tool IN LISTS CPACK_WIRE_PUBLIC_ENTRY_POINTS)
         if(EXISTS "${_wire_home}/bin/${_wire_tool}")
            # RELATIVE link: resolves identically in the staging tree and after
            # install, and keeps the link inside /usr as Debian policy prefers.
            file(CREATE_LINK "../lib/cdt/bin/${_wire_tool}"
                 "${_wire_root}/usr/bin/${_wire_tool}" SYMBOLIC)
            list(APPEND _wire_linked "${_wire_tool}")
         endif()
      endforeach()
      if(NOT _wire_linked)
         message(FATAL_ERROR
            "no public entry point was linked into ${_wire_root}/usr/bin -- "
            "CPACK_WIRE_PUBLIC_ENTRY_POINTS does not match the staged bin/")
      endif()
      list(REMOVE_DUPLICATES _wire_linked)
      list(LENGTH _wire_linked _wire_linked_count)
      message(STATUS "wire-cdt: linked ${_wire_linked_count} public entry points into /usr/bin: ${_wire_linked}")

      # ---- CMake-default-searched cdt-config.cmake --------------------------
      # /usr/lib/cmake/cdt is on CMake's default search path; /usr/lib/cdt is
      # not. A DUPLICATE of the configured file is installed there rather than a
      # symlink: both copies are generated from one template with one baked root
      # (/usr/lib/cdt), so they cannot drift, while a symlinked DIRECTORY is a
      # classic dpkg dir-vs-symlink upgrade hazard. Only cdt-config.cmake is
      # needed -- it puts ${CDT_ROOT}/lib/cmake/cdt on CMAKE_MODULE_PATH, so
      # CDTMacros and friends are picked up from the self-contained tree.
      set(_wire_config "${_wire_home}/lib/cmake/cdt/cdt-config.cmake")
      if(NOT EXISTS "${_wire_config}")
         message(FATAL_ERROR "staged cdt-config.cmake missing at '${_wire_config}'")
      endif()
      file(MAKE_DIRECTORY "${_wire_root}/usr/lib/cmake/cdt")
      configure_file("${_wire_config}"
                     "${_wire_root}/usr/lib/cmake/cdt/cdt-config.cmake" COPYONLY)
      message(STATUS "wire-cdt: cdt-config.cmake duplicated to /usr/lib/cmake/cdt")
   endif()

   # ---- licenses -> /usr/share/licenses/wire-cdt ----------------------------
   # The install rule stages them inside the self-contained tree (correct for the
   # tarball); the deb/rpm move them to the FHS location.
   if(IS_DIRECTORY "${_wire_home}/licenses")
      file(MAKE_DIRECTORY "${_wire_root}/usr/share/licenses")
      file(RENAME "${_wire_home}/licenses"
                  "${_wire_root}/usr/share/licenses/wire-cdt")
      message(STATUS "wire-cdt: licenses moved to /usr/share/licenses/wire-cdt")
   endif()

   # ---- structural guard ----------------------------------------------------
   # The collision class must be impossible to reintroduce silently: assert here
   # (packaging time) in addition to the verifier gates (artifact time).
   foreach(_wire_banned IN LISTS _wire_banned_bin_names)
      if(EXISTS "${_wire_root}/usr/bin/${_wire_banned}")
         message(FATAL_ERROR
            "/usr/bin/${_wire_banned} would be packaged -- bundled llvm/clang "
            "binaries must stay private to /usr/lib/cdt/bin or they collide "
            "with the distro's clang / llvm / lld packages")
      endif()
   endforeach()
   if(IS_DIRECTORY "${_wire_root}/usr/cdt")
      message(FATAL_ERROR "a /usr/cdt subtree was staged -- nothing may install there")
   endif()
endforeach()
