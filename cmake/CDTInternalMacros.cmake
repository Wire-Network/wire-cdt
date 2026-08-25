# Internal CDT macros — NOT part of the public API.
# These are used only by CDT's own library and test builds.
# External consumers should use add_contract() and add_native_contract()
# from CDTMacros.cmake instead.

function(_cdt_use_native_compiler_cache TARGET)
  if(NOT CDT_INTERNAL_LIBRARY_BUILD OR NOT CDT_NATIVE_CCACHE_DIR)
    return()
  endif()

  foreach(LANG C CXX)
    get_target_property(LAUNCHER ${TARGET} ${LANG}_COMPILER_LAUNCHER)
    if(LAUNCHER)
      list(LENGTH LAUNCHER LAUNCHER_LENGTH)
      set(IS_CMAKE_ENV_LAUNCHER FALSE)
      if(LAUNCHER_LENGTH GREATER 2)
        list(GET LAUNCHER 0 LAUNCHER_COMMAND)
        list(GET LAUNCHER 1 LAUNCHER_MODE)
        list(GET LAUNCHER 2 LAUNCHER_SUBCOMMAND)
        if(LAUNCHER_COMMAND STREQUAL "${CMAKE_COMMAND}"
            AND LAUNCHER_MODE STREQUAL "-E"
            AND LAUNCHER_SUBCOMMAND STREQUAL "env")
          set(IS_CMAKE_ENV_LAUNCHER TRUE)
        endif()
      endif()

      if(IS_CMAKE_ENV_LAUNCHER)
        list(INSERT LAUNCHER 3 "CCACHE_DIR=${CDT_NATIVE_CCACHE_DIR}")
      else()
        set(LAUNCHER
          "${CMAKE_COMMAND};-E;env;CCACHE_DIR=${CDT_NATIVE_CCACHE_DIR};${LAUNCHER}")
      endif()
      set_property(TARGET ${TARGET} PROPERTY ${LANG}_COMPILER_LAUNCHER
        "${LAUNCHER}")
    endif()
  endforeach()
endfunction()

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
