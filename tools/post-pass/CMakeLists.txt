# sysio-pp: WIRE-specific WASM post-pass, built against the vcpkg-provided WABT
# (de-vendored — see SEC-10 / WSA-020). Replaces the old in-tree build at
# tools/external/wabt, which vendored a 2018-era WABT source copy. libwabt and
# the stock converters now come from the pinned `wabt` vcpkg port.
find_package(wabt CONFIG REQUIRED)

add_executable(sysio-pp postpass.cc)
set_property(TARGET sysio-pp PROPERTY CXX_STANDARD 17)
set_property(TARGET sysio-pp PROPERTY CXX_STANDARD_REQUIRED ON)
# WABT_TRY in postpass.cc needs exceptions; the wabt port is built WITH_EXCEPTIONS=ON.
target_compile_options(sysio-pp PRIVATE -fexceptions)
target_link_libraries(sysio-pp PRIVATE wabt::wabt)

# Stage sysio-pp into the CDT tools bin dir (consumed by cmake/InstallCDT.cmake),
# matching the convention used by the other tools (CDT_TOOLS_BIN_DIR /
# cdt_register_tool_target are defined in the parent tools/CMakeLists.txt).
add_custom_command(TARGET sysio-pp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E make_directory "${CDT_TOOLS_BIN_DIR}"
   COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:sysio-pp> "${CDT_TOOLS_BIN_DIR}/sysio-pp"
   VERBATIM)
cdt_register_tool_target(sysio-pp)

# The stock WABT converters (wat2wasm / wasm2wat) ship from the wabt port's
# `tools` feature; expose them under the historical sysio-* names. wabt_DIR
# points at <triplet>/share/wabt, so the tools live at <triplet>/tools/wabt.
get_filename_component(_wabt_tools "${wabt_DIR}/../../tools/wabt" ABSOLUTE)
if(NOT EXISTS "${_wabt_tools}/wat2wasm")
   message(FATAL_ERROR "wabt converter tools not found at ${_wabt_tools} — "
                       "ensure the wabt dependency includes the 'tools' feature")
endif()
add_custom_command(TARGET sysio-pp POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy "${_wabt_tools}/wat2wasm"  "${CDT_TOOLS_BIN_DIR}/sysio-wast2wasm"
   COMMAND ${CMAKE_COMMAND} -E copy "${_wabt_tools}/wasm2wat"  "${CDT_TOOLS_BIN_DIR}/sysio-wasm2wast"
   VERBATIM)
