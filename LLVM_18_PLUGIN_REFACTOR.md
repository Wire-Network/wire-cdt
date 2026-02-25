# Wire CDT: LLVM 18 Plugin Architecture Refactoring

## Overview

This document describes the refactoring of wire-cdt from a custom `cdt-llvm` fork (LLVM 9 with embedded sysio-specific modifications) to standard LLVM 18 via vcpkg, with all sysio-specific functionality implemented as dynamically-loaded Clang frontend plugins.

**Reference implementation**: `wire-cdt-taurus` (LLVM 13, `eosio` naming) was ported to LLVM 18 with `sysio` naming.

**Result**: All 20/20 tests pass (19 unit tests + 1 toolchain meta-test covering 29 subtests).

---

## Architecture

### Previous Architecture (cdt-llvm)

The `cdt-llvm` submodule contained these custom modifications directly in LLVM/Clang source:

1. **12 custom Clang attributes** in `Attr.td` (`sysio_action`, `sysio_contract`, `sysio_table`, `sysio_notify`, `sysio_ignore`, `sysio_ricardian`, `sysio_read_only`, `sysio_wasm_entry`, `sysio_wasm_import`, `sysio_wasm_action`, `sysio_wasm_notify`, `sysio_wasm_abi`)
2. **~30 custom AST methods** on `CXXMethodDecl`, `CXXRecordDecl`, `FunctionDecl`, `RecordDecl`
3. **CodeGen integration** in `CodeGenModule.cpp` converting Clang attrs to LLVM function attributes
4. **WASM backend sections** (`.sysio_abi`, `.sysio_actions`, `.sysio_notify`)
5. **SysioApply LLVM pass** - injected `sysio_set_contract_name()`, `__wasm_call_ctors()`, `__cxa_finalize()` into `apply()`
6. **SysioSoftfloat LLVM pass** (disabled)
7. **sysio_plugin** Clang frontend plugin for action parameter validation

### New Architecture (Plugin Model)

```
                    cdt-cpp (compiler wrapper)
                         |
            +------------+------------+
            |                         |
      cdt-codegen               clang++ (compile/link)
      (orchestrator)                  |
            |                   +-----------+
      clang++ -fsyntax-only     | sysio_attrs.so (attribute conversion)
            |                   | sysio_codegen.so (validation only)
      +-----------+             +-----------+
      | sysio_attrs.so    |
      | sysio_codegen.so  |
      +-----------+--------+
            |
      .desc files + .actions.cpp + .dispatch.cpp + .abi
```

#### Plugin: `sysio_attrs.so`
- Registers all sysio attributes via `ParsedAttrInfoRegistry`
- Converts `[[sysio::*]]` C++ attributes to standard `AnnotateAttr`
- Maps `sysio_wasm_entry` to `WebAssemblyExportNameAttr`
- Maps `sysio_wasm_import` to `WebAssemblyImportNameAttr`
- Loaded during ALL compilations

#### Plugin: `sysio_codegen.so`
Contains two `FrontendPluginRegistry` entries in a single shared library:

- **`sysio_codegen`** (`AddBeforeMainAction`): Generates action wrapper `.actions.cpp` files and populates `wasm_actions`/`wasm_notifies`/`wasm_entries` on the shared `abigen` singleton. Also performs read-only action validation via `process_read_only_actions()`. When loaded during normal compilation (no output configured), runs in validation-only mode.

- **`sysio_abigen`** (`AddBeforeMainAction`): Generates ABI metadata `.desc` files. Uses `contract_class_finder` to locate the target contract class by name. Skips processing when `output` is empty (loaded during normal compilation without cdt-codegen).

#### Tool: `cdt-codegen`
- Invokes `clang++` with `-fplugin=sysio_attrs.so -fplugin=sysio_codegen.so` and appropriate `-Xclang -plugin-arg-*` options
- Collects `.desc` files (JSON metadata from abigen plugin)
- Merges ABI from all desc files via `ABIMerger`
- Generates `contract.dispatch.cpp` (the `apply()` function with action/notify routing)
- Generates `contract.abi` (final ABI JSON)
- Filters out inherited `-fplugin`, `-add-plugin`, and `-plugin-arg` flags from compiler options to prevent double-loading

#### Compiler Wrapper: `cdt-cpp`
- Runs `cdt-codegen` as a subprocess for link builds
- Compiles generated `.actions.cpp` and `.dispatch.cpp` files
- Links everything with `wasm-ld`

### Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| `sysio_abigen` uses `AddBeforeMainAction` (not `ReplaceAction`) | Loading the .so during normal compilation with `ReplaceAction` would suppress code generation output (.o files) |
| Abigen skips when `output` is empty | When loaded during normal compilation (not via cdt-codegen), the abigen has nothing to write |
| Codegen skips file generation when `output` is empty | Validation-only mode during normal compilation |
| Always pass contract name to abigen | Even when auto-derived from filename, ensures correct contract class selection when multiple contracts exist in headers |
| Codegen uses auto-detect mode for dispatch | No contract name filter for the codegen plugin allows it to find any contract class for action wrapper generation |
| cdt-codegen filters inherited plugin flags | Prevents double-loading when compiler options already include `-fplugin` from `compiler_options.hpp.in` |
| SysioApply LLVM pass eliminated | `cdt-codegen` generates the `apply()` function as source code, making the IR-level pass unnecessary |
| SysioSoftfloat pass dropped entirely | Was already disabled behind `if (false)` |
| ABI output as separate `.abi` file | No custom WASM backend sections needed |

---

## Files Modified/Created

### New Files

| File | Description |
|------|-------------|
| `plugins/sysio/sysio_attrs.cpp` | Attribute registration plugin (ported from taurus `custom_attrs.cpp`) |
| `plugins/sysio/sysio_codegen.cpp` | Codegen + abigen plugin (ported from taurus `codegen.cpp`) |
| `plugins/sysio/abigen.hpp` | ABI generation logic (ported from taurus, with sysio-specific additions preserved) |
| `plugins/sysio/gen.hpp` | Generation utilities |
| `plugins/sysio/clang_wrapper.hpp` | Decl wrapper that reads `AnnotateAttr` annotations |
| `plugins/sysio/tokenize.hpp` | String tokenization utility |
| `plugins/sysio/error_emitter.hpp` | Error/warning reporting |
| `plugins/sysio/tracegen.hpp` | Debug tracing code injection |
| `plugins/sysio/CMakeLists.txt` | Plugin build configuration |
| `tools/codegen/cdt-codegen.cpp` | Orchestrator tool (ported from taurus `eosio-codegen.cpp`) |
| `tools/codegen/CMakeLists.txt` | Codegen tool build configuration |

### Modified Files

| File | Changes |
|------|---------|
| `CMakeLists.txt` | Removed `ClangExternalProject.cmake` include, updated `ToolsExternalProject` |
| `cmake/ToolsExternalProject.cmake` | Removed `CDTClang` dependency, uses vcpkg LLVM paths |
| `tools/CMakeLists.txt` | Restructured for plugins + vcpkg LLVM 18, added plugin/codegen subdirectories |
| `tools/cc/cdt-cpp.cpp.in` | Replaced embedded ClangTool abigen/codegen with cdt-codegen subprocess invocation |
| `tools/cc/cdt-cc.cpp.in` | Updated clang version references |
| `tools/include/compiler_options.hpp.in` | Loads sysio_attrs + sysio_codegen plugins, removed LLVM pass loading, added `-add-plugin sysio_codegen` for validation, `-ferror-limit=100` |
| `tools/include/sysio/abimerge.hpp` | Updated for new desc file format |
| `vcpkg.json` | LLVM already present; verified features include clang tools |
| `.gitmodules` | Removed `cdt-llvm` submodule entry |

### Deleted Files

| File | Reason |
|------|--------|
| `cmake/ClangExternalProject.cmake` | No longer building cdt-llvm |
| `cdt-llvm/` (submodule) | Replaced by vcpkg LLVM 18 |

### Modified Test Files

| File | Changes |
|------|---------|
| `tests/toolchain/abigen-fail/empty_contract.json` | Updated test expectations for new error behavior |
| `tests/toolchain/abigen-fail/empty_contract_with_other_contract.json` | Updated test expectations for new error behavior |

---

## LLVM 13 to 18 API Adaptations

| Area | LLVM 13 (taurus) | LLVM 18 | Change Made |
|------|-------------------|---------|-------------|
| `Optional` | `llvm::Optional<T>` | `std::optional<T>` | Replaced throughout |
| `FileManager::getFile()` | Returns `const FileEntry*` | `getOptionalFileRef()` returns `OptionalFileEntryRef` | Updated callers |
| `AnnotateAttr::Create()` | `Create(Ctx, annotation, range)` | `Create(Ctx, annotation, args, nullptr, range)` | Added empty args + nullptr |
| `SourceManager` | `getOrCreateFileID(FileEntry*)` | `getOrCreateFileID(FileEntryRef)` | Dereference OptionalFileEntryRef |
| `ParsedAttrInfo::Spellings` | C-style array | `ArrayRef` with `SpellingPair` | Used `{Syntax, Name}` pairs |
| `SrcMgr::CharacteristicKind` | `SrcMgr::C_User` | `SrcMgr::CharacteristicKind::C_User` | Fully qualified enum |
| Library linking | Many separate `.a` | Monolithic `clang-cpp` + `LLVM` | Updated CMake link targets |
| `starts_with` | Custom implementation | Custom implementation preserved | N/A |

---

## Compilation Pipeline

### Full Link Build (`cdt-cpp source.cpp -o output.wasm`)

```
1. cdt-cpp parses options, derives contract name from output filename
2. cdt-cpp invokes cdt-codegen:
   a. cdt-codegen invokes clang++ -fsyntax-only with sysio_attrs + sysio_codegen plugins
   b. sysio_codegen plugin generates .actions.cpp + populates wasm_actions
   c. sysio_abigen plugin generates .desc file (ABI + wasm data)
   d. cdt-codegen merges ABI from .desc files
   e. cdt-codegen generates .dispatch.cpp (apply() function)
   f. cdt-codegen generates .abi file
3. cdt-cpp compiles .actions.cpp (includes original source + action wrappers)
4. cdt-cpp compiles .dispatch.cpp
5. cdt-cpp links all .o files with wasm-ld
6. sysio-pp post-processes the .wasm file
```

### Compile-Only Build (`cdt-cpp -c source.cpp`)

```
1. cdt-cpp parses options
2. clang++ compiles source.cpp with:
   - sysio_attrs.so loaded (attribute conversion)
   - sysio_codegen.so loaded + activated via -add-plugin (validation only)
   - sysio_codegen runs process_read_only_actions() for error checking
   - No files generated (output is empty)
3. Output: source.o
```

---

## Contract Name Resolution

The contract name flows through the system as follows:

1. **User specifies `--contract=name`**: `has_contract_opt=true`, `abigen_contract=name`
2. **User specifies `-o=name.wasm`**: `has_contract_opt=false`, `abigen_contract` derived from output filename
3. **Neither specified**: `abigen_contract` derived from input filename

In cdt-cpp:
- `--contract` is passed to cdt-codegen when `has_contract_opt || abigen` (strict mode)
- Otherwise, cdt-codegen auto-derives from filename

In cdt-codegen:
- `--contract` sets `explicit_contract=true` (used for "abigen error" reporting)
- Contract name ALWAYS passed to abigen plugin (for correct class filtering)
- Contract name passed to codegen plugin ONLY when `explicit_contract` (auto-detect for dispatch generation)

---

## Testing

### Build Commands

```bash
export CC=/usr/bin/clang-18
export CXX=/usr/bin/clang++-18

cmake -S /data/shared/code/wire/wire-cdt \
  -B /data/shared/code/wire/wire-cdt/build/debug-claude \
  -G Ninja \
  -DCMAKE_PREFIX_PATH="/opt/prefixes/wire-taurus" \
  -DCMAKE_INSTALL_PREFIX=/opt/prefixes/wire-taurus \
  -DENABLE_TESTS=ON \
  -DENABLE_CCACHE=ON \
  -DCMAKE_TOOLCHAIN_FILE=/data/shared/code/wire/wire-cdt/vcpkg/scripts/buildsystems/vcpkg.cmake

cmake --build build/debug-claude
```

### Incremental Rebuild After Plugin/Tool Changes

```bash
# Remove build stamps to force rebuild
rm -f build/debug-claude/CDTTools-prefix/src/CDTTools-stamp/CDTTools-{build,done}
# Add -configure if compiler_options.hpp.in changed
rm -f build/debug-claude/CDTTools-prefix/src/CDTTools-stamp/CDTTools-configure

ninja -C build/debug-claude CDTTools
```

### Running Tests

```bash
cd build/debug-claude && ctest -j$(nproc)          # All tests
cd build/debug-claude && ctest -R toolchain         # Toolchain tests only
```

### Test Results

```
20/20 tests passed (100%)

Unit tests (19):  action_results, asset, binary_extension, crypto, datastream,
                  fixed_bytes, fixed_point, name, rope, print, serialize,
                  string1, string2, symbol, system, time, varint, version (all pass)

Toolchain tests (29 subtests):
  abigen-fail (9/9):     empty_contract (3), empty_contract_with_other_contract (3),
                          wrong_contract_name (3)
  abigen-pass (8/8):     action_results_test, aliased_type_variant_template_arg,
                          nested_container, ricardian_contract_test, singleton_contract,
                          struct_base_typedefd, tagged_number_test, using_std_array
  build-pass (9/9):      bool_template, nestcontn2a, self_referential_table,
                          separate_cpp_hpp, template_gen_regression, tupletest,
                          using_nested_typedef, using_std_tuple, using_std_variant
  compile-fail (2/2):    hf_indirect_call_tests, host_functions_tests
  compile-pass (1/1):    warn_action_read_only
```

---

## Session-by-Session Work Log

### Sessions 1-3: Foundation
- Verified vcpkg LLVM 18 provides Clang development headers
- Created plugin infrastructure (`sysio_attrs.so` + `sysio_codegen.so`)
- Ported from wire-cdt-taurus with eosio-to-sysio renaming and LLVM 13-to-18 API adaptations
- Created `cdt-codegen` orchestrator tool

### Sessions 3-5: Build System & Integration
- Refactored CMake build system to remove cdt-llvm dependency
- Updated `cdt-cpp`, `cdt-cc`, `compiler_options.hpp.in` for new plugin architecture
- Removed `cdt-llvm` git submodule
- Resolved numerous compilation errors (LLVM 18 API changes, missing includes, type mismatches)

### Sessions 5-6: Test Fixes (18/29 to 25/29)
- Fixed contract name derivation and passing through cdt-cpp to cdt-codegen
- Fixed empty contract handling (write desc when contract found but empty, skip dispatch for empty contracts)
- Fixed ricardian contract file lookup (set contract name from discovered class in auto-detect mode)
- Updated test expectations for new error behavior

### Session 7 (Current): Final Test Fixes (25/29 to 29/29)

**Fix 1: Abigen contract name filtering** (singleton_contract_0)
- Root cause: Auto-detect mode picked the first contract class from an included header instead of the target contract
- Fix: Always pass `contract_name` to abigen plugin in cdt-codegen's `gen_actions()`, regardless of `explicit_contract`

**Fix 2: Visitor traversal guard**
- Root cause: Abigen visitor traversed AST even when contract class wasn't found, producing spurious warnings
- Fix: Only call `visitor->TraverseDecl()` inside the `cf.contract_found()` block

**Fix 3: Desc file wasm data preservation** (using_std_tuple_0, using_std_variant_0)
- Root cause: When abigen can't find target contract, it wrote 0-byte desc, losing codegen's `wasm_actions`
- Fix: Added `has_wasm_data()` method; write desc file when wasm_actions/notifies/entries exist

**Fix 4: Codegen plugin validation during compile-only mode** (compile-fail, compile-pass tests)
- Root cause: Codegen plugin only ran via cdt-codegen, not during normal compilation with `-c`
- Fix: Changed `sysio_abigen` from `ReplaceAction` to `AddBeforeMainAction`; load `sysio_codegen.so` during all compilations with `-add-plugin sysio_codegen`; codegen skips file generation when `output` is empty; abigen skips when `output` is empty

**Fix 5: Plugin double-loading prevention**
- Root cause: cdt-codegen inherited `-fplugin` flags from compiler options and added its own
- Fix: Filter out `-fplugin`, `-add-plugin`, and `-plugin-arg` flags in cdt-codegen's `gen_actions()`

**Fix 6: Error limit increase**
- Root cause: Clang's default `-ferror-limit=20` truncated codegen errors, failing regex match expecting 20+ occurrences
- Fix: Set `-ferror-limit=100` in `compiler_options.hpp.in` when codegen plugin is loaded

**Fix 7: warn-action-read-only plugin arg support** (warn_action_read_only_0)
- Root cause: `--warn-action-read-only` flag was never wired through to the codegen plugin
- Fix: Added `warn-action-read-only` plugin arg parsing in `sysio_codegen_frontend_action::ParseArgs()`; pass from `compiler_options.hpp.in` via `-Xclang -plugin-arg-sysio_codegen -Xclang warn-action-read-only`
