# Stage the CDT-owned header trees into <build>/include, pruning first.
#
# Run in script mode (`cmake -P`) from the `stage_cdt_headers` build target, not at
# configure time. Configure-time `file(COPY)` is additive: it never removes a staged
# copy whose source has been deleted, so a removed header stayed in <build>/include
# forever -- shipped by install/CPack, and visible to native consumers whose compiled
# view could then disagree with the rebuilt library. Reusing a build tree across such
# a deletion is the case this exists to handle, and a configure-time copy cannot,
# because the ExternalProject's configure step is stamped and does not re-run.
#
# The two destinations OVERLAP: sysiolib owns include/sysiolib, and native's second
# copy lands in include/sysiolib/native, a subdirectory of it. Pruning and copying
# from one ordered script is what makes that safe -- two independent steps would race
# to delete each other's output.
#
# `file(COPY)` preserves source timestamps and skips files already current at the
# destination, so re-running every build neither churns mtimes nor triggers
# downstream rebuilds.
#
# Inputs (via -D):
#   STAGE_SOURCE_DIR - the repo's libraries/ directory
#   STAGE_BINARY_DIR - BASE_BINARY_DIR, whose include/ subtree is staged into
#   STAGE_NATIVE     - truthy when ENABLE_NATIVE_COMPILER is on

foreach(var STAGE_SOURCE_DIR STAGE_BINARY_DIR)
   if(NOT DEFINED ${var})
      message(FATAL_ERROR "stage_headers.cmake: ${var} is required")
   endif()
endforeach()

set(header_patterns FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp")

# Prune BOTH trees before either is repopulated, and prune the native tree whether or
# not native mode is on. Pruning it inside the STAGE_NATIVE branch left the previous
# build's native headers staged when a reused tree flipped ENABLE_NATIVE_COMPILER from
# ON to OFF -- and since InstallCDT.cmake installs the whole include tree, an OFF build
# then packaged an API it was configured not to build. (include/sysiolib/native happens
# to vanish with its parent; include/sysio/native has no such parent.)
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/sysiolib")
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/sysio/native")

# sysiolib -> include/sysiolib
file(COPY "${STAGE_SOURCE_DIR}/sysiolib"
     DESTINATION "${STAGE_BINARY_DIR}/include"
     ${header_patterns})

if(STAGE_NATIVE)
   # native -> include/sysio/native
   file(COPY "${STAGE_SOURCE_DIR}/native"
        DESTINATION "${STAGE_BINARY_DIR}/include/sysio"
        ${header_patterns} PATTERN "softfloat" EXCLUDE)

   # native/native -> include/sysiolib/native (inside the tree pruned above)
   file(COPY "${STAGE_SOURCE_DIR}/native/native"
        DESTINATION "${STAGE_BINARY_DIR}/include/sysiolib"
        ${header_patterns} PATTERN "softfloat" EXCLUDE)
endif()
