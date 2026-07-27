#!/bin/sh
# S3 gate: RPM payload + containerized install + contract-compile smoke.
# Usage: verify-rpm.sh <base-rpm> [dev-rpm]
set -e
r="$1"; dev="$2"
[ -f "$r" ] || { echo "S3 FAIL: rpm not found: $r"; exit 1; }
fail() { echo "S3 FAIL: $1"; exit 1; }
l=$(rpm -qlp "$r" 2>/dev/null)
# cdt-cc is the C driver -- CDTWasmToolchain.cmake names it for both
# CMAKE_C_COMPILER and CMAKE_ASM_COMPILER, so its absence breaks every contract
# build that compiles a .c or .s.
# DISTRO-TOOLCHAIN layout (cf. /usr/lib/llvm-18): the whole self-contained
# toolchain homes at /usr/lib/cdt, /usr/bin gets symlinks for the public entry
# points only, the discoverable cmake config sits at /usr/lib/cmake/cdt, and the
# licenses at /usr/share/licenses/wire-cdt. There is no /usr/cdt subtree.
for f in /usr/lib/cdt/bin/cdt-cc /usr/lib/cdt/bin/cdt-cpp /usr/lib/cdt/bin/cdt-protoc /usr/lib/cdt/bin/cdt-protoc-gen-zpp /usr/lib/cdt/lib/cmake/cdt/cdt-config.cmake /usr/lib/cdt/lib/cmake/cdt/CDTWasmToolchain.cmake /usr/lib/cdt/lib/libsysio.a /usr/lib/cdt/cdt.imports /usr/lib/cmake/cdt/cdt-config.cmake /usr/share/licenses/wire-cdt/cdt.license; do
    echo "$l" | grep -qx "$f" || fail "payload missing $f"
done
# Public entry points reach /usr/bin as symlinks into the private home.
for f in cdt-cc cdt-cpp cdt-protoc cdt-protoc-gen-zpp cdt-ld cdt-abidiff cdt-init cdt-codegen; do
    echo "$l" | grep -qx "/usr/bin/$f" || fail "entry-point symlink missing /usr/bin/$f"
done
# NEGATIVE GATE -- the collision class must be structurally impossible. These
# names belong to the distro's clang / llvm / lld packages; shipping them in
# /usr/bin either fails the rpm transaction on a file conflict or hijacks the
# system toolchain. They live at /usr/lib/cdt/bin and must stay there.
for f in clang clang++ opt llc lld ld.lld wasm-ld; do
    echo "$l" | grep -qx "/usr/bin/$f" && fail "BANNED: /usr/bin/$f collides with the distro toolchain"
done
echo "$l" | grep -qE '^/usr/bin/llvm-' && fail "BANNED: bare llvm-* binaries in /usr/bin"
# Nothing may land in a /usr/cdt subtree.
echo "$l" | grep -q '^/usr/cdt/' && fail "rpm installs into a /usr/cdt subtree"
# The cdt-* aliases installed as SYMLINKS onto their sysio-* counterparts
# (cmake/InstallCDT.cmake). `rpm -qlp` lists a dangling symlink exactly like a
# live one, so presence is checked here and RESOLUTION is checked in the
# container smoke below (`test -x` follows the link).
cdt_symlinks="cdt-pp cdt-wast2wasm cdt-wasm2wast"
for f in $cdt_symlinks; do
    echo "$l" | grep -qx "/usr/lib/cdt/bin/$f" || fail "payload missing /usr/lib/cdt/bin/$f"
    echo "$l" | grep -qx "/usr/bin/$f" || fail "entry-point symlink missing /usr/bin/$f"
done
echo "$l" | grep -q "libnative" && fail "base rpm leaks native dev libs"
v=$(rpm -qp --qf '%{VERSION}' "$r" 2>/dev/null)
case "$v" in *-*) fail "version tag contains hyphen: $v" ;; esac
if [ -n "$dev" ]; then
    dl=$(rpm -qlp "$dev" 2>/dev/null)
    echo "$dl" | grep -qx "/usr/lib/cdt/lib/libnative.a" || fail "dev payload missing libnative.a"
    rpm -qp --requires "$dev" 2>/dev/null | grep -q "wire-cdt = " || fail "dev rpm missing versioned base requirement"
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
# find_package(cdt) with ZERO setup -- no CMAKE_PREFIX_PATH, no cdt_DIR. The
# discoverable copy at /usr/lib/cmake/cdt is what makes that work (/usr/lib/cdt
# is NOT on CMake's default search path); it bakes CDT_ROOT=/usr/lib/cdt, so
# every ${CDT_ROOT}/... path resolves into the self-contained home -- the same
# form wire-sysio's build-sysio.sh uses.
mkdir -p "$smoke/fp"
cat > "$smoke/fp/CMakeLists.txt" <<'FP'
cmake_minimum_required(VERSION 3.19)
project(cdt_find_package_smoke NONE)
find_package(cdt REQUIRED)
if(NOT CDT_ROOT STREQUAL "/usr/lib/cdt")
   message(FATAL_ERROR "CDT_ROOT resolved to '${CDT_ROOT}', expected '/usr/lib/cdt'")
endif()
foreach(f bin/cdt-cpp lib/cmake/cdt/cdt-config.cmake lib/cmake/cdt/CDTWasmToolchain.cmake cdt.imports)
   if(NOT EXISTS "${CDT_ROOT}/${f}")
      message(FATAL_ERROR "missing ${CDT_ROOT}/${f} under the resolved CDT_ROOT")
   endif()
endforeach()
FP
pkgdir=$(cd "$(dirname "$r")" && pwd)
devbase=""; [ -n "$dev" ] && devbase=$(basename "$dev")
docker run --rm -v "$pkgdir":/pkg -v "$smoke":/smoke fedora:latest bash -ec "
    dnf install -y cmake > /tmp/cmake.log 2>&1 || { tail -15 /tmp/cmake.log; exit 1; }
    dnf install -y /pkg/$(basename "$r") ${devbase:+/pkg/$devbase} > /tmp/inst.log 2>&1 || { tail -15 /tmp/inst.log; exit 1; }
    # test -x FOLLOWS symlinks, so this resolves BOTH hops: /usr/bin/<tool> ->
    # ../lib/cdt/bin/<tool>, and (for the aliases) that link's own sysio-*
    # target. A break anywhere in the chain fails here.
    for tool in cdt-cc cdt-cpp cdt-protoc $cdt_symlinks; do
        test -L /usr/bin/\$tool || { echo \"SMOKE FAIL: /usr/bin/\$tool is not a symlink\"; exit 1; }
        test -x /usr/bin/\$tool || { echo \"SMOKE FAIL: \$tool not installed/executable (dangling symlink?)\"; exit 1; }
        case \"\$(readlink /usr/bin/\$tool)\" in
            ../lib/cdt/bin/*) ;;
            *) echo \"SMOKE FAIL: /usr/bin/\$tool -> \$(readlink /usr/bin/\$tool), expected ../lib/cdt/bin/*\"; exit 1 ;;
        esac
    done
    test -d /usr/cdt && { echo 'SMOKE FAIL: /usr/cdt exists after install'; exit 1; }
    # The bundled toolchain binaries must stay in the PRIVATE home, never in
    # /usr/bin where the distro's own clang / llvm / lld packages own the names.
    for banned in clang clang++ opt llc lld ld.lld wasm-ld; do
        test -e /usr/bin/\$banned && { echo \"SMOKE FAIL: /usr/bin/\$banned shipped by wire-cdt\"; exit 1; }
    done
    ls /usr/bin/llvm-* >/dev/null 2>&1 && { echo 'SMOKE FAIL: bare llvm-* in /usr/bin'; exit 1; }
    # The packaged toolchain must be reachable through plain find_package(cdt)
    # with no prefix hints at all.
    cmake -S /smoke/fp -B /tmp/fp > /tmp/fp.log 2>&1 || { echo 'SMOKE FAIL: find_package(cdt)'; tail -20 /tmp/fp.log; exit 1; }
    # Functional smoke THROUGH THE /usr/bin SYMLINK. /usr/lib/cdt/bin is
    # deliberately NOT on PATH, so a bare \`cdt-cpp\` can only be the symlink.
    # This is the load-bearing check for whereami's realpath(/proc/self/exe)
    # resolution: the compiler must find ../cdt.imports and its sibling llvm
    # tools relative to its REAL home (/usr/lib/cdt/bin), not to /usr/bin.
    # NB merged-/usr: /usr/sbin and /bin are symlinks to /usr/bin and DO come
    # first on Fedora's PATH, so compare RESOLVED paths rather than the literal hit.
    onpath=\$(command -v cdt-cpp) || { echo 'SMOKE FAIL: cdt-cpp not on PATH'; exit 1; }
    case \"\$onpath\" in
        /usr/lib/cdt/bin/*) echo \"SMOKE FAIL: \$onpath is the private home, not an entry-point symlink\"; exit 1 ;;
    esac
    [ \"\$(readlink -f \"\$onpath\")\" = /usr/lib/cdt/bin/cdt-cpp ] \\
        || { echo \"SMOKE FAIL: \$onpath resolves to \$(readlink -f \"\$onpath\"), expected /usr/lib/cdt/bin/cdt-cpp\"; exit 1; }
    # abigen emits the .abi into the working directory, so compile from /smoke.
    cd /smoke
    cdt-cpp -abigen -contract hello -o hello.wasm hello.cpp || { echo 'SMOKE FAIL: contract compile'; exit 1; }
    test -s hello.wasm || { echo 'SMOKE FAIL: hello.wasm missing/empty'; exit 1; }
    test -s hello.abi || { echo 'SMOKE FAIL: hello.abi missing/empty'; exit 1; }
" || fail "container install / contract-compile smoke"
echo "S3 PASS: $r ${dev:+(+ $dev)} (contract smoke: wasm+abi produced)"
