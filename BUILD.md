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

### Debian Package

Debian packages are only produced for Linux builds:

```bash
cd build/packages
./generate_package.sh deb ubuntu-24.04 amd64
sudo apt install ./cdt_*_amd64.deb
```

### CMake Install

```bash
sudo cmake --install build
```

### Use from the Build Directory

```bash
export PATH=/path/to/wire-cdt/build/bin:$PATH
```

For CMake projects, use the generated CDT Wasm toolchain file:

```bash
-DCMAKE_TOOLCHAIN_FILE=/path/to/wire-cdt/build/lib/cmake/CDTWasmToolchain.cmake
```

## Installed Tools

Wire CDT installs its command-line tools under the CDT install prefix, normally
`/usr/local/cdt/bin`.

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

If installed from a Debian package:

```bash
sudo apt remove cdt
```

If installed with CMake:

```bash
sudo rm -fr /usr/local/cdt
sudo rm -fr /usr/local/lib/cmake/cdt
sudo rm -f /usr/local/bin/sysio-*
sudo rm -f /usr/local/bin/cdt-*
```
