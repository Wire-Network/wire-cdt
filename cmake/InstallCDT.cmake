# PUBLIC ENTRY POINTS -- the tool names that get a /usr/bin symlink in the deb
# and the rpm (cmake/cpack-system-layout.cmake consumes this list via
# CPACK_WIRE_PUBLIC_ENTRY_POINTS). It is appended by the install macros below so
# there is ONE source of truth: the install rules themselves. Everything NOT in
# this list -- notably the bundled clang / clang++ / lld / ld.lld / opt / llc /
# wasm-ld / llvm-* binaries -- stays private to /usr/lib/cdt/bin and must never
# reach /usr/bin, where it would collide with the distro's own toolchain
# packages.
set(CDT_PUBLIC_ENTRY_POINTS "")

macro( cdt_tool_install file )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/tools/bin)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/${file} ${CMAKE_BINARY_DIR}/bin/ )
   install(FILES ${BINARY_DIR}/${file}
      DESTINATION bin COMPONENT base
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
endmacro( cdt_tool_install )

macro( cdt_tool_install_and_symlink file symlink )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/tools/bin)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${BINARY_DIR}/${file} ${CMAKE_BINARY_DIR}/bin/ )
   if(NOT "${file}" STREQUAL "${symlink}")
      add_custom_command( TARGET CDTTools POST_BUILD
         COMMAND ${CMAKE_COMMAND} -E create_symlink ${file} ${CMAKE_BINARY_DIR}/bin/${symlink} )
   endif()
   install(FILES ${BINARY_DIR}/${file}
      DESTINATION bin COMPONENT base
      PERMISSIONS OWNER_READ OWNER_EXECUTE GROUP_READ GROUP_EXECUTE WORLD_READ WORLD_EXECUTE)
   if(NOT "${file}" STREQUAL "${symlink}")
      install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"${file}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/${symlink}\")" COMPONENT base)
   endif()
   # Both the real binary and its alias are public entry points.
   list(APPEND CDT_PUBLIC_ENTRY_POINTS "${file}" "${symlink}")
endmacro( cdt_tool_install_and_symlink )

macro( cdt_cmake_install_and_symlink file symlink )
   set(BINARY_DIR ${CMAKE_BINARY_DIR}/modules)
endmacro( cdt_cmake_install_and_symlink )

macro( cdt_libraries_install)
   execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/lib)
   execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/include)
   # Wasm sysroot archives -> base; native (host) testing archives -> dev.
   # lib/cmake is excluded: the packages ship the dedicated package-configured
   # variants installed from ${CMAKE_BINARY_DIR}/packaging (see CMakeLists.txt).
   install(DIRECTORY ${CMAKE_BINARY_DIR}/lib/ DESTINATION lib COMPONENT base
      PATTERN "libnative*" EXCLUDE
      PATTERN "libsf.a" EXCLUDE
      PATTERN "cmake" EXCLUDE)
   # Guarded on the option, not merely on what happens to be sitting in lib/: a tree
   # reconfigured from native ON to OFF can still hold archives from the previous build.
   # stage_cdt_tree prunes those, and this makes packaging one impossible regardless.
   if(ENABLE_NATIVE_COMPILER)
      install(DIRECTORY ${CMAKE_BINARY_DIR}/lib/ DESTINATION lib COMPONENT dev
         FILES_MATCHING PATTERN "libnative*"
         PATTERN "cmake" EXCLUDE)
   endif()
   install(DIRECTORY ${CMAKE_BINARY_DIR}/include/ DESTINATION include COMPONENT base)
endmacro( cdt_libraries_install )

# Ensure bin/ exists before copying anything into it
add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/bin )

# Copy LLVM tools from tools/bin/ to bin/
foreach(tool llvm-ranlib llvm-ar llvm-nm llvm-objcopy llvm-objdump llvm-readobj llvm-readelf llvm-strip opt llc lld ld.lld clang clang++ wasm-ld)
   add_custom_command( TARGET CDTTools POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/tools/bin/${tool} ${CMAKE_BINARY_DIR}/bin/ 2>/dev/null || true )
   # Install from bin/ (the POST_BUILD `cmake -E copy` output), NOT tools/bin:
   # the tools tree exposes several llvm binaries as symlinks into the vcpkg
   # tool directory, and installing those embeds dead absolute links in the
   # packages; the bin/ copies are dereferenced real files.
   install(PROGRAMS ${CMAKE_BINARY_DIR}/bin/${tool}
      DESTINATION bin COMPONENT base
      OPTIONAL)
endforeach()

# CDT binutils aliases -- cdt-ar, cdt-ranlib, cdt-nm, ... each a SYMLINK onto its
# llvm-* counterpart in the same bin/.
#
# These MUST be installed, not merely created in the build tree:
# CDTWasmToolchain.cmake bakes CMAKE_AR=${CDT_ROOT}/bin/cdt-ar and
# CMAKE_RANLIB=${CDT_ROOT}/bin/cdt-ranlib, so every consumer that builds a STATIC
# library through the packaged toolchain invokes these paths. Without the install
# rule the packages shipped a toolchain file naming binaries the payload did not
# contain, and `add_library(... STATIC ...)` failed at the archive step with
# "Error running link command: No such file or directory". (The deleted
# scripts/generate_tarball.sh created these links by hand, so their absence was a
# payload regression, not a deliberate omission.)
#
# NOT public entry points -- deliberately absent from CDT_PUBLIC_ENTRY_POINTS, so
# they get NO /usr/bin symlink. The cdt- prefix means either choice is
# collision-free, so the tie is broken on consistency with what the list means:
# an entry point is a tool a HUMAN or a consuming build invokes BY NAME off PATH
# (cdt-cpp, cdt-protoc, cdt-pp). These are build-system plumbing addressed only
# by the ABSOLUTE ${CDT_ROOT}/bin/... path the toolchain file bakes, exactly like
# the llvm-ar / llvm-ranlib binaries they point at -- which are themselves
# private to the home. A /usr/bin/cdt-ar would therefore buy nothing and would
# widen the public surface the layout deliberately keeps minimal.
foreach(tool ranlib ar nm objcopy objdump readobj readelf strip)
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND cd ${CMAKE_BINARY_DIR}/bin && ln -sf llvm-${tool} cdt-${tool} 2>/dev/null || true )
   # A RELATIVE symlink onto the sibling llvm-* binary: resolves identically in
   # the build tree, the cpack staging tree and after install, under any prefix.
   install(CODE "execute_process(COMMAND \"${CMAKE_COMMAND}\" -E create_symlink \"llvm-${tool}\" \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}/bin/cdt-${tool}\")" COMPONENT base)
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
   DESTINATION bin COMPONENT base)
# cdt-protoc is installed directly rather than through the macro, so it registers
# itself as a public entry point here (wire-sysio's OPP model generation calls it).
list(APPEND CDT_PUBLIC_ENTRY_POINTS "cdt-protoc")

# Sysio plugins (built by tools project)
foreach(plugin sysio_attrs sysio_codegen)
   set(PLUGIN_FILE ${CMAKE_BINARY_DIR}/tools/bin/${plugin}${CMAKE_SHARED_LIBRARY_SUFFIX})
   add_custom_command( TARGET CDTTools POST_BUILD COMMAND ${CMAKE_COMMAND} -E copy ${PLUGIN_FILE} ${CMAKE_BINARY_DIR}/bin/ )
   install(FILES ${PLUGIN_FILE}
      DESTINATION bin COMPONENT base
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
   DESTINATION include/zpp COMPONENT base)
install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/google/protobuf
   DESTINATION include/google COMPONENT base)

# Install magic_enum headers
install(DIRECTORY ${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/magic_enum
   DESTINATION include COMPONENT base)

cdt_libraries_install()
