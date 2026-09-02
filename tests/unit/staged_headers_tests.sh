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
for dest in sysiolib libc libcxx boost/preprocessor bluegrass; do
    n="$(find "${INCLUDE_DIR}/${dest}" -type f 2>/dev/null | wc -l)"
    if [ "$n" -gt 0 ]; then
        pass "${dest} is staged (${n} files)"
    else
        fail "${dest} is staged"
        echo "    nothing under ${INCLUDE_DIR}/${dest}"
    fi
done

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
        planted=0
        for dest in sysiolib libc libcxx boost/preprocessor bluegrass; do
            if [ -d "${PRUNE_SCRATCH}/include/${dest}" ]; then
                : > "${PRUNE_SCRATCH}/include/${dest}/zz_stale_probe.hpp" && planted=$((planted + 1))
            else
                fail "fresh staging created ${dest}"
            fi
        done
        if [ "$planted" -ne 5 ]; then
            fail "planted a stale sentinel in each staged tree (planted ${planted}, expected 5)"
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
    if [ -d "${INCLUDE_DIR}/sysio/native" ]; then
        pass "native headers are staged (native enabled)"
    else
        fail "native headers are staged (native enabled)"
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
    for f in libnative.a libnative_sysio.a libsf.a libc.a; do echo stale > "${SCRATCH}/lib/${f}"; done
    : > "${SCRATCH}/include/sysio/native/sentinel.hpp"
    : > "${SCRATCH}/include/sysiolib/native/sentinel.hpp"

    if cmake -DSTAGE_SOURCE_DIR="${SOURCE_DIR}/libraries" -DSTAGE_BINARY_DIR="${SCRATCH}" \
             -DSTAGE_NATIVE=0 -P "${SOURCE_DIR}/cmake/stage_cdt_tree.cmake" \
             > "${SCRATCH}/stage.log" 2>&1; then
        # libnative* and the native header trees only. libsf.a is asserted separately, and
        # positively: it is a WebAssembly archive built in every configuration.
        leftovers=()
        for f in "${SCRATCH}/lib/libnative.a" "${SCRATCH}/lib/libnative_sysio.a" \
                 "${SCRATCH}/include/sysio/native" "${SCRATCH}/include/sysiolib/native"; do
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
