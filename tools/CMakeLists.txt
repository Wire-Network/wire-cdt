project(cdt_tools)

set(CDT_TOOLS_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}")
set(CDT_TOOLS_BIN_DIR "${CDT_TOOLS_BINARY_DIR}/bin")
set_property(GLOBAL PROPERTY CDT_TOOLS_TARGETS "")

find_package(LLVM 18.1 CONFIG REQUIRED)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(Clang CONFIG REQUIRED)
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

# Statically link C++ runtime for portable binaries
set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -static-libstdc++ -static-libgcc")
file(MAKE_DIRECTORY ${CDT_TOOLS_BIN_DIR})

set(SYSIO_STACK_SIZE 8192)
function(cdt_register_tool_target name)
   if(TARGET ${name})
      set_property(GLOBAL APPEND PROPERTY CDT_TOOLS_TARGETS ${name})
   else()
      message(WARNING "Cannot register missing CDT tool target '${name}'")
   endif()
endfunction()

macro (add_tool name)
   set(LLVM_LINK_COMPONENTS support)

   include_directories(include)

   add_executable(${name} ${CDT_TOOLS_BINARY_DIR}/${name}.cpp)
   set_property(TARGET ${name} PROPERTY CXX_STANDARD 23)
   target_compile_options(${name} PRIVATE -fexceptions -fno-rtti)
   target_include_directories(${name} PUBLIC
      ${CDT_TOOLS_BINARY_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}
      ${CMAKE_CURRENT_SOURCE_DIR}/../include
      ${LLVM_INCLUDE_DIRS}
      ${CLANG_INCLUDE_DIRS}
   )
   target_include_directories(${name} SYSTEM PUBLIC
      ${CMAKE_CURRENT_SOURCE_DIR}/../jsoncons/include
   )
   target_link_libraries(${name}
     LLVMSupport
   )

   add_custom_command( TARGET ${name} POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:${name}> ${CDT_TOOLS_BIN_DIR}/ )
   cdt_register_tool_target(${name})
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
add_subdirectory(protoc-gen-zpp)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/include/compiler_options.hpp.in ${CDT_TOOLS_BINARY_DIR}/compiler_options.hpp)

# Stage the LLVM host tools in the bin directory as build outputs. `ninja clean`
# removes tools/bin, so Ninja must own these copies instead of relying on a
# configure-time side effect.
set(_cdt_llvm_tool_outputs "")
foreach(target clang clang++ wasm-ld llvm-ar llvm-ranlib llvm-nm llvm-objcopy llvm-objdump llvm-readobj llvm-readelf llvm-strip opt llc lld ld.lld)
   unset(_tool_path CACHE)
   find_program(_tool_path ${target} HINTS ${LLVM_TOOLS_BINARY_DIR} NO_DEFAULT_PATH)
   if(_tool_path)
      get_filename_component(_real_tool_path "${_tool_path}" REALPATH)
      set(_staged_tool_path "${CDT_TOOLS_BIN_DIR}/${target}")
      add_custom_command(
         OUTPUT "${_staged_tool_path}"
         COMMAND ${CMAKE_COMMAND} -E make_directory "${CDT_TOOLS_BIN_DIR}"
         COMMAND ${CMAKE_COMMAND} -E copy "${_real_tool_path}" "${_staged_tool_path}"
         DEPENDS "${_real_tool_path}"
         COMMENT "Staging LLVM tool ${target}"
         VERBATIM)
      list(APPEND _cdt_llvm_tool_outputs "${_staged_tool_path}")
   else()
      message(WARNING "LLVM tool '${target}' not found in ${LLVM_TOOLS_BINARY_DIR}")
   endif()
endforeach()

if(_cdt_llvm_tool_outputs)
   add_custom_target(cdt_llvm_tools DEPENDS ${_cdt_llvm_tool_outputs})
   cdt_register_tool_target(cdt_llvm_tools)
endif()

get_property(CDT_TOOLS_TARGETS GLOBAL PROPERTY CDT_TOOLS_TARGETS)
set(CDT_TOOLS_TARGETS ${CDT_TOOLS_TARGETS} PARENT_SCOPE)
