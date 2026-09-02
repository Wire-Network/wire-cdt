# Stage the CDT-owned parts of the build tree -- headers into <build>/include and the
# native archives in <build>/lib -- pruning first.
#
# Run in script mode (`cmake -P`) from the `stage_cdt_tree` build target, not at
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
# `file(COPY)` preserves source timestamps, so re-running every build neither churns
# mtimes nor triggers downstream rebuilds. (It also skips files already current at the
# destination, but that never applies here: both destinations are REMOVE_RECURSE'd
# below before either is repopulated.)
#
# EVERY tree staged into <build>/include is handled here. The four vendored ones -- libc,
# libcxx, boost/preprocessor and bluegrass -- were left as configure-time copies in an
# earlier revision, which meant deleting a header from the cdt-musl or cdt-libcxx submodule
# left the staged copy shipping forever, exactly the bug this script exists to fix. They are
# pruned and recopied on the same schedule now.
#
# Inputs (via -D):
#   STAGE_SOURCE_DIR - the repo's libraries/ directory
#   STAGE_BINARY_DIR - BASE_BINARY_DIR, whose include/ subtree is staged into
#   STAGE_NATIVE     - truthy when ENABLE_NATIVE_COMPILER is on

foreach(var STAGE_SOURCE_DIR STAGE_BINARY_DIR)
   if(NOT DEFINED ${var})
      message(FATAL_ERROR "stage_cdt_tree.cmake: ${var} is required")
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
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/libc")
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/libcxx")
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/boost/preprocessor")
file(REMOVE_RECURSE "${STAGE_BINARY_DIR}/include/bluegrass")

# sysiolib -> include/sysiolib
file(COPY "${STAGE_SOURCE_DIR}/sysiolib"
     DESTINATION "${STAGE_BINARY_DIR}/include"
     ${header_patterns})

# The native-host archives are copied into lib/ by POST_BUILD commands that exist only while
# ENABLE_NATIVE_COMPILER is on. Reconfiguring a reused tree to OFF removes those targets but
# not the files they already copied, and InstallCDT.cmake installs lib/ wholesale -- so an OFF
# build packaged archives its own configuration never produced, still carrying whatever symbols
# the last ON build put in them.
#
# libnative* ONLY. libsf.a is a WebAssembly archive, not a native-host one: cdt-ld links it
# with -lsf for --use-rt and every --fquery mode (compiler_options.hpp.in), and the base
# install ships it. It merely happens to be declared in libraries/native/CMakeLists.txt, which
# is why an OFF configure does not rebuild it -- so deleting it here would strip a working
# copy from a reused tree and leave an OFF package unable to link those modes.
if(NOT STAGE_NATIVE)
   file(GLOB stale_native "${STAGE_BINARY_DIR}/lib/libnative*")
   if(stale_native)
      file(REMOVE ${stale_native})
   endif()
endif()

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

# The vendored trees. libc and libcxx copy whole directories rather than header-matching,
# because musl and libc++ both ship extensionless headers (<cstdint>, <vector>, ...) that a
# "*.h;*.hpp" filter would drop.
file(COPY "${STAGE_SOURCE_DIR}/libc/cdt-musl/include/"       DESTINATION "${STAGE_BINARY_DIR}/include/libc/")
file(COPY "${STAGE_SOURCE_DIR}/libc/cdt-musl/src/internal/"  DESTINATION "${STAGE_BINARY_DIR}/include/libc/")
file(COPY "${STAGE_SOURCE_DIR}/libc/cdt-musl/arch/eos/"      DESTINATION "${STAGE_BINARY_DIR}/include/libc/")
file(COPY "${STAGE_SOURCE_DIR}/libc++/cdt-libcxx/include/"   DESTINATION "${STAGE_BINARY_DIR}/include/libcxx")
file(COPY "${STAGE_SOURCE_DIR}/boost/include/boost/preprocessor"
     DESTINATION "${STAGE_BINARY_DIR}/include/boost")
file(COPY "${STAGE_SOURCE_DIR}/meta_refl/include/bluegrass"
     DESTINATION "${STAGE_BINARY_DIR}/include")
