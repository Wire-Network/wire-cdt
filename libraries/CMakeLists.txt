cmake_minimum_required(VERSION 3.5)

project(cdt_libraries)

list(APPEND CMAKE_MODULE_PATH ${CDT_BIN})
include(CDTMacros)
include(CDTInternalMacros)

# ccache 4.14's preprocessor mode can treat different cdt-cc/cdt-cpp inputs as
# the same compilation. Depend mode hashes the compiler-produced dependency
# graph instead and is safe for these object-only internal library builds.
if(CDT_INTERNAL_LIBRARY_BUILD AND CDT_NATIVE_CCACHE_DIR)
  set(CDT_CCACHE_ENV CCACHE_DEPEND=true)
  if(CDT_CCACHE_COMPILER_CHECK)
    list(APPEND CDT_CCACHE_ENV
      "CCACHE_COMPILERCHECK=${CDT_CCACHE_COMPILER_CHECK}")
  endif()

  foreach(LANG C CXX)
    if(CMAKE_${LANG}_COMPILER_LAUNCHER)
      set(CMAKE_${LANG}_COMPILER_LAUNCHER
        "${CMAKE_COMMAND};-E;env;${CDT_CCACHE_ENV};${CMAKE_${LANG}_COMPILER_LAUNCHER}")
    endif()
  endforeach()
endif()

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

add_subdirectory(libc)
add_subdirectory(libc++)
add_subdirectory(sysiolib)
add_subdirectory(rt)

if (ENABLE_NATIVE_COMPILER)
   add_subdirectory(native)
endif()

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/boost/include/boost/preprocessor DESTINATION ${BASE_BINARY_DIR}/include/boost)
file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/meta_refl/include/bluegrass DESTINATION ${BASE_BINARY_DIR}/include)
