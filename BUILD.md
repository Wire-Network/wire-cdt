# Build from Source Instructions

> **Supported Platforms:** As of this release, Wire CDT can be built on **Ubuntu 24.04 LTS on x86_64. Earlier Ubuntu versions (20.04, 22.04) are no longer supported for building from source.

# Get the source code

Clone this repo as follows:

```bash
# Clone with HTTP
git clone --recursive https://github.com/Wire-Network/wire-cdt.git

# Clone with SSH (if you have SSH keys set up)
# git clone --recursive git@github.com:Wire-Network/wire-cdt.git

# Enter the repo directory
cd wire-cdt
```

# On Host Machine

> **ONLY SUPPORTS:** Ubuntu 24.04 LTS.

## Prerequisites

You will need to build on a [supported operating system](README.md#supported-operating-systems) with the following tools and libraries available:

- **Clang** – C/C++ compiler (system clang is sufficient for CDT)
- **CMake 3.16+** – Build system generator
- **libxml2** – XML parsing library
- **Python 3** – Python is needed for some build steps and tools
- **OCaml** – Required for certain CDT components
- **git** – Version control, used for fetching submodules

Other standard development tools and libraries (build essentials, etc.) are needed as well. These will be installed in the steps below.

## Step 1 - Install Dependencies

### 1a. Install System Packages (Ubuntu 24.04)

On Ubuntu 24.04, install the following dependencies.

```bash
# Update package lists
sudo apt-get update

# Install build tools and libraries
sudo apt-get install -y \
        build-essential \
        clang \
        clang-tidy \
        cmake \
        git \
        libxml2-dev \
        opam \
        ocaml-interp \
        python3 \
        python3-pip \
        time
```

### 1b. Install Python Dependencies

Install required Python packages:

```bash
python3 -m pip install pygments
```

### 1c. Install LLVM 11

Wire CDT depends on **LLVM 11**. On modern systems (Ubuntu 24.04+), you will need to build LLVM 11 from source, since it's not available in the apt repositories.

Use the helper script from Wire Sysio to build and install LLVM 11:

```bash
# First, you'll need the Wire Sysio repository for the LLVM 11 build script
# Clone it temporarily if you don't have it:
git clone https://github.com/Wire-Network/wire-sysio.git /tmp/wire-sysio

# Choose an installation base directory for LLVM 11
export BASE_DIR=/opt/llvm    # (you can choose a different directory if desired)

# Build and install LLVM 11 from source
sudo mkdir -p "$BASE_DIR"; sudo chown "$USER":"$USER" "$BASE_DIR"
/tmp/wire-sysio/scripts/llvm-11/llvm-11-ubuntu-build-source.sh

# or:
sudo --preserve-env=BASE_DIR /tmp/wire-sysio/scripts/llvm-11/llvm-11-ubuntu-build-source.sh
```

This will download the LLVM 11 source code, compile it, and install the libraries and tools to `$BASE_DIR/llvm-11`. (By default, the script places the final installation in `/opt/llvm/llvm-11` if `BASE_DIR=/opt/llvm`.)

> **IMPORTANT:** Remember the installation path of LLVM 11 – you will need to supply it to CMake in the build step. For example, `-DCMAKE_PREFIX_PATH=/opt/llvm/llvm-11`.

### 1d. Bootstrap vcpkg

Wire CDT uses the **vcpkg** package manager for managing third-party dependencies. After installing the required system packages above, you must bootstrap vcpkg from the root of the cloned `wire-cdt` repository:

```bash
# From the wire-cdt repository root directory
./vcpkg/bootstrap-vcpkg.sh
```

This will build the vcpkg executable and set up the local vcpkg infrastructure. (It may take a few minutes on first run.)

You are now ready to build Wire CDT.

### 1e. Optional - Enable Integration Tests

Integration tests require access to a build of [Wire Sysio](https://github.com/Wire-Network/wire-sysio).

If you do not wish to build Wire Sysio, you can skip this section and continue to [Step 2](#step-2---build). Otherwise, follow the instructions below before running `cmake`.

First, ensure that Wire Sysio has been built from source (see [BUILD.md](https://github.com/Wire-Network/wire-sysio/blob/master/BUILD.md) for details) and identify the build path, e.g. `/path/to/wire-sysio/build/`.

Then, execute the following command in the same terminal session that you will use to build CDT:

```bash
export sysio_DIR=/path/to/wire-sysio/build/lib/cmake/sysio
```
When you configure the build in Step 2, you will also need to add -DENABLE_INTEGRATION_TESTS=ON and -Dsysio_DIR to the cmake command. Here is a complete example:

```bash
cmake -B . -S .. \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_PREFIX_PATH="/path/to/wire-sysio/build;/opt/llvm/llvm-11" \
-DCMAKE_TOOLCHAIN_FILE="$PWD/../vcpkg/scripts/buildsystems/vcpkg.cmake" \
-Dsysio_DIR=/path/to/wire-sysio/build/lib/cmake/sysio \
-DENABLE_INTEGRATION_TESTS=ON
```

Now you can continue with the steps to build CDT as described. When you run `cmake` make sure that it does not report `sysio package not found`. If it does, this means CDT was not able to find a build of Wire Sysio at the specified path in `sysio_DIR` and will therefore continue without building the integration tests.

### 1f. Optional - Disable ccache

If issues persist with ccache when building CDT, you can disable ccache:

```bash
export CCACHE_DISABLE=1
```

You are now ready to build Wire CDT.

## Step 2 - Build

Make sure you are in the root of the `wire-cdt` repo, then perform the build with CMake.

> ⚠️ **Memory/Parallelism Warning** ⚠️  
> Building Wire CDT from source can be resource-intensive. Some compilation units (.cpp files) in CDT are extremely complex and can consume a large amount of memory to compile. If you use all CPU cores for parallel compilation (e.g. `make -j$(nproc)`), you may exhaust memory and encounter compiler crashes. Consider using a lower parallel job count (`-j`) if you run into memory issues or if you need to use your machine for other tasks during the build.

### Build Instructions

Create a build directory and configure with CMake:

```bash
mkdir -p build
cd build

cmake -B . -S .. \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_PREFIX_PATH="/opt/llvm/llvm-11" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/../vcpkg/scripts/buildsystems/vcpkg.cmake"
```

In the above command:
- `-B . -S ..` tells CMake to use the current directory for build files and parent directory for source.
- `CMAKE_BUILD_TYPE=Release` produces an optimized build. (You can use `Debug` for development, etc.)
- `CMAKE_PREFIX_PATH` is set to the location of LLVM 11 installation. Adjust the path if you installed to a different prefix (from Step 1c).
- `CMAKE_TOOLCHAIN_FILE` points to the vcpkg toolchain file, so CMake will integrate `vcpkg` dependencies automatically.

If you enabled integration tests in Step 1e, verify the configuration output shows that Wire Sysio was found.

Now proceed to compile:

```bash
make -j$(nproc)
```

This will start the build process (using all available CPU cores by default). If you find your system running low on memory or becoming unresponsive, cancel the build (`Ctrl+C`) and re-run with a lower parallel job count, for example:

```bash
make -j4
```

(to limit to 4 threads).

Once the build completes successfully, the Wire CDT binaries will be available in `build/bin/`.

You can now proceed to [test](README.md#testing) your build (optional), or install CDT as described below.

### Optional - Build in Debug Mode

To build CDT with debug symbols for development, configure with debug flags:

```bash
cmake -DCMAKE_BUILD_TYPE="Debug" -DTOOLS_BUILD_TYPE="Debug" -DLIBS_BUILD_TYPE="Debug" ..
make -j$(nproc)
```

## Step 3 - Install

After you have [built](#step-2---build) Wire CDT (and optionally [tested](README.md#testing) it), you can install it on your system. There are three primary ways to install:

**A. Install via Debian Package:**  
We recommend installing using the Debian package that the build can produce, as it ensures all files go to appropriate system locations. To build the package, run:

```bash
cd build/packages
./generate_package.sh deb ubuntu-24.04 amd64
```

This will create the Wire CDT `.deb` package. Then install it using `apt`:

```bash
sudo apt install ./cdt_*_amd64.deb
```

*(Replace `cdt_*_amd64.deb` with the actual filename of the package. Using `apt install` on the local file will automatically handle any missing dependencies.)*

**B. Install via CMake target:**  
Alternatively, you can install the built files directly to your system using CMake. This is useful if you prefer not to create a package:

```bash
cd build
sudo cmake --install .
```

This will copy the Wire CDT binaries, libraries, and headers to the default installation prefixes (e.g., under `/usr/local/` by default, or whatever `CMAKE_INSTALL_PREFIX` was set to during configuration). You may omit `sudo` if installing to a location your user has write access to.

**C. Use without installing:**  
You can also use Wire CDT directly from the build directory without installing system-wide:

```bash
# Add build/bin to your PATH
export PATH=/path/to/wire-cdt/build/bin:$PATH

# Or use the CMake toolchain file in your CMake projects
# -DCMAKE_TOOLCHAIN_FILE=/path/to/wire-cdt/build/lib/cmake/CDTWasmToolchain.cmake
```

This allows you to compile contracts without installing CDT globally.

Choose **either** method A, B, or C according to your preference. Method A (deb package) is cleaner for system installs, as it can be easily removed or upgraded using the system package manager.

### Installed Tools

When installed globally (via method A or B), CDT provides the following command-line tools:

- **cdt-abidiff** – ABI comparison tool
- **cdt-ar** – Archive utility
- **cdt-cc** – C compiler wrapper
- **cdt-cpp** – C preprocessor
- **cdt-init** – Project initialization tool
- **cdt-ld** – Linker
- **cdt-nm** – Symbol listing tool
- **cdt-objcopy** – Object file utility
- **cdt-objdump** – Object file disassembler
- **cdt-ranlib** – Archive index generator
- **cdt-readelf** – ELF file reader
- **cdt-strip** – Symbol stripper
- **sysio-pp** – Pre-processor
- **sysio-wasm2wast** – WASM to WAST converter
- **sysio-wast2wasm** – WAST to WASM converter

It also installs CMake files for CDT accessible within a `cmake/cdt` directory located within your system's `lib` directory.

## Uninstall

### Uninstall CDT (if installed via package):

```bash
sudo apt remove cdt
```

### Uninstall CDT (if installed via CMake):

```bash
sudo rm -fr /usr/local/cdt
sudo rm -fr /usr/local/lib/cmake/cdt
sudo rm /usr/local/bin/sysio-*
sudo rm /usr/local/bin/cdt-*
```

---