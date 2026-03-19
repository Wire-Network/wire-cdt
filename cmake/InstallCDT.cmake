macro( cdt_tool_install file )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/tools/bin)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/${file} ${CMAKE_BINARY_DIR}/bin/ )
   install(FILES ${BINARY_DIR}/${file}
      DESTINATION ${CDT_INSTALL_PREFIX}/bin
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endmacro( cdt_tool_install )

macro( cdt_tool_install_and_symlink file symlink )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/tools/bin)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/${file} ${CMAKE_BINARY_DIR}/bin/ )
   install(FILES ${BINARY_DIR}/${file}
      DESTINATION ${CDT_INSTALL_PREFIX}/bin
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endmacro( cdt_tool_install_and_symlink )

macro( cdt_cmake_install_and_symlink file symlink )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/modules)
endmacro( cdt_cmake_install_and_symlink )

macro( cdt_libraries_install)
   execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib)
   execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include)
   install(DIRECTORY ${CMAKE_BINARY_DIR}/lib/ DESTINATION ${CDT_INSTALL_PREFIX}/lib)
   install(DIRECTORY ${CMAKE_BINARY_DIR}/include/ DESTINATION ${CDT_INSTALL_PREFIX}/include)
endmacro( cdt_libraries_install )

# Ensure bin/ exists before copying anything into it
add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin )

# Copy LLVM tools from tools/bin/ to bin/
foreach(tool llvm-ranlib llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-readobj llvm-readelf llvm-strip opt llc lld ld.lld clang clang++ wasm-ld)
   add_custom_command( TARGET CDTTools POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tools/bin/${tool} ${CMAKE_BINARY_DIR}/bin/ 2>/dev/null || true )
   install(PROGRAMS ${CMAKE_BINARY_DIR}/tools/bin/${tool}
      DESTINATION ${CDT_INSTALL_PREFIX}/bin
      OPTIONAL)
endforeach()

# CDT symlinks
foreach(tool ranlib ar nm objcopy objdump readobj readelf strip)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND cd ${CMAKE_BINARY_DIR}/bin && ln -sf llvm-${tool} cdt-${tool} 2>/dev/null || true )
endforeach()

# CDT tools
foreach(tool pp wast2wasm wasm2wast)
  cdt_tool_install_and_symlink(sysio-${tool} cdt-${tool})
endforeach()

foreach(tool cdt-cc cdt-cpp cdt-ld cdt-abidiff cdt-init cdt-codegen cdt-protoc-gen-zpp)
  cdt_tool_install_and_symlink(${tool} ${tool})
endforeach()

# Install cdt-protoc (protoc from vcpkg, copied during tools build)
add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tools/bin/cdt-protoc ${CMAKE_BINARY_DIR}/bin/ )
install(PROGRAMS ${CMAKE_BINARY_DIR}/tools/bin/cdt-protoc
   DESTINATION ${CDT_INSTALL_PREFIX}/bin)

# Sysio plugins (built by tools project)
foreach(plugin sysio_attrs sysio_codegen)
   set(PLUGIN_FILE ${CMAKE_BINARY_DIR}/tools/bin/${plugin}${CMAKE_SHARED_LIBRARY_SUFFIX})
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${PLUGIN_FILE} ${CMAKE_BINARY_DIR}/bin/ )
   install(FILES ${PLUGIN_FILE}
      DESTINATION ${CDT_INSTALL_PREFIX}/bin
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endforeach()

cdt_cmake_install_and_symlink(cdt-config.cmake cdt-config.cmake)
cdt_cmake_install_and_symlink(CDTWasmToolchain.cmake CDTWasmToolchain.cmake)
cdt_cmake_install_and_symlink(CDTMacros.cmake CDTMacros.cmake)

# Copy protobuf support files to main include dir for contract compilation
add_custom_command( TARGET CDTTools POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include
   COMMAND ${CMAKE_COMMAND} -E copy ${ZPP_BITS_INCLUDE_DIR}/zpp_bits.h ${CMAKE_BINARY_DIR}/include/
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include/zpp
   COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tools/include/zpp/zpp_options.proto ${CMAKE_BINARY_DIR}/include/zpp/
   COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include/google/protobuf
   COMMAND ${CMAKE_COMMAND} -E copy ${ZPP_BITS_INCLUDE_DIR}/google/protobuf/descriptor.proto ${CMAKE_BINARY_DIR}/include/google/protobuf/ )
install(FILES ${CMAKE_BINARY_DIR}/tools/include/zpp/zpp_options.proto
   DESTINATION ${CDT_INSTALL_PREFIX}/include/zpp)
install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/google/protobuf
   DESTINATION ${CDT_INSTALL_PREFIX}/include/google)

# Install magic_enum headers
install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/magic_enum
   DESTINATION ${CDT_INSTALL_PREFIX}/include)

cdt_libraries_install()
