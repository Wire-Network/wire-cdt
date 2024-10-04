# Wire CDT (Contract Development Toolkit)

Wire Contract Development Toolkit (CDT) is a C/C++ toolchain targeting WebAssembly (WASM) and a set of tools to facilitate development of smart contracts written in C/C++ that are meant to be deployed to a Wire blockchain.

## Branches

The `master` branch is the latest stable branch.

## Supported Operating Systems

We currently support the following operating systems.

| **Operating Systems**           |
|---------------------------------|
| Ubuntu 22.04 Jammy              |
| Ubuntu 20.04 Focal              |
| Ubuntu 18.04 Bionic             |

## Installation

In the future, we plan to support the installation of Debian packages directly from our [Release page](https://github.com/Wire-Network/wire-cdt/releases), providing a more streamlined and convenient setup process. However, for the time being, installation requires **building the software from source**.

### Building from source

The instructions below assume that you are building on Ubuntu 20.04. and 22.04.

### Install dependencies

```sh
sudo apt-get update && sudo apt-get install   \
        build-essential             \
        clang                       \
        cmake                       \
        git                         \
        libxml2-dev                 \
        opam ocaml-interp           \
        python3                     \
        python3-pip                 \
        time
```

```sh
python3 -m pip install pygments
```

---

#### Optional - Build with integration tests

Integration tests require access to a build of [Wire Sysio](https://github.com/Wire-Network/wire-sysio).

If you do not wish to build Wire Sysio, you can skip this section and continue to [Build CDT](#build-cdt). Otherwise, follow the instructions below before running `cmake`.

First, ensure that Wire Sysio has been built from source (see [README](https://github.com/Wire-Network/wire-sysio/wire_sysio#building-from-source) for details) and identify the build path, e.g. `/path/to/wire-sysio/build/`.

Then, execute the following command in the same terminal session that you will use to build CDT:

```sh
export wire_sysio_DIR=/path/to/wire-sysio/build/lib/cmake/wire-sysio
```

Now you can continue with the steps to build CDT as described. When you run `cmake` make sure that it does not report `wire-sysio package not found`. If it does, this means CDT was not able to find a build of Wire Sysio at the specified path in `wire_sysio_DIR` and will therefore continue without building the integration tests.

### ccache

If issues persist with ccache when building CDT, you can disable ccache:

```sh
export CCACHE_DISABLE=1
```

---

### Build CDT

> [!WARNING]  
> **About Compilation Jobs (`-j` flag)**:
>
> When building C/C++ software often the build is performed in > parallel via a command such as `make -j $(nproc)` which uses the > number of CPU cores as the number of compilation jobs to perform > simultaneously. However, be aware that some compilation units (.cpp > files) in CDT are extremely complex and can consume a large amount > of memory to compile. If you are running into issues due to amount > of memory available on your build host, you may need to reduce the > level of parallelization used for the build. For example, instead of > `make -j $(nproc)` you can try `make -j2`. Failures due to memory > exhaustion will typically but not always manifest as compiler > crashes.

Use the commands below to clone this repository along with its submodules, set up the build environment, and compile the project:

```sh
git clone --recursive https://github.com/Wire-Network/wire-cdt && cd wire-cdt && mkdir build && cd build 
cmake ..
make -j $(nproc)
```

The binaries will be located at in the `build/bin` directory.

You can export the path to the directory to your `PATH` environment variable which allows you to conveniently use them to compile contracts without installing CDT globally.

Alternatively, you can use CMake toolchain file located in `build/lib/cmake/CDTWasmToolchain.cmake` to compile the contracts in your CMake project, which also allows you to avoid installing CDT globally.

If you would prefer to install CDT globally, see the section [Install CDT](#install-cdt) below.

### Run tests

#### Run unit tests

```sh
cd build

ctest
```

#### Run integration tests (if CDT was build with integration tests)

```sh
cd build/tests/integration

ctest
```

### Install CDT

Installing CDT globally on your system will install the following tools in a location accessible to your `PATH`:

* cdt-abidiff
* cdt-ar
* cdt-cc
* cdt-cpp
* cdt-init
* cdt-ld
* cdt-nm
* cdt-objcopy
* cdt-objdump
* cdt-ranlib
* cdt-readelf
* cdt-strip
* sysio-pp
* sysio-wasm2wast
* sysio-wast2wasm

It will also install CMake files for CDT accessible within a `cmake/cdt` directory located within your system's `lib` directory.

#### Manual installation

One option for installing CDT globally is via `make install`. From within the `build` directory, run the following command:

```sh
sudo make install
```

#### Package installation

A better option for installing CDT globally is to generate a package and then install the package. This makes uninstalling CDT much easier.

From within the `build` directory, run the following commands to generate a Debian package:

```sh
cd packages
bash ./generate_package.sh deb ubuntu-20.04 amd64
sudo apt install ./cdt_*_amd64.deb
```

### Uninstall CDT

#### Uninstall CDT(if installed via `make install`)

```sh
sudo rm -fr /usr/local/cdt
sudo rm -fr /usr/local/lib/cmake/cdt
sudo rm /usr/local/bin/sysio-*
sudo rm /usr/local/bin/cdt-*
```

#### Uninstall CDT( if installed via `apt install`)

```sh
sudo apt remove cdt
```

## License

[FSL-1.1-Apache-2.0](./LICENSE.md)

---

<!-- markdownlint-disable MD033 -->
<table>
  <tr>
    <td><img src="https://wire.foundation/favicon.ico" alt="Wire Network" width="50"/></td>
    <td>
      <strong>Wire Network</strong><br>
      <a href="https://www.wire.network/">Website</a> |
      <a href="https://x.com/wire_blockchain">Twitter</a> |
      <a href="https://www.linkedin.com/company/wire-network-blockchain/">LinkedIn</a><br>
      © 2024 Wire Network. All rights reserved.
    </td>
  </tr>
</table>
