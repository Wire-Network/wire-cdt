cmake_minimum_required(VERSION 3.5)

project(cdt_libraries)

list(APPEND CMAKE_MODULE_PATH ${CDT_BIN})
include(CDTMacros)
include(CDTInternalMacros)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -no-missing-ricardian-clause")
if(CMAKE_BUILD_TYPE STREQUAL "Release")
   set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
   set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS ON)

# Stage the CDT-owned headers into ${BASE_BINARY_DIR}/include at BUILD time, pruning
# any whose source has been deleted. See cmake/stage_cdt_tree.cmake for why this cannot
# be a configure-time file(COPY).
add_custom_target(stage_cdt_tree ALL
   COMMAND ${CMAKE_COMMAND}
           -DSTAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
           -DSTAGE_BINARY_DIR=${BASE_BINARY_DIR}
           -DSTAGE_NATIVE=$<BOOL:${ENABLE_NATIVE_COMPILER}>
           -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/stage_cdt_tree.cmake
   COMMENT "Staging CDT build tree into ${BASE_BINARY_DIR}/include"
   VERBATIM)

add_subdirectory(libc)
add_subdirectory(libc++)
add_subdirectory(sysiolib)
add_subdirectory(rt)

if (ENABLE_NATIVE_COMPILER)
   add_subdirectory(native)
endif()

# Anything compiled against the staged tree must see the pruned copy, not a leftover.
#
# Enumerated, not listed by hand. The staging step REMOVE_RECURSE's its destinations and every
# compile in this tree carries -I<build>/include/... baked in by the driver, so a target that
# is not ordered after it can race the deletion. A three-name allowlist covered `sysio`,
# `native` and `native_sysio` and silently missed sysio_malloc, sysio_dsm, sysio_cmem, c, c++,
# rt, sf and the native_* variants -- and would miss the next one added.
function(cdt_order_after_staging dir)
   get_property(dir_targets DIRECTORY "${dir}" PROPERTY BUILDSYSTEM_TARGETS)
   foreach(tgt IN LISTS dir_targets)
      if(NOT tgt STREQUAL "stage_cdt_tree")
         get_target_property(tgt_type ${tgt} TYPE)
         if(NOT tgt_type STREQUAL "INTERFACE_LIBRARY")
            add_dependencies(${tgt} stage_cdt_tree)
         endif()
      endif()
   endforeach()
   get_property(subdirs DIRECTORY "${dir}" PROPERTY SUBDIRECTORIES)
   foreach(subdir IN LISTS subdirs)
      cdt_order_after_staging("${subdir}")
   endforeach()
endfunction()
cdt_order_after_staging("${CMAKE_CURRENT_SOURCE_DIR}")

