set(LLVM_ENABLE_EH 1)

function(add_clang_plugin name src)
  add_llvm_library(${name} PARTIAL_SOURCES_INTENDED MODULE ${src} PLUGIN_TOOL clang)
  target_include_directories(${name} SYSTEM PRIVATE "${LLVM_INCLUDE_DIRS};${CLANG_INCLUDE_DIRS}")

  if (NOT LLVM_ENABLE_RTTI)
    target_compile_options(${name} PRIVATE -fno-rtti)
  endif()

  set_target_properties(${name} PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

  if(APPLE)
    target_link_libraries(${name} PUBLIC clang-cpp)
  endif()

  install(TARGETS ${name} LIBRARY DESTINATION ${CDT_INSTALL_PREFIX}/bin)
endfunction()

add_clang_plugin(sysio_attrs sysio_attrs.cpp)
add_clang_plugin(sysio_codegen sysio_codegen.cpp)

target_include_directories(sysio_codegen
  PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../../tools/jsoncons/include)

if(NOT TARGET SysioPlugins)
  add_custom_target(SysioPlugins)
  add_dependencies(SysioPlugins sysio_attrs sysio_codegen)
endif()
