cmake_minimum_required(VERSION 3.5)

find_package(LLVM 18.1 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang in: ${Clang_DIR}")

list(APPEND CMAKE_MODULE_PATH ${LLVM_DIR})
include(AddLLVM)

find_program(SCCACHE_FOUND sccache)
if (SCCACHE_FOUND)
   message(STATUS "Using sccache")
   set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE sccache)
else()
   find_program(CCACHE_FOUND ccache)
   if (CCACHE_FOUND)
      message(STATUS "Using ccache")
      set_property(GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache)
   endif()
endif()

include_directories(${LLVM_INCLUDE_DIRS})
link_directories(${LLVM_LIBRARY_DIRS})
add_definitions(${LLVM_DEFINITIONS})
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

set(SYSIO_STACK_SIZE 8192)

macro (add_tool name)
   set(LLVM_LINK_COMPONENTS support)

   include_directories(include)

   add_executable(${name} ${CMAKE_BINARY_DIR}/${name}.cpp)
   set_property(TARGET ${name} PROPERTY CXX_STANDARD 17)
   target_compile_options(${name} PRIVATE -fexceptions -fno-rtti)
   target_include_directories(${name} PUBLIC
      ${CMAKE_BINARY_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../include
      ${CMAKE_CURRENT_SOURCE_DIR}/../jsoncons/include
      ${LLVM_INCLUDE_DIRS}
      ${CLANG_INCLUDE_DIRS}
   )
   target_link_libraries(${name}
     LLVMSupport
   )

   add_custom_command( TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${name}> ${CMAKE_BINARY_DIR}/bin/ )
endmacro()

# Build plugins (sysio_attrs and sysio_codegen)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../plugins/sysio ${CMAKE_CURRENT_BINARY_DIR}/sysio_plugins)

# Build orchestrator tool
add_subdirectory(codegen)

add_subdirectory(abidiff)
add_subdirectory(cc)
add_subdirectory(ld)
add_subdirectory(init)
add_subdirectory(external)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/compiler_options.hpp.in ${CMAKE_BINARY_DIR}/compiler_options.hpp)

# Create symlinks for LLVM tools in the bin directory
foreach(target clang clang++ wasm-ld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-objdump llvm-readobj llvm-readelf llvm-strip opt llc lld ld.lld)
   find_program(_tool_path ${target} HINTS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
   if(_tool_path)
      file(CREATE_LINK ${_tool_path} ${CMAKE_BINARY_DIR}/bin/${target} SYMBOLIC)
   else()
      message(WARNING "LLVM tool '${target}' not found in ${LLVM_TOOLS_BINARY_DIR}")
   endif()
   unset(_tool_path CACHE)
endforeach()
