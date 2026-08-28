# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Code Quality Standards

This is compiler/toolchain code for blockchain smart contracts. **Prefer the best solution over the simplest one.** Correctness and reproducibility always take priority over brevity or speed of implementation.

- **Deterministic output**: The toolchain produces WASM that executes on-chain, where every node must compute identical results. Codegen, the ABI generator, and the post-pass must be deterministic — no iteration over unordered containers when order reaches the output, no uninitialized reads, no undefined behavior, no host-dependent behavior baked into an artifact.
- **ABI stability**: A change to `abigen.hpp`, the `.desc` format, or the dispatch generator changes the ABI or the WASM of every contract built with it. Treat those as breaking until proven otherwise, and add a toolchain test that pins the expected output.
- **Thoroughness over shortcuts**: Take the time to understand the full problem. Read all relevant code before proposing changes. Do not suggest partial or "quick fix" solutions when a complete, well-designed solution is achievable.
- **Complete test coverage**: Every change should include tests covering normal paths, edge cases, and error conditions. For the toolchain, that means a `tests/toolchain/*` case; for library code, a `tests/unit/*_tests.cpp` case; for anything that must run on a real chain, a `tests/integration/*` case.
- **Robustness**: Handle error conditions properly. Validate inputs at system boundaries — a contract author's malformed source must produce a diagnostic, never a crash or a silently wrong ABI.
- **Clean code**: Use clear naming, consistent patterns, and appropriate abstractions. Code should be easy to audit and review.

Do not optimize for token economy or response brevity at the expense of code quality. A longer, more thorough response that produces correct, well-tested code is always preferred.

## Code Quality Invariants

Apply these to every change in this repo. Check them before declaring a task complete — they are not optional polish.

### 1. No duplicated helpers

If the same helper / check / calculation appears in two translation units, extract it. Pick the home by scope:

- **One tool, internal:** anonymous namespace in the `.cpp`, or a `private`/`protected` member on the owning class.
- **Shared across the Clang plugins:** a header under `plugins/sysio/` (`gen.hpp`, `utils.hpp`, `clang_wrapper.hpp`, `error_emitter.hpp`).
- **Shared across host tools:** a header under `tools/include/`.
- **Shared by contract-facing library code:** the appropriate header under `libraries/sysiolib/core/sysio/` or `libraries/sysiolib/contracts/sysio/`.
- **Common behaviour every subclass of an abstract base needs:** `protected` member (or `static` if stateless) on the base, NOT repeated in every concrete.

### 2. No magic literals

Every string or numeric value that isn't a trivial array index / loop bound lives behind a named `constexpr`:

- **File-local:** `constexpr std::string_view` / `constexpr auto` in the anonymous namespace at the top of the `.cpp`.
- **Shared:** `inline constexpr` in a header (`constexpr` variables are implicitly inline since C++17 — being explicit never hurts).
- **Toolchain-facing identifiers** (attribute spellings, plugin names, plugin-arg keys, generated-file suffixes, ABI version strings, section names) group by concern in a nested `namespace`, e.g.

  ```cpp
  namespace gen {
     constexpr auto actions_suffix  = ".actions.cpp";
     constexpr auto dispatch_suffix = ".dispatch.cpp";
     constexpr auto desc_suffix     = ".desc";
     constexpr auto attrs_plugin    = "sysio_attrs.so";
  }
  ```

  A suffix or version rename becomes one grep-and-change, not twenty scattered literals.

### 3. Enums over raw values

Any value drawn from a closed set — attribute kind, ABI type class, diagnostic severity, build mode — uses the enum member, never the underlying `int` or `string`.

#### Enum conversions — use `magic_enum`, never `static_cast`

`magic_enum` is a vcpkg dependency and is shipped into the CDT include tree (`find_cdt_magic_enum()` in `cmake/CDTMacros.cmake.in`), so it is available both to host tools and to contract code. Every enum ↔ raw value conversion goes through it (`#include <magic_enum/magic_enum.hpp>`). `static_cast` and hand-rolled switches survive rename refactors silently; `magic_enum` does not.

| Need | Use |
|---|---|
| Enum → underlying integer | `magic_enum::enum_integer(v)` |
| Enum → string name (runtime) | `magic_enum::enum_name(v)` |
| Integer → enum (checked `std::optional<E>`) | `magic_enum::enum_cast<E>(n)` |
| String name → enum (checked `std::optional<E>`) | `magic_enum::enum_cast<E>(name)` |
| N-th member by declaration order | `magic_enum::enum_value<E>(i)` |

Forbidden once a `magic_enum` form exists:

- `static_cast<uint64_t>(SomeEnum::X)` — use `magic_enum::enum_integer(SomeEnum::X)`.
- `static_cast<SomeEnum>(int_var)` at a trust boundary — use `magic_enum::enum_cast<SomeEnum>(int_var).value_or(SomeEnum::DEFAULT)`; static_cast past the enum's declared range is UB and it hides bad input.
- Hand-rolled `if/switch` tables that map an enum to its spelling — use `magic_enum::enum_name`.

**Exceptions:**
- **Clang/LLVM enums.** Use the upstream API (`clang::attr::Kind`, `llvm::StringSwitch`, `Attr::getKind()`, etc.) as LLVM itself does. Do not wrap LLVM enums in `magic_enum`.
- **Protobuf-generated enums.** `protoc` emits `<EnumName>_Name(int)` / `<EnumName>_Parse` for every proto enum. Use those for `.pb.h` types — the generated `_Name` encodes the exact wire-format spelling and stays in lock-step with the `.proto` on every regeneration.

## Project Overview

Wire CDT (Contract Development Toolkit) is a C/C++ toolchain targeting WebAssembly for smart contracts deployed to a Wire blockchain. It bundles **standard LLVM 18 from vcpkg** plus **sysio-specific Clang frontend plugins** — there is no LLVM fork. It ships the compiler drivers, the ABI generator, the contract runtime libraries (`sysiolib`), a WASM libc/libc++, a native (host) contract-testing path, and the CMake package (`find_package(cdt)`) that downstream projects — notably `wire-sysio` — build their contracts with.

### Toolchain pipeline

```
cdt-cpp (compiler driver)
   ├── cdt-codegen (orchestrator, link builds only)
   │      └── clang++ -fsyntax-only
   │             ├── sysio_attrs.so    [[sysio::*]] → AnnotateAttr
   │             └── sysio_codegen.so  action wrappers + ABI metadata
   │      ⇒ <name>.actions.cpp, <name>.desc  ⇒ merged ⇒ <name>.dispatch.cpp, <name>.abi
   └── clang++ (real compile, plugins loaded in validation-only mode)

cdt-ld (linker driver)
   └── wasm-ld  ⇒  sysio-pp (WABT post-pass)  ⇒  final .wasm
```

- **`cdt-cpp` / `cdt-cc`** — compiler wrappers. Run `cdt-codegen`, then the bundled `clang++`/`clang`.
- **`cdt-codegen`** — orchestrator. Invokes `clang++` with the two plugins, collects `.desc` files, merges them (`tools/include/sysio/abimerge.hpp`), emits `<contract>.dispatch.cpp` (the `apply()` router) and `<contract>.abi`. Filters inherited `-fplugin`/`-add-plugin`/`-plugin-arg` flags to prevent double-loading.
- **`cdt-ld`** — linker wrapper. Calls `wasm-ld`, then `sysio-pp`.
- **`sysio-pp`** (`tools/post-pass/postpass.cc`) — WABT-based post-pass over the linked WASM.
- **`cdt-abidiff`**, **`cdt-init`**, **`cdt-protoc`**, **`cdt-protoc-gen-zpp`**, **`cdt-wast2wasm`** / **`cdt-wasm2wast`** — supporting tools.

Generated `apply()` in `dispatch.cpp` is `__attribute__((weak))` so a `SYSIO_DISPATCH` macro's strong symbol wins in multi-file contracts.

## Build Commands

Builds go under `build/` (`.gitignore` covers `[Bb]uild*/`). `CMakePresets.json` defines `debug-cdt` → `build/debug` and `release-cdt` → `build/release`. Examples below use `$BUILD_DIR` — substitute your actual build path. In-source builds are rejected by `CMakeLists.txt`.

### Prerequisites (one-time setup)

```bash
# Ubuntu 24.04 x86_64 — see BUILD.md for the full package list
sudo apt-get install -y build-essential ccache cmake curl git jq ninja-build \
    libcurl4-openssl-dev libedit-dev libffi-dev libgmp-dev libncurses-dev \
    libssl-dev libtinfo-dev libxml2-dev libzstd-dev python3 python3-dev zlib1g-dev

# LLVM 18 from apt.llvm.org (matches CI; keeps vcpkg binary-cache ABI keys aligned)
sudo apt-get install -y clang-18 clang-tools-18 lld-18 libclang-18-dev \
    libclang-common-18-dev libclang-cpp18 libclang-rt-18-dev llvm-18 llvm-18-dev

# Submodules: cdt-musl, cdt-libcxx, berkeley-softfloat-3, vcpkg
git submodule update --init --recursive

./vcpkg/bootstrap-vcpkg.sh
```

macOS Apple Silicon is supported (Xcode CLT + Homebrew `cmake ninja ccache python`); use the `arm64-osx-release` triplets. See [BUILD.md](./BUILD.md).

### Configure and Build (presets — preferred)

```bash
cmake --preset debug-cdt          # or release-cdt
cmake --build --preset debug-cdt
ctest --preset debug-cdt
```

### Configure and Build (manual)

```bash
# Clear `linuxbrew` from PATH (if present) to avoid conflicts with system libraries and compilers.
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v linuxbrew | tr '\n' ':' | sed 's/:$//')

export BUILD_DIR=$PWD/build/debug
export CC=/usr/bin/clang-18
export CXX=/usr/bin/clang++-18
export VCPKG_TARGET_TRIPLET=x64-linux-release
export VCPKG_HOST_TRIPLET=x64-linux-release

cmake -B $BUILD_DIR -S . -G Ninja \
  -DCMAKE_C_COMPILER="$CC" \
  -DCMAKE_CXX_COMPILER="$CXX" \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/vcpkg/scripts/buildsystems/vcpkg.cmake" \
  -DCMAKE_BUILD_TYPE=Debug \
  -DVCPKG_TARGET_TRIPLET="$VCPKG_TARGET_TRIPLET" \
  -DVCPKG_HOST_TRIPLET="$VCPKG_HOST_TRIPLET" \
  -DVCPKG_OVERLAY_TRIPLETS="$PWD/.github/vcpkg-triplets"

export NUM_JOBS=$(echo $(($(nproc) - 2)))
cmake --build $BUILD_DIR -- -j${NUM_JOBS}
```

The first configure builds LLVM 18 through vcpkg and takes a long time; subsequent configures hit the binary cache.

### Build layout

The top-level project builds the host tools and plugins **in the main CMake graph** (`cmake/Tools.cmake` → `add_subdirectory(tools)`). Two things are ExternalProjects, because they are cross-compiled with the CDT toolchain that this very build produces:

| ExternalProject | Source | Binary dir | Built with |
|---|---|---|---|
| `CDTWasmLibraries` | `libraries/` | `$BUILD_DIR/libraries` | `$BUILD_DIR/lib/cmake/cdt/CDTWasmToolchain.cmake` |
| `CDTWasmTests` | `tests/unit/` | `$BUILD_DIR/tests/unit` | same |
| `CDTIntegrationTests` (opt-in) | `tests/integration/` | `$BUILD_DIR/tests/integration` | host compiler + `find_package(sysio)` |

All three are `BUILD_ALWAYS 1`, so a top-level build re-enters them every time.

Build outputs: drivers, plugins and bundled LLVM tools in `$BUILD_DIR/bin/`; the CMake package in `$BUILD_DIR/lib/cmake/cdt/`.

### Building Specific Targets

```bash
ninja -C $BUILD_DIR CDTTools            # host tools + Clang plugins only
ninja -C $BUILD_DIR CDTWasmLibraries    # sysiolib / libc / libc++ / native / rt
ninja -C $BUILD_DIR CDTWasmTests        # unit test executables + tests/unit/test_contracts
ninja -C $BUILD_DIR cdt-cpp             # a single driver
```

## Testing Commands

Three suites, distinguished by ctest label: `unit_tests`, `toolchain_tests`, `integration_tests`.

### Run everything

```bash
ctest --test-dir $BUILD_DIR -j "$(nproc)" --output-on-failure
```

**Tip:** toolchain tests are slow. Log output to a temp file so you can grep/tail without re-running:

```bash
ctest --test-dir $BUILD_DIR -j "$(nproc)" --output-on-failure 2>&1 | tee /tmp/ctest-run.log
grep "Failed" /tmp/ctest-run.log
grep "% tests passed" /tmp/ctest-run.log
```

### Unit tests

`tests/unit/*_tests.cpp` are **native host executables** built through the CDT toolchain's native path (`add_native_executable`), using the in-tree `SYSIO_TEST` framework from `libraries/native/native/sysio/tester.hpp` (`SYSIO_TEST_BEGIN` / `CHECK_EQUAL` / `CHECK_ASSERT` / `REQUIRE_*`). They are not Boost.Test.

```bash
ctest --test-dir $BUILD_DIR -L unit_tests -j "$(nproc)"
ctest --test-dir $BUILD_DIR -R name_tests --output-on-failure

# Or run the binary directly — one executable per suite
$BUILD_DIR/tests/unit/kv_cached_tests
```

Adding a unit test needs **two** registrations: `add_cdt_unit_test(<name>)` in `tests/unit/CMakeLists.txt` (builds it) and `add_unit_test(<name>)` in `tests/CMakeLists.txt` (registers it with ctest). Miss the second and the test still compiles but `ctest` never executes it — a silent gap, not a failure. `basic_name_tests` sat that way until it was registered; when adding a test, check both lists match.

A few unit tests are shell scripts registered directly in `tests/CMakeLists.txt`: `version_tests`, `abi_version_tests`, `abidiff_tests`, `multidir_contract_tests`, `postpass_tests`, `staged_headers_tests`.

### Toolchain tests

`tests/toolchain/` drives the compiler end to end. Each test is a `.cpp` plus a sibling `.json` declaring expected output (`exit-code`, `stderr`, `wasm`, `abi`), grouped into directories by kind:

| Directory | Expectation |
|---|---|
| `compile-pass` / `compile-fail` | compiles / fails to compile |
| `build-pass` / `build-fail` | compiles and links / fails |
| `abigen-pass` / `abigen-fail` | ABI generated correctly / generation fails |

```bash
ctest --test-dir $BUILD_DIR -R toolchain_tests --output-on-failure

# Run one test or one suite directly — much faster than the ctest meta-test
$BUILD_DIR/tools/toolchain-tester/toolchain-tester tests/toolchain \
    --cdt $BUILD_DIR/bin --verbose -t <test_name>
```

Start every toolchain test file with a comment saying what it exercises and, where relevant, which bug it pins and the issue/PR number. Regenerate an expected WASM blob with `xxd -p <file>`. See `tools/toolchain-tester/README.md`.

### Integration tests

Off by default. They need a built [wire-sysio](https://github.com/Wire-Network/wire-sysio) and use Boost.Test against the real chain runtime.

```bash
export sysio_DIR=/path/to/wire-sysio/build/lib/cmake/sysio
cmake -B $BUILD_DIR -S . -G Ninja ... -Dsysio_DIR="$sysio_DIR" -DENABLE_INTEGRATION_TESTS=ON
cmake --build $BUILD_DIR

ctest --test-dir $BUILD_DIR/tests/integration --output-on-failure
$BUILD_DIR/tests/integration/integration_tests --run_test=<suite> --report_level=detailed
```

`tests/integration/CMakeLists.txt` derives one ctest entry per `BOOST_AUTO_TEST_SUITE` name, trimming a trailing `_test`/`_tests` and appending `_integration_test`. As in wire-sysio, `--run_test=` takes the **suite name declared in the source**, not the filename — find it with `grep "BOOST_AUTO_TEST_SUITE(" <file.cpp>`.

**Pre-PR sweep:** build and run all three suites (with `-DENABLE_INTEGRATION_TESTS=ON` when the change can affect generated code, the ABI, or the runtime libraries).

## Architecture Overview

### Directory Structure

- **`tools/`** — host tools: `cc/` (`cdt-cpp`, `cdt-cc`), `ld/` (`cdt-ld`), `codegen/` (`cdt-codegen`), `abidiff/`, `init/`, `post-pass/` (`sysio-pp`), `protoc-gen-zpp/`, `toolchain-tester/`, `include/` (shared headers incl. `compiler_options.hpp.in`, `sysio/abimerge.hpp`), `packaging/`
- **`plugins/sysio/`** — the two Clang frontend plugins and their supporting headers
- **`libraries/`** — cross-compiled to WASM: `sysiolib/` (contract API), `libc/` (cdt-musl), `libc++/` (cdt-libcxx), `boost/`, `meta_refl/`, `rt/`, `native/` (host-side contract testing + softfloat)
- **`cmake/`** — the CMake package templates (`cdt-config.cmake.in`, `CDTMacros.cmake.in`, `CDTWasmToolchain.cmake.in`, `CDTInternalMacros.cmake`), ExternalProject wiring, CPack layout hooks
- **`tests/`** — `unit/`, `toolchain/`, `integration/`
- **`examples/`** — reference contracts (`hello`, `multi_index_example`, `kv_table_example`, `kv_global_example`, `singleton_example`, `send_inline`, `hash_id_example`)
- **`docs/`** — feature documentation (see index below)
- **`imports/cdt.imports.in`** — the allowed WASM import list

### Clang plugins

Both live in `plugins/sysio/` and are loaded as `.so`s from `$BUILD_DIR/bin/`.

**`sysio_attrs.so`** — registers the `[[sysio::*]]` attributes via `ParsedAttrInfoRegistry` and lowers each to a standard `AnnotateAttr` (so the AST stays stock Clang). Loaded during *all* compilations. The set is declared by the `SYSIO_ATTR` macro table at the bottom of `sysio_attrs.cpp`:

| Attribute | Applies to |
|---|---|
| `[[sysio::contract("name")]]` | class / method |
| `[[sysio::action("name")]]` | class / method |
| `[[sysio::table("name")]]` | class |
| `[[sysio::on_notify("acct::action")]]` | method |
| `[[sysio::ignore]]` | class / struct |
| `[[sysio::ricardian("...")]]` | class / method |
| `[[sysio::read_only]]` | function |
| `[[sysio::kv_key("name")]]` | class |
| `[[sysio::type("name")]]` | field |
| `[[sysio::wasm_action]]` / `wasm_notify` / `wasm_abi` | function |
| `[[sysio::wasm_entry]]` | → `WebAssemblyExportNameAttr` |
| `[[sysio::wasm_import]]` | → `WebAssemblyImportNameAttr` |

Adding an attribute means one `SYSIO_ATTR(...)` line here plus a reader in `clang_wrapper.hpp`, which parses the `AnnotateAttr` strings back into a name→args map.

**`sysio_codegen.so`** — two `FrontendPluginRegistry` entries in one shared library, both `AddBeforeMainAction` (never `ReplaceAction` — that would suppress `.o` output when the plugin is loaded during a normal compile):

- `sysio_codegen` — generates `<name>.actions.cpp` action wrappers, populates `wasm_actions`/`wasm_notifies`/`wasm_entries` on the shared `abigen` singleton, and validates read-only actions.
- `sysio_abigen` — generates the `.desc` ABI-metadata JSON, locating the contract class by name via `contract_class_finder`.

Both **skip generation when `output` is empty**, which is how they run in validation-only mode during an ordinary compile that did not go through `cdt-codegen`.

### CMake package surface

`find_package(cdt)` pulls in `CDTMacros.cmake`. The macros downstream projects use:

| Macro | Purpose |
|---|---|
| `add_contract(CONTRACT_NAME TARGET srcs...)` | build a WASM contract (adds `-abigen`, `-abigen_output=`, `-contract`) |
| `target_ricardian_directory(TARGET DIR)` | point at `*.contracts.md` / `*.clauses.md` |
| `add_native_contract(TARGET ... CONTRACT_CLASS ... )` | build a dlopen-able `*_native.so` for the native-module runtime |
| `add_contract_native(CONTRACT_NAME TARGET ...)` | native build of a contract target |
| `target_add_protobuf(TARGET ...)` / `contract_use_protobuf(...)` | protobuf → `.pb.hpp` via `cdt-protoc-gen-zpp` |
| `add_native_library` / `add_native_executable` (internal) | host-compiled library/executable through the CDT native path |

**Changing any of these templates changes every downstream consumer.** `wire-sysio` builds all of its system contracts through them; verify a change there before treating it as done.

### Install / packaging layout

Three install shapes, each baking a different `CDT_ROOT`:

| Shape | Root | Applied by |
|---|---|---|
| deb / rpm | `/usr/lib/cdt` (+ `/usr/bin` symlinks for public entry points only) | `cmake/cpack-system-layout.cmake` (CPack pre-build hook) |
| portable tarball | `/opt/wire-cdt`, with relative-discovery fallback | `cmake/cpack-tgz-toolchain-root.cmake` |
| `cmake --install` | the effective `CMAKE_INSTALL_PREFIX`, configured **at install time** inside `install(CODE ...)` | top-level `CMakeLists.txt` |

The install-time configure is deliberate: `--prefix` is only known when `cmake --install` runs, so a configure-time bake would ship a toolchain naming the wrong root. Do not "simplify" it into a plain `install(DIRECTORY)`. Likewise, do not remove `cdt-config.cmake.in`'s relative-discovery fallback — it is what makes a non-`/opt` tarball extraction work. Full rationale is in the comments in `CMakeLists.txt` and in [BUILD.md](./BUILD.md).

## Generated Artifacts

CDT generates `<name>.actions.cpp`, `<name>.dispatch.cpp`, `<name>.desc`, and `<name>.abi` alongside compiled contracts. These are build products and are **never committed** — here or in consuming repos. If they appear as untracked in a source tree:

```bash
find . -name "*.actions.cpp" -o -name "*.dispatch.cpp" -o -name "*.desc" | xargs rm -f
```

`wire-sysio` carries `.gitignore` rules for the same files under `contracts/` and `unittests/test-contracts/`.

## Code Style

There is **no repo-wide `.clang-format`** (the only one is vendored inside `libraries/libc++/cdt-libcxx/` and belongs to that upstream). Match the surrounding file. The house style, consistent with wire-sysio:

- **Indent: 3 spaces** (not 4) — used throughout `sysiolib`, the plugins, and the CMake files
- **Line limit: 120 characters** for new code
- **Pointer alignment: Left** (`int* ptr` not `int *ptr`)
- **Constructor initializers: Break before comma**

Vendored submodules (`cdt-musl`, `cdt-libcxx`, `berkeley-softfloat-3`) and `tools/jsoncons/` keep their upstream style — do not reformat them.

## Documentation Comments

All generated or modified code **must** include documentation comments.

- **C/C++**: Doxygen-style (`/** ... */` or `/// ...`). Put doc comments in the header when the declaration lives in a header; use implementation-file comments only for internal/static functions with no header declaration. Public `sysiolib` headers are the contract-author-facing API — document parameters, preconditions, and the `check()` failures a call can raise.
- **Python** (`toolchain-tester`, `scripts/`): docstrings (`"""..."""`), Sphinx/MkDocs compatible. Not JSDoc.
- **CMake**: comment macros with the `# @param` form already used in `CDTMacros.cmake.in`.

## Git Practices

- **Do NOT commit without explicit permission.**
- Commit messages follow Conventional Commits as used in this repo's history: `feat(kv):`, `fix(kv):`, `refactor(kv):`, `docs(kv):`, `chore:`, `ci:`, `packaging:`.
- `git add -A` / `git add .` are fine — an authorized commit stages the whole working tree rather than silently omitting files someone judged "unrelated". Build artifacts and generated contract files are kept out by `.gitignore`; if something untracked shows up that shouldn't be committed, fix the ignore rules rather than hand-picking paths at stage time.
- No AI attribution in commit messages or PR bodies — no `Co-Authored-By` lines, no "Generated with Claude Code".

## Key CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `ENABLE_TESTS` | ON | Build unit + toolchain tests |
| `ENABLE_INTEGRATION_TESTS` | OFF | Build integration tests (requires `ENABLE_TESTS` and `sysio_DIR`) |
| `ENABLE_NATIVE_COMPILER` | ON | Build the native (host) contract path |
| `ENABLE_PACKAGE` | ON | Include CPack rules |
| `CMAKE_BUILD_TYPE` | Release | Build type for the top-level project |
| `TOOLS_BUILD_TYPE` | Release | Build type for the host tools |
| `LIBS_BUILD_TYPE` | Release | Build type for the WASM libraries |
| `VCPKG_TARGET_TRIPLET` | — | `x64-linux-release` (Linux) / `arm64-osx-release` (macOS) |
| `sysio_DIR` | — | Path to wire-sysio's `lib/cmake/sysio`, for integration tests |

## Versioning

`VERSION_MAJOR` / `VERSION_MINOR` / `VERSION_PATCH` / `VERSION_SUFFIX` at the top of `CMakeLists.txt` are the **single source of truth** for the distribution version. `prepare-release.yaml` rewrites them and `release.yaml` cross-checks them against the tag — don't set the version anywhere else. A non-empty `VERSION_SUFFIX` (`dev`, `rc1`, …) marks a pre-release. The CMake project / `find_package` name stays `cdt` regardless; the distribution name is `wire-cdt` (`WIRE_PACKAGE_NAME` in `cmake/package.cmake`). See [docs/release-workflow.md](./docs/release-workflow.md).

## Relationship to wire-sysio

CDT and wire-sysio are coupled in both directions:

- wire-sysio builds its system and test contracts with this toolchain, through `find_package(cdt)` and the `CDTMacros` above. A change to codegen, the ABI generator, `sysiolib`, or the CMake macros can break its contract builds or change its WASM hashes.
- Changing a contract's WASM in wire-sysio invalidates its pre-generated reference data (deep-mind log, snapshots, consensus blockchain data) — that regeneration is documented in wire-sysio's own CLAUDE.md.
- Integration tests here require a wire-sysio build; version compatibility is checked by `SYSIO_CHECK_VERSION` in `tests/integration/CMakeLists.txt`.

When a change is likely to be visible downstream, build wire-sysio's contracts against the modified CDT before calling it done.

## Docs Index

| Doc | Topic |
|---|---|
| [docs/migrating-from-antelope.md](./docs/migrating-from-antelope.md) | Porting a contract from EOS/Telos/WAX/Antelope: getting started, renames, storage, resources, host-function diff |
| [docs/kv-storage-guide.md](./docs/kv-storage-guide.md) | Overview of the KV storage layer |
| [docs/kv-table.md](./docs/kv-table.md) | `sysio::kv::table` |
| [docs/kv-multi-index.md](./docs/kv-multi-index.md) | `sysio::multi_index` |
| [docs/kv-scoped-table.md](./docs/kv-scoped-table.md) | `sysio::kv::scoped_table` |
| [docs/kv-global.md](./docs/kv-global.md) | `sysio::kv::global` |
| [docs/kv-abi-key-metadata.md](./docs/kv-abi-key-metadata.md) | How KV key metadata lands in the ABI |
| [docs/kv-intrinsics-reference.md](./docs/kv-intrinsics-reference.md) | KV host intrinsics |
| [docs/protocol-buffers.md](./docs/protocol-buffers.md) | Protobuf support (`cdt-protoc`, `protoc-gen-zpp`) |
| [docs/native-tester-compilation.md](./docs/native-tester-compilation.md) | Native tester and host-side compilation |
| [docs/release-workflow.md](./docs/release-workflow.md) | Release process |
| [BUILD.md](./BUILD.md) | Full build, install, and packaging reference |
| [LLVM_18_PLUGIN_REFACTOR.md](./LLVM_18_PLUGIN_REFACTOR.md) | Why the plugin architecture looks the way it does |
