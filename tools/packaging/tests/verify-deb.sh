#!/bin/sh
# S2 gate: DEB payload + containerized install + contract-compile smoke.
# Usage: verify-deb.sh <base-deb> [dev-deb]
set -e
d="$1"; dev="$2"
[ -f "$d" ] || { echo "S2 FAIL: deb not found: $d"; exit 1; }
fail() { echo "S2 FAIL: $1"; exit 1; }
c=$(dpkg-deb -c "$d" | awk '{print $6}')
for f in ./usr/cdt/bin/cdt-cpp ./usr/cdt/bin/cdt-protoc ./usr/cdt/bin/cdt-protoc-gen-zpp ./usr/cdt/lib/cmake/cdt/cdt-config.cmake ./usr/cdt/lib/libsysio.a ./usr/cdt/licenses/cdt.license ./usr/cdt/cdt.imports; do
    echo "$c" | grep -qx "$f" || fail "payload missing $f"
done
echo "$c" | grep -q "libnative" && fail "base deb leaks native dev libs"
if [ -n "$dev" ]; then
    dc=$(dpkg-deb -c "$dev" | awk '{print $6}')
    for f in ./usr/cdt/lib/libnative.a ./usr/cdt/share/cdt/native-contract-src/sysiolib.cpp ./usr/cdt/scripts/gen_native_dispatch.py; do
        echo "$dc" | grep -qx "$f" || fail "dev payload missing $f"
    done
    tmp=$(mktemp -d)
    dpkg-deb -e "$dev" "$tmp/ctrl"
    grep -q "^Depends:.*cdt (= " "$tmp/ctrl/control" || fail "dev deb missing versioned base dependency"
fi
smoke=$(mktemp -d); trap 'rm -rf "$smoke" ${tmp:-}' EXIT
cat > "$smoke/hello.cpp" <<'SRC'
#include <sysio/sysio.hpp>
using namespace sysio;
CONTRACT hello : public contract {
 public:
   using contract::contract;
   ACTION hi(name user) { print("hi,", user); }
};
SRC
pkgdir=$(cd "$(dirname "$d")" && pwd)
devbase=""; [ -n "$dev" ] && devbase=$(basename "$dev")
docker run --rm -v "$pkgdir":/pkg -v "$smoke":/smoke ubuntu:24.04 bash -ec "
    export DEBIAN_FRONTEND=noninteractive
    apt-get update -qq >/dev/null
    apt-get install -y /pkg/$(basename "$d") ${devbase:+/pkg/$devbase} > /tmp/inst.log 2>&1 || { tail -15 /tmp/inst.log; exit 1; }
    test -x /usr/cdt/bin/cdt-cpp || { echo 'SMOKE FAIL: cdt-cpp not installed/executable'; exit 1; }
    # Functional smoke: the packaged toolchain must compile a contract.
    # abigen emits the .abi into the working directory, so compile from /smoke.
    cd /smoke
    /usr/cdt/bin/cdt-cpp -abigen -contract hello -o hello.wasm hello.cpp || { echo 'SMOKE FAIL: contract compile'; exit 1; }
    test -s hello.wasm || { echo 'SMOKE FAIL: hello.wasm missing/empty'; exit 1; }
    test -s hello.abi || { echo 'SMOKE FAIL: hello.abi missing/empty'; exit 1; }
" || fail "container install / contract-compile smoke"
echo "S2 PASS: $d ${dev:+(+ $dev)} (contract smoke: wasm+abi produced)"
