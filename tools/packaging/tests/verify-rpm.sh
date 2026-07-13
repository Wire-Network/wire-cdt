#!/bin/sh
# S3 gate: RPM payload + containerized install + contract-compile smoke.
# Usage: verify-rpm.sh <base-rpm> [dev-rpm]
set -e
r="$1"; dev="$2"
[ -f "$r" ] || { echo "S3 FAIL: rpm not found: $r"; exit 1; }
fail() { echo "S3 FAIL: $1"; exit 1; }
l=$(rpm -qlp "$r" 2>/dev/null)
for f in /usr/cdt/bin/cdt-cpp /usr/cdt/bin/cdt-protoc /usr/cdt/bin/cdt-protoc-gen-zpp /usr/cdt/lib/cmake/cdt/cdt-config.cmake /usr/cdt/lib/libsysio.a /usr/cdt/licenses/cdt.license /usr/cdt/cdt.imports; do
    echo "$l" | grep -qx "$f" || fail "payload missing $f"
done
echo "$l" | grep -q "libnative" && fail "base rpm leaks native dev libs"
v=$(rpm -qp --qf '%{VERSION}' "$r" 2>/dev/null)
case "$v" in *-*) fail "version tag contains hyphen: $v" ;; esac
if [ -n "$dev" ]; then
    dl=$(rpm -qlp "$dev" 2>/dev/null)
    echo "$dl" | grep -qx "/usr/cdt/lib/libnative.a" || fail "dev payload missing libnative.a"
    rpm -qp --requires "$dev" 2>/dev/null | grep -q "cdt = " || fail "dev rpm missing versioned base requirement"
fi
smoke=$(mktemp -d); trap 'rm -rf "$smoke"' EXIT
cat > "$smoke/hello.cpp" <<'SRC'
#include <sysio/sysio.hpp>
using namespace sysio;
CONTRACT hello : public contract {
 public:
   using contract::contract;
   ACTION hi(name user) { print("hi,", user); }
};
SRC
pkgdir=$(cd "$(dirname "$r")" && pwd)
devbase=""; [ -n "$dev" ] && devbase=$(basename "$dev")
docker run --rm -v "$pkgdir":/pkg -v "$smoke":/smoke fedora:latest bash -ec "
    dnf install -y /pkg/$(basename "$r") ${devbase:+/pkg/$devbase} > /tmp/inst.log 2>&1 || { tail -15 /tmp/inst.log; exit 1; }
    test -x /usr/cdt/bin/cdt-cpp || { echo 'SMOKE FAIL: cdt-cpp not installed/executable'; exit 1; }
    # Functional smoke: the packaged toolchain must compile a contract.
    # abigen emits the .abi into the working directory, so compile from /smoke.
    cd /smoke
    /usr/cdt/bin/cdt-cpp -abigen -contract hello -o hello.wasm hello.cpp || { echo 'SMOKE FAIL: contract compile'; exit 1; }
    test -s hello.wasm || { echo 'SMOKE FAIL: hello.wasm missing/empty'; exit 1; }
    test -s hello.abi || { echo 'SMOKE FAIL: hello.abi missing/empty'; exit 1; }
" || fail "container install / contract-compile smoke"
echo "S3 PASS: $r ${dev:+(+ $dev)} (contract smoke: wasm+abi produced)"
