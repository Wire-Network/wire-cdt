# Build from Source Instructions

> **Supported platforms:**
>
> - Ubuntu 24.04 LTS on x86_64
> - macOS on Apple Silicon
>
> Earlier Ubuntu versions are not supported for building Wire CDT from source.
> Intel macOS is not supported.

## Get the Source Code

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/Wire-Network/wire-cdt.git
cd wire-cdt
```

If you already cloned without submodules, initialize them before configuring:

```bash
git submodule update --init --recursive
```

## Install Dependencies

Wire CDT builds with Clang, CMake, Ninja, and vcpkg. The Linux CI builder uses the LLVM 18 packages from the Ubuntu 24.04 `apt.llvm.org` Noble repository; using the same Linux toolchain locally keeps vcpkg binary-cache ABI keys aligned with CI. The macOS CI builder uses the Xcode Command Line Tools on Apple Silicon.

### Ubuntu 24.04 x86_64

```bash
sudo apt-get update
sudo apt-get install -y \
  build-essential \
  ccache \
  cmake \
  curl \
  git \
  gnupg \
  jq \
  libcurl4-openssl-dev \
  libedit-dev \
  libffi-dev \
  libgmp-dev \
  libncurses-dev \
  libssl-dev \
  libtinfo-dev \
  libxml2-dev \
  libzstd-dev \
  ninja-build \
  python3 \
  python3-dev \
  python3-numpy \
  python3-pip \
  software-properties-common \
  unzip \
  wget \
  zip \
  zlib1g-dev \
  zstd
```

Add the LLVM 18 Noble package source and install Clang 18:

```bash
sudo wget -qO /etc/apt/trusted.gpg.d/apt.llvm.org.asc https://apt.llvm.org/llvm-snapshot.gpg.key
echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-18 main" | \
  sudo tee /etc/apt/sources.list.d/llvm-toolchain-noble-18.list

sudo apt-get update
sudo apt-get install -y \
  clang-18 \
  clang-tools-18 \
  lld-18 \
  libclang-18-dev \
  libclang-common-18-dev \
  libclang-cpp18 \
  libclang-rt-18-dev \
  libclang1-18 \
  libllvm18 \
  llvm-18 \
  llvm-18-dev \
  llvm-18-linker-tools \
  llvm-18-runtime \
  llvm-18-tools
```

Optional Python helper:

```bash
python3 -m pip install pygments
```

### macOS Apple Silicon

Install Xcode Command Line Tools and Homebrew dependencies:

```bash
xcode-select --install

brew install \
  ccache \
  cmake \
  git \
  ninja \
  python
```

Wire CDT macOS host binaries are built for Apple Silicon and dynamically link
against the system libc++ provided by the installed Xcode Command Line Tools.
Use macOS 11.0 or newer with current Command Line Tools when building or running
the macOS CDT tools.

## Bootstrap vcpkg

From the repository root:

```bash
./vcpkg/bootstrap-vcpkg.sh
```

## Configure

From the repository root:

### Ubuntu 24.04 x86_64

```bash
export CC=/usr/bin/clang-18
export CXX=/usr/bin/clang++-18
export CMAKE_MAKE_PROGRAM=/usr/bin/ninja
export VCPKG_TARGET_TRIPLET=x64-linux-release
export VCPKG_HOST_TRIPLET=x64-linux-release
export VCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets"

cmake -B build -S . -G Ninja \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
  -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
  -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_OVERLAY_TRIPLETS"
```

### macOS Apple Silicon

```bash
export CC="$(xcrun --find clang)"
export CXX="$(xcrun --find clang++)"
export CMAKE_MAKE_PROGRAM="$(command -v ninja)"
export VCPKG_TARGET_TRIPLET=arm64-osx-release
export VCPKG_HOST_TRIPLET=arm64-osx-release
export VCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets"

cmake -B build -S . -G Ninja \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
  -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
  -DVCPKG_OVERLAY_TRIPLETS="$VCPKG_OVERLAY_TRIPLETS"
```

## Build

```bash
cmake --build build
```

Wire CDT is a large build. If the machine runs out of memory, retry with fewer jobs:

```bash
cmake --build build -- -j 4
```

## Test

```bash
JOBS="$(sysctl -n hw.ncpu 2>/dev/null || getconf _NPROCESSORS_ONLN)"
ctest --test-dir build -j "$JOBS" --output-on-failure
```

## Optional: Enable Integration Tests

Integration tests require a build of [Wire Sysio](https://github.com/Wire-Network/wire-sysio).

After building Wire Sysio, point CMake at its package config and enable integration tests:

```bash
export sysio_DIR=/path/to/wire-sysio/build/lib/cmake/sysio
export CC=/usr/bin/clang-18
export CXX=/usr/bin/clang++-18
export CMAKE_MAKE_PROGRAM=/usr/bin/ninja
export VCPKG_TARGET_TRIPLET=x64-linux-release
export VCPKG_HOST_TRIPLET=x64-linux-release

cmake -B build -S . -G Ninja \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_MAKE_PROGRAM="$CMAKE_MAKE_PROGRAM" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
  -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets" \
  -Dsysio_DIR="$sysio_DIR" \
  -DENABLE_INTEGRATION_TESTS=ON
```

On macOS, use the macOS compiler and `arm64-osx-release` triplets from the
manual macOS configure example above. Integration tests also require a matching
Wire Sysio build.

## Optional: Disable ccache

If ccache causes local build issues:

```bash
export CCACHE_DISABLE=1
```

## Install

After building and testing, install using one of these methods.

### System Packages

Packaging is driven by CPack. Debian and RPM packages are only produced for
Linux builds; the portable tarball builds on Linux and macOS:

```bash
cd build
cpack -G DEB                      # wire-cdt_<version>_amd64.deb + wire-cdt-dev_…
cpack -G RPM                      # wire-cdt-<version>-x86_64.rpm + wire-cdt-dev-…
cpack -G TGZ                      # wire-cdt-<version>-<arch>.tar.gz
cmake --build . --target package-tgz   # same tarball, convenience alias

sudo apt install ./wire-cdt_*_amd64.deb
```

The deb and the rpm use the **distro-toolchain layout** — the same shape
Debian and Fedora use for a bundled compiler (`/usr/lib/llvm-18/…`):

| Path | Contents |
|---|---|
| `/usr/lib/cdt/` | the **entire self-contained toolchain**: `bin/` (all real binaries, including the bundled `clang`, `clang++`, `lld`, `ld.lld`, `opt`, `llc`, `wasm-ld`, `llvm-*`), `lib/`, `include/`, `lib/cmake/cdt/`, `scripts/`, `share/cdt/native-contract-src/`, `cdt.imports` |
| `/usr/bin/<tool>` | symlinks → `../lib/cdt/bin/<tool>`, **public entry points only** — `cdt-cc`, `cdt-cpp`, `cdt-ld`, `cdt-abidiff`, `cdt-init`, `cdt-codegen`, `cdt-protoc`, `cdt-protoc-gen-zpp`, `cdt-pp`, `cdt-wast2wasm`, `cdt-wasm2wast`, `sysio-pp`, `sysio-wast2wasm`, `sysio-wasm2wast` |
| `/usr/lib/cmake/cdt/cdt-config.cmake` | the CMake-default-searched copy → zero-setup `find_package(cdt)` |
| `/usr/share/licenses/wire-cdt/` | license texts |

There is no `/usr/cdt` subtree. The relative layout **inside** `/usr/lib/cdt` is
byte-for-byte the same one the tarball has under `wire-cdt/`.

`bin/` also carries the **binutils aliases** — `cdt-ar`, `cdt-ranlib`, `cdt-nm`,
`cdt-objcopy`, `cdt-objdump`, `cdt-readobj`, `cdt-readelf`, `cdt-strip`, each a
symlink onto its `llvm-*` neighbour. `CDTWasmToolchain.cmake` bakes
`CMAKE_AR`/`CMAKE_RANLIB` to `<root>/bin/cdt-ar` and `<root>/bin/cdt-ranlib`, so
they are required for any `add_library(… STATIC …)` built through the packaged
toolchain. They are deliberately **not** public entry points: nothing invokes
them by name off `PATH`, only through that absolute path, so they stay private
to the home like the `llvm-*` binaries they point at.

**Why the private home:** the toolchain bundles its own LLVM. Installing those
binaries into `/usr/bin` would put `/usr/bin/clang`, `/usr/bin/lld`,
`/usr/bin/llvm-ar` … in direct conflict with the distro's own `clang`, `lld` and
`llvm` packages — either failing the `dpkg`/`rpm` transaction on a file conflict
or silently hijacking the system compiler. Keeping them under `/usr/lib/cdt/bin`
and exposing only the `cdt-*` / `sysio-*` entry points makes that impossible;
`verify-deb.sh` and `verify-rpm.sh` both gate on it explicitly.

Two consequences worth knowing:

- The entry points land on the default `PATH` — `cdt-cpp --version` works with no
  environment setup after `apt install` / `dnf install`. Invoking through the
  symlink is fully supported: the compiler locates `cdt.imports` and its sibling
  LLVM tools by resolving its **own real path**
  (`realpath("/proc/self/exe")` → `/usr/lib/cdt/bin`), not the symlink's.
- `find_package(cdt)` resolves with **no configuration at all** — no
  `CMAKE_PREFIX_PATH`, no `cdt_DIR` — because of the discoverable copy at
  `/usr/lib/cmake/cdt/`. Both copies bake `CDT_ROOT=/usr/lib/cdt`, so every
  `${CDT_ROOT}/…` path lands inside the self-contained home.

```cmake
find_package(cdt REQUIRED)   # nothing else needed with the deb/rpm installed
```

### Portable Tarball

`wire-cdt-<version>-<arch>.tar.gz` (`<arch>` is `x86_64` on Linux,
`macos-arm64` on Apple Silicon) is a **relocatable** toolchain tree rooted at
`wire-cdt/`. Its **default install location is `/opt`** — extracted there the
root directory lands on exactly `/opt/wire-cdt`, which is the path its packaged
CMake files bake. No root beyond write access to `/opt`, no package manager, and
it coexists with a deb/rpm install (which lives at `/usr`, a different prefix):

```bash
tar xzf wire-cdt-<version>-x86_64.tar.gz -C /opt      # -> /opt/wire-cdt/
export PATH=/opt/wire-cdt/bin:$PATH
cdt-cpp --version
```

The tarball carries **both** packaged components, `base` **and** `dev` — so it
includes the native (host) contract-testing payload: `lib/libnative*.a`,
`share/cdt/native-contract-src/` and `scripts/gen_native_dispatch.py`. That is
required rather than optional, because `CDTMacros.cmake` ships in `base` and its
native-test macros reference the first two by `${CDT_ROOT}` path; a base-only
tarball would advertise native contract testing while omitting everything it
needs — and since the tarball is the only macOS artifact, that made native
contract testing unreachable on macOS entirely.

For CMake projects, point `find_package(cdt)` at the extracted tree:

```bash
cmake -B build -S . -DCMAKE_PREFIX_PATH=/opt/wire-cdt
# or, equivalently:
cmake -B build -S . -Dcdt_DIR=/opt/wire-cdt/lib/cmake/cdt
```

The toolchain file works straight out of the tarball too, because it bakes the
same `/opt/wire-cdt` root:

```bash
cmake -B build -S . \
  -DCMAKE_TOOLCHAIN_FILE=/opt/wire-cdt/lib/cmake/cdt/CDTWasmToolchain.cmake
```

**Extracting somewhere other than `/opt`** still works through
`find_package(cdt)`: the tarball's `lib/cmake/cdt/cdt-config.cmake` falls back to
discovering `CDT_ROOT` **relative to its own location** whenever the baked
`/opt/wire-cdt` is not a directory, so it resolves to wherever you extracted it.
`cdt-config.cmake` then puts that tree on `CMAKE_MODULE_PATH` and pulls in
`CDTMacros`, which resolves every path through the discovered `CDT_ROOT`.
`CDTWasmToolchain.cmake` has no discovery of its own — every compiler path in it
is absolute — so a non-`/opt` extraction must go through `find_package(cdt)`
rather than the toolchain file.

The deb/rpm copies of both files bake `/usr/lib/cdt` instead. **Both packaged
roots are applied at package time only**, by a CPack pre-build hook per
generator — `cmake/cpack-system-layout.cmake` for the deb/rpm, and
`cmake/cpack-tgz-toolchain-root.cmake` for the tarball. A plain
`cmake --install` never sees either of them; see below.

### CMake Install

```bash
sudo cmake --install build
```

This is **not** packaging, and it has no distribution-named or `cdt/`
sub-directory layer: the install rules use relative destinations, so everything
lands directly under `CMAKE_INSTALL_PREFIX` — `<prefix>/bin`, `<prefix>/lib`,
`<prefix>/include`, `<prefix>/lib/cmake/cdt`, `<prefix>/cdt.imports`. With
CMake's default prefix that means `/usr/local/bin`, `/usr/local/lib`, … Pass
`-DCMAKE_INSTALL_PREFIX=<dir>` to choose another root.

`lib/cmake/cdt/` is configured **at install time against the prefix actually in
effect**, so `cdt-config.cmake` and `CDTWasmToolchain.cmake` name that prefix and
nothing else — neither packaged root leaks into a plain install:

```bash
cmake --install build --prefix /tmp/cdt-prefix
grep CMAKE_CXX_COMPILER /tmp/cdt-prefix/lib/cmake/cdt/CDTWasmToolchain.cmake
# set(CMAKE_CXX_COMPILER "/tmp/cdt-prefix/bin/cdt-cpp")
```

This works for `--prefix` too, not just the configure-time
`-DCMAKE_INSTALL_PREFIX`, because the prefix is only known once `cmake --install`
runs. `DESTDIR` relocates the output path without changing the baked root, which
stays the logical prefix the files will be read from.

### Use from the Build Directory

The build tree carries the same `lib/cmake/cdt/` subtree an install does, so another project can
consume it in place — no install step at all:

```bash
cmake -DCDT_ROOT=/path/to/wire-cdt/build ...
```

That is what wire-sysio expects: `cmake/contract-tools.cmake` resolves
`$CDT_ROOT/lib/cmake/cdt/cdt-config.cmake` and passes
`$CDT_ROOT/lib/cmake/cdt/CDTWasmToolchain.cmake` as the toolchain file, so building contracts
against an uninstalled CDT is a matter of pointing `CDT_ROOT` at the build directory.

For a project that takes the toolchain file directly:

```bash
-DCMAKE_TOOLCHAIN_FILE=/path/to/wire-cdt/build/lib/cmake/cdt/CDTWasmToolchain.cmake
```

Putting `build/bin` on `PATH` also works for invoking the drivers, but note it holds the bundled
unprefixed `clang`, `clang++`, `lld`, `wasm-ld`, `opt`, `llc` and `llvm-*` — unlike the packaged
layout, which keeps them private — so it will shadow the host toolchain:

```bash
export PATH=/path/to/wire-cdt/build/bin:$PATH
```

## Installed Tools

Where the command-line tools land, per install method:

| Install method | Real binaries | On `PATH` |
|---|---|---|
| deb / rpm | `/usr/lib/cdt/bin` | `/usr/bin` symlinks, public entry points only |
| portable tarball (default `/opt`) | `/opt/wire-cdt/bin` | add it to `PATH` yourself |
| `cmake --install` (default prefix) | `/usr/local/bin` | already on `PATH` |

Primary CDT tools:

- `cdt-abidiff`
- `cdt-cc`
- `cdt-codegen`
- `cdt-cpp`
- `cdt-init`
- `cdt-ld`
- `cdt-protoc`
- `cdt-protoc-gen-zpp`
- `sysio-pp`
- `sysio-wasm2wast`
- `sysio-wast2wasm`

The install also includes the host LLVM tools that the CDT drivers invoke at
runtime:

- `clang`
- `clang++`
- `ld.lld`
- `llc`
- `lld`
- `llvm-ar`
- `llvm-nm`
- `llvm-objcopy`
- `llvm-objdump`
- `llvm-ranlib`
- `llvm-readelf`
- `llvm-readobj`
- `llvm-strip`
- `opt`
- `wasm-ld`

These LLVM tools are copied into the install tree as regular executables, not
as symlinks to the build directory. Keeping them next to the CDT tools makes the
installed CDT toolchain self-contained and prevents `cdt-cc`, `cdt-cpp`, and
`cdt-ld` from accidentally using a different system LLVM version.

The install also places the `sysio_attrs.so` and `sysio_codegen.so` plugins in
the same `bin` directory.

## Uninstall

If installed from a Debian or RPM package:

```bash
sudo apt remove wire-cdt wire-cdt-dev      # deb
sudo dnf remove wire-cdt wire-cdt-dev      # rpm
```

If installed from the portable tarball, remove the extracted tree:

```bash
sudo rm -fr /opt/wire-cdt
```

If installed with CMake (default prefix shown):

```bash
sudo rm -fr /usr/local/lib/cmake/cdt
sudo rm -f  /usr/local/cdt.imports
sudo rm -f  /usr/local/bin/sysio-*
sudo rm -f  /usr/local/bin/cdt-*
```
