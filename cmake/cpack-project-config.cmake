# CPack per-generator configuration (CPACK_PROJECT_CONFIG_FILE).
if(CPACK_GENERATOR STREQUAL "TGZ")
   # Portable toolchain tarball: plain `cdt` top-level directory. Monolithic
   # archives take both the artifact name and top dir from
   # CPACK_PACKAGE_FILE_NAME; the package-tgz target restores the versioned
   # artifact name afterwards. Base component only.
   set(CPACK_PACKAGING_INSTALL_PREFIX "/")
   set(CPACK_PACKAGE_FILE_NAME "cdt")
   string(REPLACE ";ALL;/" ";base;/" CPACK_INSTALL_CMAKE_PROJECTS "${CPACK_INSTALL_CMAKE_PROJECTS}")
elseif(CPACK_GENERATOR MATCHES "^(DEB|RPM)$")
   # System packages install the toolchain under /usr/cdt; consumers discover
   # the root dynamically via lib/cmake/cdt/cdt-config.cmake.
   set(CPACK_PACKAGING_INSTALL_PREFIX "/usr/cdt")
   set(CPACK_SET_DESTDIR OFF)
endif()
