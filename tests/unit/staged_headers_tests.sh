#!/bin/bash
# Guards the staged header tree under <build>/include against stale files.
#
# Header staging used to be a configure-time file(COPY), which is additive: deleting a
# source header left the staged copy behind forever, so install/CPack kept shipping a
# removed API and native consumers could compile against a header that disagreed with
# the rebuilt library. Reusing a build tree across such a deletion never recovered,
# because the ExternalProject's configure step is stamped and does not re-run.
#
# stage_cdt_tree (cmake/stage_cdt_tree.cmake) now prunes before it copies. This test
# pins the resulting invariant -- every staged header has a source counterpart -- so it
# catches ANY future stale staging, not just the deletion that prompted it.
#
# Usage: staged_headers_tests.sh <build_dir> <source_dir> <native_enabled: 0|1>
#
# The caller passes $<BOOL:${ENABLE_NATIVE_COMPILER}>, the same canonicalization the stage
# target uses. Comparing the raw cache spelling here meant a valid setting like
# -DENABLE_NATIVE_COMPILER=TRUE staged the native trees while this script took its OFF branch.
set -euo pipefail

BUILD_DIR="$1"
SOURCE_DIR="$2"
NATIVE_ENABLED="${3:-1}"
INCLUDE_DIR="${BUILD_DIR}/include"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# Map a staged path back to the source file it was copied from. The three staged trees
# come from two source trees, and one destination nests inside another:
#
#   include/sysiolib/native/<rest>  <-  libraries/native/native/<rest>
#   include/sysiolib/<rest>         <-  libraries/sysiolib/<rest>
#   include/sysio/native/<rest>     <-  libraries/native/<rest>
#
# The sysiolib/native rule is tested first because it is the more specific prefix.
# Maps a staged path back to the source it was copied from, for every tree
# stage_cdt_tree.cmake owns. The vendored four were added when staging took them over from
# their configure-time copies; leaving them out of this map is what let that half of the
# rework ship untested.
source_for() {
    local staged="$1"
    case "$staged" in
        sysiolib/native/*)   echo "${SOURCE_DIR}/libraries/native/native/${staged#sysiolib/native/}" ;;
        sysiolib/*)          echo "${SOURCE_DIR}/libraries/${staged}" ;;
        sysio/native/*)      echo "${SOURCE_DIR}/libraries/native/${staged#sysio/native/}" ;;
        libcxx/*)            echo "${SOURCE_DIR}/libraries/libc++/cdt-libcxx/include/${staged#libcxx/}" ;;
        bluegrass/*)         echo "${SOURCE_DIR}/libraries/meta_refl/include/${staged}" ;;
        boost/preprocessor/*) echo "${SOURCE_DIR}/libraries/boost/include/${staged}" ;;
        # libc is stitched together from three source roots, so a staged file legitimately
        # matches any one of them; first hit wins.
        libc/*)
            local rest="${staged#libc/}" root
            for root in "libraries/libc/cdt-musl/include" \
                        "libraries/libc/cdt-musl/src/internal" \
                        "libraries/libc/cdt-musl/arch/eos"; do
                [ -f "${SOURCE_DIR}/${root}/${rest}" ] && { echo "${SOURCE_DIR}/${root}/${rest}"; return; }
            done
            # Not found under any root -- report the first so the failure names a real path.
            echo "${SOURCE_DIR}/libraries/libc/cdt-musl/include/${rest}"
            ;;
        *)                   echo "" ;;
    esac
}

echo "=== Staged Header Tests ==="

if [ ! -d "$INCLUDE_DIR" ]; then
    fail "staged include directory exists (${INCLUDE_DIR})"
    echo "Results: ${PASS} passed, ${FAIL} failed"
    exit 1
fi

# Every tree stage_cdt_tree.cmake owns, vendored ones included. A staged file with no source
# counterpart is one the pruning step failed to remove -- the whole point of the rework.
staged_count=0
stale=()
while IFS= read -r abs; do
    rel="${abs#${INCLUDE_DIR}/}"
    src="$(source_for "$rel")"
    [ -z "$src" ] && continue
    staged_count=$((staged_count + 1))
    [ -f "$src" ] || stale+=("$rel")
# No extension filter on the vendored trees: libc++ ships extensionless headers (<vector>,
# <cstdint>), which a '*.h;*.hpp' find would skip entirely -- and those are exactly the files
# a whole-directory copy stages and an extension-filtered one would have dropped.
done < <( { find "${INCLUDE_DIR}/sysiolib" "${INCLUDE_DIR}/sysio" \
                 \( -name '*.h' -o -name '*.hpp' \) -type f 2>/dev/null
            find "${INCLUDE_DIR}/libc" "${INCLUDE_DIR}/libcxx" \
                 "${INCLUDE_DIR}/boost/preprocessor" "${INCLUDE_DIR}/bluegrass" \
                 -type f 2>/dev/null; } )

if [ "$staged_count" -eq 0 ]; then
    fail "found staged CDT headers to check (none under ${INCLUDE_DIR})"
else
    pass "found ${staged_count} staged CDT headers"
fi

# Per destination, not just in aggregate. A single total cannot show that every tree was
# staged: dropping the bluegrass copy from stage_cdt_tree.cmake leaves the count nonzero on
# the strength of the other five, and the test stays green.
# count_files: 0 for a missing directory rather than a non-zero find. Under `set -euo
# pipefail` an unguarded `find` on an absent path aborts the suite mid-run -- the failure this
# check exists to report is exactly when the path is absent, so it would never be printed.
count_files() { [ -d "$1" ] || { echo 0; return 0; }; find "$1" -type f 2>/dev/null | wc -l; }

# Every destination stage_cdt_tree.cmake populates in EVERY configuration. Named once because
# three checks walk it -- this build tree, a fresh scratch staging, and the isolated OFF build
# below -- and a tree added to the script but missed in one of them is a gap the others cannot
# report.
readonly NON_NATIVE_DESTS=(sysiolib libc libcxx boost/preprocessor bluegrass)

# Every native-host archive a native-enabled build stages into lib/, from the POST_BUILD copies
# in libraries/{native,sysiolib,libc,libc++,rt}/CMakeLists.txt. The ON -> OFF prune must remove
# ALL of them: seeding only a couple left `file(GLOB ... libnative*)` free to narrow to those
# names while the rest survived a reconfigure to OFF and were packaged, still carrying the
# previous build's symbols. libnative_c++.a is the one that matters most -- its plus signs are
# what a hand-written character class drops.
readonly NATIVE_ARCHIVES=(libnative.a libnative_sysio.a libnative_c.a libnative_c++.a libnative_rt.a)

# Count the files under a NON-NATIVE destination, with the native subtree that nests inside
# one of them excluded. Staging copies native/native into include/sysiolib/native, so a plain
# recursive count of include/sysiolib is satisfied by the four native headers alone: gating the
# main sysiolib copy on `NOT STAGE_NATIVE` dropped all 63 regular headers from a native-ON
# build and every assertion stayed green.
count_non_native_files() {
    [ -d "$1" ] || { echo 0; return 0; }
    find "$1" -type f -not -path '*/sysiolib/native/*' 2>/dev/null | wc -l
}

# Require each of those to be present AND non-empty under the include root $1, suffixing each
# result with $2. The file count, not the directory: a staging regression that created the
# destinations and copied nothing would ship a package with no public headers at all while a
# directory-existence check stayed green.
require_non_native_dests() {   # $1=include root  $2=label suffix
    local root="$1" label="$2" dest n
    for dest in "${NON_NATIVE_DESTS[@]}"; do
        n="$(count_non_native_files "${root}/${dest}")"
        if [ "$n" -gt 0 ]; then
            pass "${dest} is staged${label} (${n} files)"
        else
            fail "${dest} is staged${label}"
            echo "    nothing under ${root}/${dest}"
        fi
    done
}

require_non_native_dests "$INCLUDE_DIR" ""

if [ "${#stale[@]}" -eq 0 ]; then
    pass "every staged header has a source counterpart"
else
    fail "every staged header has a source counterpart"
    echo "    ${#stale[@]} staged header(s) no longer exist in libraries/:"
    for f in "${stale[@]}"; do echo "      include/${f}"; done
    echo "    stage_cdt_tree should have pruned these; see cmake/stage_cdt_tree.cmake"
fi

# Pruning, per destination -- in an ISOLATED tree. An earlier version planted sentinels in the
# live ${INCLUDE_DIR} and re-ran the staging script there, which wipes and repopulates the very
# headers other tests are compiling against; under `ctest -j` that raced toolchain_tests and
# abi_version_tests. Staging into a scratch destination proves the same property and touches
# nothing shared.
if ! command -v cmake > /dev/null 2>&1; then
    echo "  SKIP: cmake not on PATH (per-tree prune)"
else
    PRUNE_SCRATCH="$(mktemp -d)"
    if ! cmake -DSTAGE_SOURCE_DIR="${SOURCE_DIR}/libraries" \
               -DSTAGE_BINARY_DIR="${PRUNE_SCRATCH}" \
               -DSTAGE_NATIVE="${NATIVE_ENABLED}" \
               -P "${SOURCE_DIR}/cmake/stage_cdt_tree.cmake" > "${PRUNE_SCRATCH}/stage.log" 2>&1; then
        fail "the staging script populates a fresh tree"
        sed 's/^/      /' "${PRUNE_SCRATCH}/stage.log"
    else
        # What the script produces IN THIS MODE, before any sentinel is planted. The live tree
        # checked above is a snapshot of whatever the last build left, so a staging regression
        # is invisible there until someone rebuilds; this runs the script and looks at its
        # actual output. Gating the main sysiolib copy on `NOT STAGE_NATIVE` -- which drops all
        # 63 regular headers from a native-ON build and leaves only the four under
        # sysiolib/native -- is caught here and nowhere else.
        require_non_native_dests "${PRUNE_SCRATCH}/include" " (fresh staging)"

        planted=0
        for dest in "${NON_NATIVE_DESTS[@]}"; do
            if [ -d "${PRUNE_SCRATCH}/include/${dest}" ]; then
                : > "${PRUNE_SCRATCH}/include/${dest}/zz_stale_probe.hpp" && planted=$((planted + 1))
            else
                fail "fresh staging created ${dest}"
            fi
        done
        if [ "$planted" -ne "${#NON_NATIVE_DESTS[@]}" ]; then
            fail "planted a stale sentinel in each staged tree (planted ${planted}, expected ${#NON_NATIVE_DESTS[@]})"
        elif ! cmake -DSTAGE_SOURCE_DIR="${SOURCE_DIR}/libraries" \
                     -DSTAGE_BINARY_DIR="${PRUNE_SCRATCH}" \
                     -DSTAGE_NATIVE="${NATIVE_ENABLED}" \
                     -P "${SOURCE_DIR}/cmake/stage_cdt_tree.cmake" > /dev/null 2>&1; then
            fail "the staging script re-runs cleanly"
        else
            survivors="$(find "${PRUNE_SCRATCH}/include" -name 'zz_stale_probe.hpp' 2>/dev/null | wc -l)"
            if [ "$survivors" -eq 0 ]; then
                pass "a stale file is pruned from every staged tree"
            else
                fail "a stale file is pruned from every staged tree"
                find "${PRUNE_SCRATCH}/include" -name 'zz_stale_probe.hpp' \
                    | sed "s|${PRUNE_SCRATCH}/include/|      |"
            fi
        fi
    fi
    rm -rf "$PRUNE_SCRATCH"
fi

# With native mode off, the native headers must not be staged at all. They are pruned
# unconditionally rather than inside the STAGE_NATIVE branch, because a build tree whose
# ENABLE_NATIVE_COMPILER flipped ON -> OFF would otherwise keep the previous build's copy
# -- and InstallCDT.cmake installs the whole include tree, so the OFF package would ship
# an API it was configured not to build. The counterpart check above cannot catch that:
# those files still have source counterparts, they simply should not be there.
if [ "$NATIVE_ENABLED" = "1" ]; then
    # BOTH destinations: staging owns include/sysio/native and include/sysiolib/native, and
    # checking only the first left the second free to disappear with the suite still green.
    # A nonzero file count, not merely the directory. A staging-pattern regression that
    # created the destinations and copied nothing would ship a package with no native API
    # while a directory-existence check stayed green.
    missing_native=()
    for d in "${INCLUDE_DIR}/sysio/native" "${INCLUDE_DIR}/sysiolib/native"; do
        n="$(count_files "$d")"
        [ "$n" -gt 0 ] || missing_native+=("$d ($n files)")
    done
    if [ "${#missing_native[@]}" -eq 0 ]; then
        pass "both native header trees are staged and non-empty (native enabled)"
    else
        fail "both native header trees are staged and non-empty (native enabled)"
        for d in "${missing_native[@]}"; do echo "      empty or absent: $d"; done
    fi
else
    leftovers=()
    for d in "${INCLUDE_DIR}/sysio/native" "${INCLUDE_DIR}/sysiolib/native"; do
        [ -d "$d" ] && leftovers+=("$d")
    done
    # The native-HOST archives are copied into lib/ by POST_BUILD commands that only exist
    # while native mode is on. They survive a reconfigure to OFF, and InstallCDT installs lib/
    # wholesale, so a stale one gets packaged carrying the previous build's symbols.
    #
    # libnative* only. libsf.a is WebAssembly and is built in every configuration, so it is
    # required below rather than forbidden here -- listing it as a leftover contradicted the
    # isolated probe, which requires the same file to survive.
    for f in "${BUILD_DIR}"/lib/libnative*; do
        [ -e "$f" ] && leftovers+=("$f")
    done
    if [ "${#leftovers[@]}" -eq 0 ]; then
        pass "native headers and archives are absent (native disabled)"
    else
        fail "native headers and archives are absent (native disabled)"
        for d in "${leftovers[@]}"; do echo "      still staged: $d"; done
    fi

    # ...and the wasm softfloat archive must be PRESENT, in this mode as in any other.
    if [ -e "${BUILD_DIR}/lib/libsf.a" ]; then
        pass "libsf.a is present (native disabled)"
    else
        fail "libsf.a is present (native disabled)"
        echo "      cdt-ld links -lsf for --use-rt and the --fquery modes"
    fi
fi

# --- native-disabled configuration, always run ------------------------------------------
#
# Both workflows leave ENABLE_NATIVE_COMPILER at its ON default, so every OFF assertion above
# is dead in CI -- and the scratch prune probe seeds a fake libsf.a rather than building one,
# so it proves only that pruning spares the file. Re-gating add_subdirectory(native), or the sf
# target inside it, would leave all of that green while a clean OFF package again shipped no
# softfloat archive.
#
# Configuring is enough to catch that and costs seconds: the generated build graph either
# contains the `sf` target or it does not. Building it is left to the OFF matrix leg.
echo "-- native-disabled configuration --"

if ! command -v cmake > /dev/null 2>&1; then
    echo "  SKIP: cmake not on PATH"
elif [ ! -f "${BUILD_DIR}/lib/cmake/cdt/CDTWasmToolchain.cmake" ]; then
    echo "  SKIP: no staged CDT toolchain file to configure against"
else
    OFFDIR="$(mktemp -d)"
    # out/lib must exist before the build: the archives are staged by POST_BUILD
    # `cmake -E copy <archive> <BASE_BINARY_DIR>/lib`, which writes a FILE named lib when the
    # directory is absent. The real build tree always has it; an isolated probe must make it.
    mkdir -p "${OFFDIR}/out/lib"
    if ! cmake -S "${SOURCE_DIR}/libraries" -B "${OFFDIR}" -G Ninja \
               -DCMAKE_BUILD_TYPE=Release \
               -DCMAKE_TOOLCHAIN_FILE="${BUILD_DIR}/lib/cmake/cdt/CDTWasmToolchain.cmake" \
               -DCDT_BIN="${BUILD_DIR}/lib/cmake/cdt/" \
               -DBASE_BINARY_DIR="${OFFDIR}/out" \
               -D__APPLE=FALSE \
               -DENABLE_NATIVE_COMPILER=OFF > "${OFFDIR}/cfg.log" 2>&1; then
        fail "the libraries project configures with ENABLE_NATIVE_COMPILER=OFF"
        sed 's/^/      /' "${OFFDIR}/cfg.log"
    else
        pass "the libraries project configures with ENABLE_NATIVE_COMPILER=OFF"

        # BUILD it, do not merely list the targets. `ninja -t targets` shows a declared target
        # even when it is EXCLUDE_FROM_ALL, and listing never runs the POST_BUILD copy into
        # lib/ -- so either change would leave a listing check green while a default OFF
        # package still shipped no softfloat archive. Building the default graph proves the
        # target is reachable from `all` AND that the archive is staged. It costs a few
        # seconds: these objects are already in the compiler cache from the main build.
        if ! ninja -C "${OFFDIR}" > "${OFFDIR}/build.log" 2>&1; then
            fail "the libraries project builds with ENABLE_NATIVE_COMPILER=OFF"
            tail -20 "${OFFDIR}/build.log" | sed 's/^/      /'
        else
            pass "the libraries project builds with ENABLE_NATIVE_COMPILER=OFF"

            if [ -f "${OFFDIR}/out/lib/libsf.a" ]; then
                pass "an OFF build stages libsf.a"
                # ...and it is WebAssembly, not a host archive. cdt-ld links it with -lsf for
                # --use-rt and the --fquery modes, so a host-built one would be useless.
                probe_dir="${OFFDIR}/probe"; mkdir -p "$probe_dir"
                ( cd "$probe_dir" && "${BUILD_DIR}/bin/llvm-ar" x "${OFFDIR}/out/lib/libsf.a" ) \
                    > /dev/null 2>&1 || true
                # -print -quit, not `| head -1`: head closes the pipe after one line, find
                # takes SIGPIPE, and under `set -o pipefail` the assignment fails with 141 --
                # which `set -e` turns into a silent early exit mid-suite. Also parenthesised,
                # so the -o binds to the two -name tests rather than to -print.
                first_obj="$(find "$probe_dir" \( -name '*.obj' -o -name '*.o' \) -print -quit 2>/dev/null)"
                if [ -n "$first_obj" ] && file -b "$first_obj" | grep -qi "webassembly"; then
                    pass "the staged libsf.a contains WebAssembly objects"
                else
                    fail "the staged libsf.a contains WebAssembly objects"
                    echo "      got: $(file -b "${first_obj:-<no object extracted>}" 2>/dev/null)"
                fi
            else
                fail "an OFF build stages libsf.a"
                echo "      cdt-ld links -lsf for --use-rt and the --fquery modes"
                ls "${OFFDIR}/out/lib" 2>/dev/null | sed 's/^/      staged: /'
            fi

            # The required header trees ARE staged. Everything else this probe asserts is
            # negative -- what an OFF build must not produce -- so removing the OFF sysiolib,
            # libc, libcxx, boost/preprocessor and bluegrass outputs altogether left every
            # assertion green while the package shipped no public headers. The live tree above
            # cannot cover this: it is configured ON, so a regression gated on the OFF branch
            # stages them there and is invisible.
            require_non_native_dests "${OFFDIR}/out/include" " (native disabled)"

            # ...and no native HEADER tree is staged. Checking lib/ alone does not pin the
            # CMake-to-staging wiring: forcing STAGE_NATIVE=1 in libraries/CMakeLists.txt
            # leaves these populated in an OFF build, and InstallCDT installs the whole
            # include tree, so they would ship.
            stray_hdrs=()
            for d in "${OFFDIR}/out/include/sysio/native" "${OFFDIR}/out/include/sysiolib/native"; do
                n="$(count_files "$d")"
                [ "$n" -eq 0 ] || stray_hdrs+=("$d ($n files)")
            done
            if [ "${#stray_hdrs[@]}" -eq 0 ]; then
                pass "an OFF build stages no native header tree"
            else
                fail "an OFF build stages no native header tree"
                for d in "${stray_hdrs[@]}"; do echo "      staged: $d"; done
            fi

            # ...while no native-HOST archive is produced. [^[:space:]]* rather than [a-z_]*:
            # the real targets include libnative_c++.a, whose plus signs a
            # letters-and-underscores class silently excludes.
            stray_native="$(ls "${OFFDIR}/out/lib" 2>/dev/null | grep -E "^libnative[^[:space:]]*\.a$" || true)"
            if [ -z "$stray_native" ]; then
                pass "an OFF build stages no libnative* archive"
            else
                fail "an OFF build stages no libnative* archive"
                sed 's/^/      staged: /' <<< "$stray_native"
            fi
        fi
    fi
    rm -rf "${OFFDIR}"
fi

# --- ON -> OFF prune, in an isolated tree ------------------------------------------
#
# The checks above only describe the mode this build was configured in, and
# ENABLE_NATIVE_COMPILER defaults ON with neither workflow overriding it -- so the OFF
# assertions never ran in CI. A clean OFF build would not prove the prune either: it has no
# stale native outputs to remove. So drive the staging script directly against a scratch tree
# seeded the way a previous ON build leaves one, which is mode-independent and always runs.
echo "-- ON -> OFF prune (isolated tree) --"

if ! command -v cmake > /dev/null 2>&1; then
    echo "  SKIP: cmake not on PATH"
else
    SCRATCH="$(mktemp -d)"
    trap 'rm -rf "$SCRATCH"' EXIT
    mkdir -p "${SCRATCH}/lib" "${SCRATCH}/include/sysio/native" "${SCRATCH}/include/sysiolib/native"
    for f in "${NATIVE_ARCHIVES[@]}" libsf.a libc.a; do echo stale > "${SCRATCH}/lib/${f}"; done
    : > "${SCRATCH}/include/sysio/native/sentinel.hpp"
    : > "${SCRATCH}/include/sysiolib/native/sentinel.hpp"

    if cmake -DSTAGE_SOURCE_DIR="${SOURCE_DIR}/libraries" -DSTAGE_BINARY_DIR="${SCRATCH}" \
             -DSTAGE_NATIVE=0 -P "${SOURCE_DIR}/cmake/stage_cdt_tree.cmake" \
             > "${SCRATCH}/stage.log" 2>&1; then
        # libnative* and the native header trees only. libsf.a is asserted separately, and
        # positively: it is a WebAssembly archive built in every configuration.
        leftovers=()
        for f in "${NATIVE_ARCHIVES[@]}"; do
            [ -e "${SCRATCH}/lib/${f}" ] && leftovers+=("${SCRATCH}/lib/${f}")
        done
        for f in "${SCRATCH}/include/sysio/native" "${SCRATCH}/include/sysiolib/native"; do
            [ -e "$f" ] && leftovers+=("$f")
        done
        if [ "${#leftovers[@]}" -eq 0 ]; then
            pass "STAGE_NATIVE=0 prunes stale native archives and header trees"
        else
            fail "STAGE_NATIVE=0 prunes stale native archives and header trees"
            for f in "${leftovers[@]}"; do echo "      survived: $f"; done
        fi

        # libsf.a must SURVIVE. It is the WebAssembly softfloat archive cdt-ld links with
        # -lsf for --use-rt and the --fquery modes, not a native-host archive -- it is only
        # declared under libraries/native/, which is why an OFF configure never rebuilds it.
        # Pruning it would leave an OFF package unable to link those modes.
        if [ -e "${SCRATCH}/lib/libsf.a" ]; then
            pass "STAGE_NATIVE=0 keeps libsf.a (a wasm archive, not a native one)"
        else
            fail "STAGE_NATIVE=0 keeps libsf.a (a wasm archive, not a native one)"
        fi

        # An unrelated archive must be left alone -- the prune is targeted, not a wipe.
        if [ -e "${SCRATCH}/lib/libc.a" ]; then
            pass "the prune leaves unrelated archives alone"
        else
            fail "the prune leaves unrelated archives alone"
        fi
    else
        fail "stage_cdt_tree.cmake runs with STAGE_NATIVE=0"
        sed 's/^/    /' "${SCRATCH}/stage.log"
    fi
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
