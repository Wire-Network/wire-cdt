include(GNUInstallDirs)

# Tools are built with the same host compiler, toolchain file, and vcpkg
# installation as the parent project. Keep them in the main CMake graph so
# vcpkg manifest and package changes naturally invalidate their find_package()
# results instead of relying on ExternalProject cache invalidation.
if(VCPKG_TARGET_TRIPLET)
   set(_cdt_vcpkg_target_triplet "${VCPKG_TARGET_TRIPLET}")
else()
   set(_cdt_vcpkg_target_triplet x64-linux)
   message(WARNING "VCPKG_TARGET_TRIPLET is not set; probing vcpkg host tools with fallback triplet '${_cdt_vcpkg_target_triplet}'")
endif()

if(VCPKG_INSTALLED_DIR)
   set(_vcpkg_root "${VCPKG_INSTALLED_DIR}")
else()
   set(_vcpkg_root "${CMAKE_BINARY_DIR}/vcpkg_installed")
endif()

set(_vcpkg_triplet_root "${_vcpkg_root}/${_cdt_vcpkg_target_triplet}")
set(_vcpkg_share "${_vcpkg_triplet_root}/share")
if(EXISTS "${_vcpkg_share}/llvm/LLVMConfig.cmake")
   set(LLVM_DIR "${_vcpkg_share}/llvm")
endif()
if(EXISTS "${_vcpkg_share}/clang/ClangConfig.cmake")
   set(Clang_DIR "${_vcpkg_share}/clang")
endif()
if(EXISTS "${_vcpkg_triplet_root}")
   list(PREPEND CMAKE_PREFIX_PATH "${_vcpkg_triplet_root}")
else()
   message(WARNING "vcpkg triplet install path '${_vcpkg_triplet_root}' does not exist; LLVM/Clang package discovery will use the existing CMAKE_PREFIX_PATH")
endif()

add_subdirectory("${CMAKE_SOURCE_DIR}/tools" "${CMAKE_BINARY_DIR}/tools")
add_subdirectory("${CMAKE_SOURCE_DIR}/tools/toolchain-tester" "${CMAKE_BINARY_DIR}/tools/toolchain-tester")

add_custom_target(CDTTools ALL)
add_dependencies(CDTTools ${CDT_TOOLS_TARGETS})
