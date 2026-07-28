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
# BINUTILS ALIASES -- cdt-ar, cdt-ranlib and friends, symlinks onto their llvm-*
# counterparts. CDTWasmToolchain.cmake bakes CMAKE_AR=${CDT_ROOT}/bin/cdt-ar and
# CMAKE_RANLIB=${CDT_ROOT}/bin/cdt-ranlib, so without them the tarball ships a
# toolchain naming binaries it does not contain and every static-library build
# through it fails at the archive step. RESOLUTION is checked against the
# extracted copy below.
cdt_binutils="bin/cdt-ar bin/cdt-ranlib bin/cdt-nm bin/cdt-objcopy bin/cdt-objdump bin/cdt-readobj bin/cdt-readelf bin/cdt-strip"
for f in $cdt_binutils; do
    echo "$list" | grep -qx "wire-cdt/$f" || fail "missing wire-cdt/$f (CMAKE_AR/CMAKE_RANLIB target)"
done
# THE TARBALL CARRIES base AND dev. CDTMacros.cmake ships in base and its
# native-test macros reference ${CDT_ROOT}/scripts/gen_native_dispatch.py and
# ${CDT_ROOT}/share/cdt/native-contract-src -- both COMPONENT dev. A base-only
# tarball advertised native contract testing while omitting everything it needs,
# and since the tarball is the ONLY macOS artifact that made native contract
# testing unreachable on macOS entirely. So these are now REQUIRED payload, the
# exact inverse of the old "portable tarball leaks native dev libs" assertion.
echo "$list" | grep -q "libnative" || fail "tarball missing the native (dev) testing libs"
echo "$list" | grep -q "share/cdt/native-contract-src/" || fail "tarball missing share/cdt/native-contract-src/"
echo "$list" | grep -qx "wire-cdt/scripts/gen_native_dispatch.py" || fail "tarball missing scripts/gen_native_dispatch.py"

# Extract and resolve: `[ -e ]` follows the link, so a symlink whose target was
# never packaged (or was packaged under a different name) fails here.
x=$(mktemp -d)
trap 'rm -rf "$x"' EXIT
tar xzf "$t" -C "$x"
for f in $cdt_symlinks; do
    [ -L "$x/wire-cdt/$f" ] || fail "wire-cdt/$f is not a symlink"
    [ -e "$x/wire-cdt/$f" ] || fail "dangling symlink wire-cdt/$f -> $(readlink "$x/wire-cdt/$f")"
done
for f in $cdt_binutils; do
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
# THE /usr-BAKED-CONFIG GUARD. The pattern deliberately leaves the string
# UNTERMINATED after /usr so it matches every system-package root, not just a
# bare /usr: the deb/rpm variant bakes IS_DIRECTORY "/usr/lib/cdt", so the old
# fully-quoted 'IS_DIRECTORY "/usr"' could never match and this gate was dead --
# it "passed" on the tarball for the wrong reason and would have passed on the
# /usr-baked file it exists to catch.
#
# Kept as a FUNCTION so the self-test below exercises the exact expression the
# gate uses; a self-test against a re-typed copy of the pattern would prove
# nothing about the gate itself.
usr_baked_config() { grep -q 'IS_DIRECTORY "/usr' "$1"; }
# SELF-TEST -- the guard must FIRE on a system-variant file and stay SILENT on a
# portable one. Without this, a future edit could re-break the pattern and the
# gate would once again pass by never matching anything.
st=$(mktemp -d); trap 'rm -rf "$x" "$st"' EXIT
printf 'if(IS_DIRECTORY "/usr/lib/cdt")\n' > "$st/system.cmake"
printf 'if(IS_DIRECTORY "/opt/wire-cdt")\n' > "$st/portable.cmake"
usr_baked_config "$st/system.cmake" \
    || fail "self-test: the /usr-baked guard does NOT fire on a /usr/lib/cdt-baked config (the gate is dead)"
usr_baked_config "$st/portable.cmake" \
    && fail "self-test: the /usr-baked guard fires on a portable /opt/wire-cdt config (false positive)"
usr_baked_config "$cfg" \
    && fail "tarball ships the /usr-baked cdt-config.cmake"
# The relative-discovery fallback must stay intact so an extraction ANYWHERE
# other than /opt still resolves CDT_ROOT from the config file's own location.
grep -q 'PREFIX_CANDIDATES' "$cfg" \
    || fail "cdt-config.cmake lost its relative-discovery fallback"

echo "S1 PASS: $t"
