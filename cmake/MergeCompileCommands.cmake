# MergeCompileCommands.cmake
#
# Creates a custom target that merges compile_commands.json files from all
# ExternalProject sub-builds into a single database at the root build directory
# so that IDEs (CLion, clangd) can navigate all sources.

set(_merge_script "${CMAKE_SOURCE_DIR}/cmake/scripts/merge_compile_commands.py")
set(_merged_cc "${CMAKE_BINARY_DIR}/compile_commands.json")

set(_compile_commands_inputs
  "${CMAKE_BINARY_DIR}/tools/compile_commands.json"
  "${CMAKE_BINARY_DIR}/tools/toolchain-tester/compile_commands.json"
  "${CMAKE_BINARY_DIR}/libraries/compile_commands.json"
)

set(_compile_commands_deps CDTTools toolchain-tester CDTWasmLibraries)

if(ENABLE_TESTS)
  list(APPEND _compile_commands_inputs "${CMAKE_BINARY_DIR}/tests/unit/compile_commands.json")
  list(APPEND _compile_commands_deps CDTWasmTests)
endif()

set(_merge_args -o "${_merged_cc}")
foreach(_cc IN LISTS _compile_commands_inputs)
  list(APPEND _merge_args "${_cc}")
endforeach()

add_custom_target(merge_compile_commands ALL
  COMMAND python3 "${_merge_script}" ${_merge_args}
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
  COMMENT "Merging compile_commands.json from ExternalProject builds"
  VERBATIM
)

foreach(_dep IN LISTS _compile_commands_deps)
  add_dependencies(merge_compile_commands ${_dep})
endforeach()
