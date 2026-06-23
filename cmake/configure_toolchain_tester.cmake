if(NOT DEFINED INPUT_FILE OR NOT DEFINED OUTPUT_FILE OR NOT DEFINED TOOLCHAIN_TESTER_SOURCE_DIR)
   message(FATAL_ERROR "INPUT_FILE, OUTPUT_FILE, and TOOLCHAIN_TESTER_SOURCE_DIR are required")
endif()

file(READ "${INPUT_FILE}" _toolchain_tester_content)
string(REPLACE "\${CMAKE_CURRENT_SOURCE_DIR}" "${TOOLCHAIN_TESTER_SOURCE_DIR}" _toolchain_tester_content "${_toolchain_tester_content}")
if(EXISTS "${OUTPUT_FILE}")
   file(READ "${OUTPUT_FILE}" _existing_toolchain_tester_content)
endif()

if(NOT DEFINED _existing_toolchain_tester_content OR NOT _existing_toolchain_tester_content STREQUAL _toolchain_tester_content)
   file(WRITE "${OUTPUT_FILE}" "${_toolchain_tester_content}")
endif()

file(CHMOD "${OUTPUT_FILE}"
   PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
