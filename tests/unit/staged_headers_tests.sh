#!/bin/bash
# Guards the staged header tree under <build>/include against stale files.
#
# Header staging used to be a configure-time file(COPY), which is additive: deleting a
# source header left the staged copy behind forever, so install/CPack kept shipping a
# removed API and native consumers could compile against a header that disagreed with
# the rebuilt library. Reusing a build tree across such a deletion never recovered,
# because the ExternalProject's configure step is stamped and does not re-run.
#
# stage_cdt_headers (cmake/stage_headers.cmake) now prunes before it copies. This test
# pins the resulting invariant -- every staged header has a source counterpart -- so it
# catches ANY future stale staging, not just the deletion that prompted it.
#
# Usage: staged_headers_tests.sh <build_dir> <source_dir>
set -euo pipefail

BUILD_DIR="$1"
SOURCE_DIR="$2"
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
source_for() {
    local staged="$1"
    case "$staged" in
        sysiolib/native/*) echo "${SOURCE_DIR}/libraries/native/native/${staged#sysiolib/native/}" ;;
        sysiolib/*)        echo "${SOURCE_DIR}/libraries/${staged}" ;;
        sysio/native/*)    echo "${SOURCE_DIR}/libraries/native/${staged#sysio/native/}" ;;
        *)                 echo "" ;;
    esac
}

echo "=== Staged Header Tests ==="

if [ ! -d "$INCLUDE_DIR" ]; then
    fail "staged include directory exists (${INCLUDE_DIR})"
    echo "Results: ${PASS} passed, ${FAIL} failed"
    exit 1
fi

# Only the CDT-owned trees are checked. The vendored trees (libc, libcxx, boost,
# bluegrass) are still staged by their own configure-time copies and are out of scope.
staged_count=0
stale=()
while IFS= read -r abs; do
    rel="${abs#${INCLUDE_DIR}/}"
    src="$(source_for "$rel")"
    [ -z "$src" ] && continue
    staged_count=$((staged_count + 1))
    [ -f "$src" ] || stale+=("$rel")
done < <(find "${INCLUDE_DIR}/sysiolib" "${INCLUDE_DIR}/sysio" \
              \( -name '*.h' -o -name '*.hpp' \) -type f 2>/dev/null)

if [ "$staged_count" -eq 0 ]; then
    fail "found staged CDT headers to check (none under ${INCLUDE_DIR})"
else
    pass "found ${staged_count} staged CDT headers"
fi

if [ "${#stale[@]}" -eq 0 ]; then
    pass "every staged header has a source counterpart"
else
    fail "every staged header has a source counterpart"
    echo "    ${#stale[@]} staged header(s) no longer exist in libraries/:"
    for f in "${stale[@]}"; do echo "      include/${f}"; done
    echo "    stage_cdt_headers should have pruned these; see cmake/stage_headers.cmake"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
