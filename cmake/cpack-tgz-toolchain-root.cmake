# CPACK_PRE_BUILD_SCRIPTS hook, registered for the TGZ generator ONLY by
# cmake/cpack-project-config.cmake.
#
# Runs after CPack has staged the install tree and before the archive is
# produced, and replaces the staged system-package (/usr-baked) cmake files with
# the portable-tarball variants configured in CMakeLists.txt, which bake
# /opt/wire-cdt -- the tarball's documented default install location
# (`tar xzf ... -C /opt` => /opt/wire-cdt).
#
# Two files bake @CDT_ROOT_DIR@ absolutely and therefore need the swap:
#   cdt-config.cmake        -- its first branch short-circuits discovery
#   CDTWasmToolchain.cmake  -- every compiler path is baked, no discovery at all
# CDTMacros.cmake is NOT swapped: it resolves everything through the RUNTIME
# ${CDT_ROOT} that cdt-config produces, so one copy serves every generator.
#
# The deb and the rpm keep the /usr-baked variants -- they install AT /usr, and
# this hook never runs for them.
#
# Fails loudly rather than silently shipping the wrong file: a staging-layout
# change that moved the configs out from under the search would otherwise
# produce a tarball whose toolchain resolves to a stray /usr install with no
# signal at all.

foreach(_wire_var CPACK_WIRE_TGZ_CDT_CONFIG CPACK_WIRE_TGZ_CDT_TOOLCHAIN)
   if(NOT DEFINED ${_wire_var} OR NOT EXISTS "${${_wire_var}}")
      message(FATAL_ERROR
         "portable-tarball cmake file for ${_wire_var} was not staged at "
         "'${${_wire_var}}' -- see the packaging/tgz configure_file block in "
         "CMakeLists.txt")
   endif()
endforeach()

# The archive generator stages under CPACK_TEMPORARY_DIRECTORY; both spellings
# are probed because the exact staging root differs between monolithic and
# per-component archive packaging.
#
# _wire_name is the staged file to overwrite, _wire_source the variable holding
# the portable variant to write over it.
foreach(_wire_pair "cdt-config.cmake:CPACK_WIRE_TGZ_CDT_CONFIG"
                   "CDTWasmToolchain.cmake:CPACK_WIRE_TGZ_CDT_TOOLCHAIN")
   string(REPLACE ":" ";" _wire_pair_list "${_wire_pair}")
   list(GET _wire_pair_list 0 _wire_name)
   list(GET _wire_pair_list 1 _wire_source_var)

   set(_wire_staged_files)
   foreach(_wire_root IN ITEMS
         "${CPACK_TEMPORARY_DIRECTORY}"
         "${CPACK_TEMPORARY_INSTALL_DIRECTORY}")
      if(_wire_root AND IS_DIRECTORY "${_wire_root}")
         file(GLOB_RECURSE _wire_found "${_wire_root}/${_wire_name}")
         foreach(_wire_candidate IN LISTS _wire_found)
            if(_wire_candidate MATCHES "/lib/cmake/cdt/${_wire_name}$")
               list(APPEND _wire_staged_files "${_wire_candidate}")
            endif()
         endforeach()
      endif()
   endforeach()

   if(NOT _wire_staged_files)
      message(FATAL_ERROR
         "no staged lib/cmake/cdt/${_wire_name} found under "
         "'${CPACK_TEMPORARY_DIRECTORY}' -- the portable tarball would ship the "
         "/usr-baked toolchain config")
   endif()

   list(REMOVE_DUPLICATES _wire_staged_files)
   foreach(_wire_staged IN LISTS _wire_staged_files)
      configure_file("${${_wire_source_var}}" "${_wire_staged}" COPYONLY)
      message(STATUS "wire-cdt: portable ${_wire_name} staged over ${_wire_staged}")
   endforeach()
endforeach()
