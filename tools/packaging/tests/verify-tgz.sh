#!/bin/sh
# S1 gate: portable toolchain tarball layout. Usage: verify-tgz.sh <tarball>
set -e
t="$1"
[ -f "$t" ] || { echo "S1 FAIL: tarball not found: $t"; exit 1; }
fail() { echo "S1 FAIL: $1"; exit 1; }
list=$(tar tzf "$t")
bad=$(echo "$list" | grep -v '^cdt/' || true)
[ -z "$bad" ] || fail "entries outside cdt/: $bad"
for f in bin/cdt-cpp bin/cdt-protoc bin/cdt-protoc-gen-zpp lib/cmake/cdt/cdt-config.cmake lib/cmake/cdt/CDTWasmToolchain.cmake lib/libsysio.a licenses/cdt.license cdt.imports; do
    echo "$list" | grep -qx "cdt/$f" || fail "missing cdt/$f"
done
n=$(echo "$list" | grep -c "libnative" || true)
[ "$n" = "0" ] || fail "portable tarball leaks native dev libs"
echo "S1 PASS: $t"
