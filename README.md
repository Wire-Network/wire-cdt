# Wire CDT (Contract Development Toolkit)

Wire Contract Development Toolkit (CDT) is a C/C++ toolchain targeting WebAssembly (WASM) and a set of tools to facilitate development of smart contracts written in C/C++ that are meant to be deployed to a Wire blockchain.

## Branches

The `master` branch is the latest stable branch.

## Supported Operating Systems

We currently support the following operating systems.

| **Operating Systems** |
|-----------------------|
| Ubuntu 24.04 Jammy    |

## Installation

In the future, we plan to support downloading Debian packages directly from our [release page](https://github.com/Wire-Network/wire-cdt/releases), providing a more streamlined and convenient setup process. However, for the time being, installation requires *building the software from source*.

Finally, verify Wire CDT was installed correctly:

```bash
cdt-cpp --version
```

You should see version information with no errors. For example:

```
Wire CDT version 4.x.x
```

## Building from source

Follow the instructions in [BUILD.md](./BUILD.md) to build Wire CDT from source.

## Coming from EOS, Telos, WAX or another Antelope chain?

[docs/migrating-from-antelope.md](./docs/migrating-from-antelope.md) walks through getting started on
Wire and porting an existing contract — the `eosio` → `sysio` renames, the KV storage layer that
replaces the legacy `db_*_i64` tables, the host functions Wire adds and removes, and the resource
model change that matters most: on Wire the **contract** is billed for CPU, NET and RAM, not the
signer.

## Testing

Wire CDT supports the following test suites:

| Test Suite                              | Test Type            | Notes                                                                                    |
|-----------------------------------------|:--------------------:|------------------------------------------------------------------------------------------|
| [Unit tests](#unit-tests)               | Unit tests           | Fast unit tests covering core functionality                                              |
| [Integration tests](#integration-tests) | Integration          | Tests requiring Wire Sysio build; optional but recommended if developing contracts       |

When building from source, we recommend running at least the [unit tests](#unit-tests).

#### Unit Tests

The unit test suite consists of tests that verify core CDT functionality without external dependencies.

You can invoke them by running `ctest` from a terminal in your build directory:

```bash
ctest
```

#### Integration Tests

The integration test suite requires a built version of [Wire Sysio](https://github.com/Wire-Network/wire-sysio) and tests contract compilation and execution end-to-end.

To build and run integration tests, you must first build Wire Sysio from source and set the `sysio_DIR` environment variable before configuring the CDT build. You also need to pass `ENABLE_INTEGRATION_TESTS=ON` to cmake. See [BUILD.md](./BUILD.md) for detailed instructions.

You can invoke them by running `ctest` from a terminal in your integration tests directory:

```bash
cd build/tests/integration
ctest
```

---

<!-- markdownlint-disable MD033 -->
<table>
  <tr>
    <td><img src="https://bucket.gitgo.app/frontend-assets/icons/favicon.png" alt="Wire Network" width="50"/></td>
    <td>
      <strong>Wire Network</strong><br>
      <a href="https://www.wire.network/">Website</a> |
      <a href="https://x.com/wire_blockchain">Twitter</a> |
      <a href="https://www.linkedin.com/company/wire-network-blockchain/">LinkedIn</a><br>
      © 2024 Wire Network. All rights reserved.
    </td>
  </tr>
</table>
