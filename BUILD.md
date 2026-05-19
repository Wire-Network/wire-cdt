# Build from Source Instructions

> **Supported platform:** Ubuntu 24.04 LTS on x86_64.
>
> Earlier Ubuntu versions are not supported for building Wire CDT from source.

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

Wire CDT builds with Clang 18, CMake, Ninja, and vcpkg. The CI builder uses the LLVM 18 packages from the Ubuntu 24.04 `apt.llvm.org` Noble repository; using the same toolchain locally keeps vcpkg binary-cache ABI keys aligned with CI.

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

## Bootstrap vcpkg

From the repository root:

```bash
./vcpkg/bootstrap-vcpkg.sh
```

## Recommended: Build with the GitHub Packages vcpkg Cache

The simplest way to build with the same vcpkg NuGet binary cache used by CI is
to run:

```bash
scripts/build-with-github-vcpkg-cache.sh
```

The script checks that the local environment matches the CI cache inputs before
configuring CMake. If something does not match, it prints an explicit error and
a correction command. It verifies:

- the workflow platform file and Dockerfile selected by
  `.github/workflows/build.yaml`
- x86_64 host architecture
- `cmake`, `ninja`, `mono`, `gh`, and vcpkg availability
- GitHub CLI authentication with `read:packages`
- `x64-linux-release` target and host triplets
- `.github/vcpkg-triplets` as the vcpkg overlay triplet path
- GitHub Packages NuGet source configuration

When `ccache` is installed, the script enables it as the CMake compiler launcher
and uses `.ccache` in the repository root by default. Set `CCACHE_DISABLE=1` to
turn it off.

Useful options:

```bash
scripts/build-with-github-vcpkg-cache.sh --build-dir build/release
scripts/build-with-github-vcpkg-cache.sh --jobs 8
scripts/build-with-github-vcpkg-cache.sh --skip-tests
```

The script has three build modes:

- `developer`: local developer builds; reads packages from the GitHub Packages
  NuGet cache and never publishes packages
- `trusted-ci`: trusted GitHub Actions runs; reads and writes the GitHub
  Packages NuGet cache
- `forked-pr-ci`: fork pull-request runs; uses vcpkg's default local cache so
  the workflow does not need package credentials

The default mode is `developer`. The GitHub Actions workflow uses the same
script with either `--mode trusted-ci` or `--mode forked-pr-ci`.

## Optional: Use the GitHub Packages vcpkg Binary Cache

The project can restore vcpkg-built dependencies, including LLVM, from the same NuGet-backed binary cache used by CI. This is optional, but it avoids rebuilding large vcpkg dependencies locally.

To use the cache, you need:

- a GitHub token that can read Wire-Network GitHub Packages
- `read:packages` scope on that token
- Mono, because vcpkg runs `nuget.exe` on Linux

Install Mono:

```bash
sudo apt-get install -y mono-complete
```

If you use the GitHub CLI, refresh the local token with package-read scope:

```bash
gh auth refresh -h github.com -s read:packages
gh auth status
```

`gh auth status` should list `read:packages` in the token scopes.

Configure the NuGet source:

```bash
export GITHUB_TOKEN="$(gh auth token)"
export GITHUB_USER="$(gh api user --jq .login)"
export VCPKG_NUGET_FEED="https://nuget.pkg.github.com/Wire-Network/index.json"

NUGET_EXE="$(./vcpkg/vcpkg fetch nuget | tail -n 1)"

mono "$NUGET_EXE" sources remove -Name "github" >/dev/null 2>&1 || true
mono "$NUGET_EXE" sources add \
  -Name "github" \
  -Source "$VCPKG_NUGET_FEED" \
  -UserName "$GITHUB_USER" \
  -Password "$GITHUB_TOKEN" \
  -StorePasswordInClearText

mono "$NUGET_EXE" setapikey "$GITHUB_TOKEN" -Source "$VCPKG_NUGET_FEED"
```

Enable read-only binary cache restores for the current shell:

```bash
export VCPKG_FEATURE_FLAGS="manifests,binarycaching"
export VCPKG_BINARY_SOURCES="clear;nuget,$VCPKG_NUGET_FEED,read"
```

A successful restore looks like:

```text
Restored 9 package(s) from NuGet
```

If vcpkg prints `Restored 0 package(s) from NuGet`, check:

- `gh auth status` includes `read:packages`
- Clang 18 comes from `llvm-toolchain-noble-18`, not Ubuntu's default `18.1.3` package
- `VCPKG_TARGET_TRIPLET` and `VCPKG_HOST_TRIPLET` are both `x64-linux-release`
- `VCPKG_OVERLAY_TRIPLETS` points at `.github/vcpkg-triplets`

## Configure

From the repository root:

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

## Build

```bash
cmake --build build -- -j "$(nproc)"
```

Wire CDT is a large build. If the machine runs out of memory, retry with fewer jobs:

```bash
cmake --build build -- -j 4
```

## Test

```bash
ctest --test-dir build/tests -j "$(nproc)" --output-on-failure
```

## Optional: Enable Integration Tests

Integration tests require a build of [Wire Sysio](https://github.com/Wire-Network/wire-sysio).

After building Wire Sysio, point CMake at its package config and enable integration tests:

```bash
export sysio_DIR=/path/to/wire-sysio/build/lib/cmake/sysio

cmake -B build -S . -G Ninja \
  -DCMAKE_C_COMPILER=/usr/bin/clang-18 \
  -DCMAKE_CXX_COMPILER=/usr/bin/clang++-18 \
  -DCMAKE_MAKE_PROGRAM=/usr/bin/ninja \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Release \
  -DVCPKG_TARGET_TRIPLET=x64-linux-release \
  -DVCPKG_HOST_TRIPLET=x64-linux-release \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets" \
  -Dsysio_DIR="$sysio_DIR" \
  -DENABLE_INTEGRATION_TESTS=ON
```

## Optional: Disable ccache

If ccache causes local build issues:

```bash
export CCACHE_DISABLE=1
```

## Install

After building and testing, install using one of these methods.

### Debian Package

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
