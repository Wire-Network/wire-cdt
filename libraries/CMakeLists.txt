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
# any whose source has been deleted. See cmake/stage_headers.cmake for why this cannot
# be a configure-time file(COPY).
add_custom_target(stage_cdt_headers ALL
   COMMAND ${CMAKE_COMMAND}
           -DSTAGE_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
           -DSTAGE_BINARY_DIR=${BASE_BINARY_DIR}
           -DSTAGE_NATIVE=$<BOOL:${ENABLE_NATIVE_COMPILER}>
           -P ${CMAKE_CURRENT_SOURCE_DIR}/../cmake/stage_headers.cmake
   COMMENT "Staging CDT headers into ${BASE_BINARY_DIR}/include"
   VERBATIM)

add_subdirectory(libc)
add_subdirectory(libc++)
add_subdirectory(sysiolib)
add_subdirectory(rt)

if (ENABLE_NATIVE_COMPILER)
   add_subdirectory(native)
endif()

# Anything compiled against the staged tree must see the pruned copy, not a leftover.
foreach(staged_consumer sysio native native_sysio)
   if(TARGET ${staged_consumer})
      add_dependencies(${staged_consumer} stage_cdt_headers)
   endif()
endforeach()

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/boost/include/boost/preprocessor DESTINATION ${BASE_BINARY_DIR}/include/boost)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/meta_refl/include/bluegrass DESTINATION ${BASE_BINARY_DIR}/include)
