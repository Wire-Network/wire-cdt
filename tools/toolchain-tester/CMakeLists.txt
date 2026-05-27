project(toolchain-tester)

set(_toolchain_tester_script "${CMAKE_CURRENT_BINARY_DIR}/toolchain-tester")

add_custom_target(toolchain-tester ALL
   COMMAND ${CMAKE_COMMAND}
      -DINPUT_FILE=${CMAKE_CURRENT_SOURCE_DIR}/toolchain-tester.in
      -DOUTPUT_FILE=${_toolchain_tester_script}
      -DTOOLCHAIN_TESTER_SOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
      -P ${CMAKE_SOURCE_DIR}/cmake/scripts/configure_toolchain_tester.cmake
   DEPENDS
      ${CMAKE_CURRENT_SOURCE_DIR}/toolchain-tester.in
      ${CMAKE_SOURCE_DIR}/cmake/scripts/configure_toolchain_tester.cmake
   VERBATIM
)
