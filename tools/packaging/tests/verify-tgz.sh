#!/bin/sh
# S1 gate: portable toolchain tarball layout.
# Usage: verify-tgz.sh [--no-service] <tarball>
#
# --no-service additionally asserts the archive carries NO Linux service
# payload (systemd unit / tmpfiles.d fragment / logrotate policy). It is the
# mode used for the macOS tarball, where such files would be dead weight.
set -e
no_service=0
if [ "$1" = "--no-service" ]; then no_service=1; shift; fi
t="$1"
[ -f "$t" ] || { echo "S1 FAIL: tarball not found: $t"; exit 1; }
fail() { echo "S1 FAIL: $1"; exit 1; }
list=$(tar tzf "$t")
# The archive's top-level directory is the DISTRIBUTION name (wire-cdt/); the
# paths INSIDE it keep the toolchain's own `cdt` naming (bin/cdt-cpp, …).
bad=$(echo "$list" | grep -v '^wire-cdt/' || true)
[ -z "$bad" ] || fail "entries outside wire-cdt/: $bad"
# cdt-cc is the C driver -- as load-bearing as cdt-cpp (CDTWasmToolchain.cmake
# names it for both CMAKE_C_COMPILER and CMAKE_ASM_COMPILER), so its absence
# breaks every contract build that compiles a .c or .s.
for f in bin/cdt-cc bin/cdt-cpp bin/cdt-protoc bin/cdt-protoc-gen-zpp lib/cmake/cdt/cdt-config.cmake lib/cmake/cdt/CDTWasmToolchain.cmake lib/libsysio.a licenses/cdt.license cdt.imports; do
    echo "$list" | grep -qx "wire-cdt/$f" || fail "missing wire-cdt/$f"
done
# The cdt-* aliases installed as SYMLINKS onto their sysio-* counterparts
# (cmake/InstallCDT.cmake). A tar listing shows a dangling symlink exactly like
# a live one, so presence is checked here and RESOLUTION is checked below
# against an extracted copy.
cdt_symlinks="bin/cdt-pp bin/cdt-wast2wasm bin/cdt-wasm2wast"
for f in $cdt_symlinks; do
    echo "$list" | grep -qx "wire-cdt/$f" || fail "missing wire-cdt/$f"
done
if [ "$no_service" = "1" ]; then
    svc=$(echo "$list" | grep -E '(^|/)(lib/systemd|lib/tmpfiles\.d|etc/logrotate\.d)/' || true)
    [ -z "$svc" ] || fail "tarball carries service payload: $svc"
fi
n=$(echo "$list" | grep -c "libnative" || true)
[ "$n" = "0" ] || fail "portable tarball leaks native dev libs"

# Extract and resolve: `[ -e ]` follows the link, so a symlink whose target was
# never packaged (or was packaged under a different name) fails here.
x=$(mktemp -d)
trap 'rm -rf "$x"' EXIT
tar xzf "$t" -C "$x"
for f in $cdt_symlinks; do
    [ -L "$x/wire-cdt/$f" ] || fail "wire-cdt/$f is not a symlink"
    [ -e "$x/wire-cdt/$f" ] || fail "dangling symlink wire-cdt/$f -> $(readlink "$x/wire-cdt/$f")"
done
for f in bin/cdt-cc bin/cdt-cpp; do
    [ -x "$x/wire-cdt/$f" ] || fail "wire-cdt/$f is not executable"
done
# The tarball's DEFAULT install location is /opt/wire-cdt (`tar xzf … -C /opt`
# lands the `wire-cdt/` root there), so its cmake files must bake THAT root --
# not the /usr the deb and the rpm install to, and never a /usr/cdt subtree
# (see cmake/cpack-tgz-toolchain-root.cmake, which performs the swap).
cfg="$x/wire-cdt/lib/cmake/cdt/cdt-config.cmake"
tc="$x/wire-cdt/lib/cmake/cdt/CDTWasmToolchain.cmake"
grep -q 'IS_DIRECTORY "/opt/wire-cdt"' "$cfg" \
    || fail "cdt-config.cmake does not bake CDT_ROOT_DIR=/opt/wire-cdt"
grep -q '"/opt/wire-cdt/bin/cdt-cpp"' "$tc" \
    || fail "CDTWasmToolchain.cmake does not bake /opt/wire-cdt compiler paths"
# No packaged artifact may reference /usr/cdt, and the tarball must not ship the
# deb/rpm (/usr-baked) config either.
grep -q '/usr/cdt' "$cfg" "$tc" && fail "tarball cmake files reference /usr/cdt"
grep -q 'IS_DIRECTORY "/usr"' "$cfg" \
    && fail "tarball ships the /usr-baked cdt-config.cmake"
# The relative-discovery fallback must stay intact so an extraction ANYWHERE
# other than /opt still resolves CDT_ROOT from the config file's own location.
grep -q 'PREFIX_CANDIDATES' "$cfg" \
    || fail "cdt-config.cmake lost its relative-discovery fallback"

echo "S1 PASS: $t"
