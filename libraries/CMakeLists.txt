cmake_minimum_required(VERSION 3.5)

project(cdt_libraries)

list(APPEND CMAKE_MODULE_PATH ${CDT_BIN})
include(CDTMacros)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -Wall -no-missing-ricardian-clause")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_EXTENSIONS ON)

add_subdirectory(libc)
add_subdirectory(libc++)
add_subdirectory(sysiolib)
add_subdirectory(boost)
add_subdirectory(rt)

if (ENABLE_NATIVE_COMPILER)
   add_subdirectory(native)
endif()

file(COPY ${CMAKE_CURRENT_SOURCE_DIR}/meta_refl/include/bluegrass DESTINATION ${BASE_BINARY_DIR}/include)
