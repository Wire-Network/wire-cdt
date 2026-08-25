# Internal CDT macros — NOT part of the public API.
# These are used only by CDT's own library and test builds.
# External consumers should use add_contract() and add_native_contract()
# from CDTMacros.cmake instead.

# Adds a native library target that can be used in native and WASM builds
# @param TARGET The target name
# @param ARGN Additional source files
macro(add_native_library TARGET)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "wasm")
    add_library(${TARGET} ${ARGN})
    target_compile_options(${TARGET} PUBLIC -fnative)
  else()
    add_library(${TARGET} ${ARGN})
    set_target_properties(${TARGET} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_cdt_include_directories(${TARGET})
    set_target_properties(${TARGET} PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/dummy)
  endif()
  _cdt_use_native_compiler_cache(${TARGET})
  # sysio_wasm_import is only understood by the WASM clang plugin; suppress for native builds
  target_compile_options(${TARGET} PRIVATE -Wno-unknown-attributes)
endmacro()

# Adds a native executable target that can be used in native and WASM builds
# @param TARGET The target name
# @param ARGN Additional source files
macro(add_native_executable TARGET)
  if(CMAKE_SYSTEM_PROCESSOR MATCHES "wasm")
    add_executable(${TARGET} ${ARGN})
    target_compile_options(${TARGET} PUBLIC -fnative)
    set_target_properties(${TARGET} PROPERTIES LINK_FLAGS "-fnative" SUFFIX "")
    get_target_property(BINOUTPUT ${TARGET} BINARY_DIR)
    if("${CMAKE_BUILD_TYPE}" STREQUAL "Debug" AND APPLE)
      target_compile_options(${TARGET} PUBLIC -g)
      find_program(name NAMES "dsymutil")
      if(name)
        add_custom_command(TARGET ${TARGET} POST_BUILD COMMAND dsymutil ${BINOUTPUT}/${TARGET})
      endif()
    endif()
  else()
    add_executable(${TARGET} ${ARGN})
    set_target_properties(${TARGET} PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_cdt_include_directories(${TARGET})
    set_target_properties(${TARGET} PROPERTIES EXECUTABLE_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/dummy)
  endif()
  _cdt_use_native_compiler_cache(${TARGET})
  # sysio_wasm_import is only understood by the WASM clang plugin; suppress for native builds
  target_compile_options(${TARGET} PRIVATE -Wno-unknown-attributes)
endmacro()
