include(GNUInstallDirs)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Export compile_commands.json" FORCE)

# Tools are built with the same host compiler, toolchain file, and vcpkg
# installation as the parent project. Keep them in the main CMake graph so
# vcpkg manifest and package changes naturally invalidate their find_package()
# results instead of relying on ExternalProject cache invalidation.
set(_vcpkg_share "${CMAKE_BINARY_DIR}/vcpkg_installed/x64-linux/share")
if(EXISTS "${_vcpkg_share}/llvm/LLVMConfig.cmake")
   set(LLVM_DIR "${_vcpkg_share}/llvm")
endif()
if(EXISTS "${_vcpkg_share}/clang/ClangConfig.cmake")
   set(Clang_DIR "${_vcpkg_share}/clang")
endif()
if(VCPKG_INSTALLED_DIR AND VCPKG_TARGET_TRIPLET)
   list(PREPEND CMAKE_PREFIX_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/tools" "${CMAKE_BINARY_DIR}/tools")
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/toolchain-tester" "${CMAKE_BINARY_DIR}/tools/toolchain-tester")

add_custom_target(CDTTools ALL DEPENDS ${CDT_TOOLS_TARGETS})
