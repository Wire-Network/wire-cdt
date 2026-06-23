# Native Tester and Compilation

Wire CDT supports compiling smart contracts to native host code instead of WebAssembly. This enables:

- **Fast unit testing** without deploying to a blockchain
- **Debugging with standard tools** like `gdb` or `lldb` — set breakpoints, inspect variables, and step through contract code
- **Quick iteration** during development

Native compilation produces an ordinary executable that can be run directly on your machine. Blockchain intrinsics (`require_auth`, `prints`, `db_*`, etc.) are replaced with mockable stubs that you configure per-test.

On macOS, native compilation targets Apple Silicon arm64 only. Intel macOS/x86_64 native compilation is unsupported.

## Getting Started

Given a contract with a header and implementation:

**hello.hpp**
```cpp
#include <sysio/sysio.hpp>
using namespace sysio;

class [[sysio::contract]] hello : public contract {
   public:
      using contract::contract;

      [[sysio::action]]
      void hi( name nm );
      [[sysio::action]]
      void check( name nm );

      using hi_action = action_wrapper<"hi"_n, &hello::hi>;
      using check_action = action_wrapper<"check"_n, &hello::check>;
};
```

**hello.cpp**
```cpp
#include <hello.hpp>

[[sysio::action]]
void hello::hi( name nm ) {
   print_f("Name : %\n", nm);
}

[[sysio::action]]
void hello::check( name nm ) {
   print_f("Name : %\n", nm);
   sysio::check(nm == "hello"_n, "check name not equal to `hello`");
}
```

Create a test file that mocks the intrinsics and exercises the contract actions:

**hello_test.cpp**
```cpp
#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

#include <hello.hpp>

using namespace sysio;
using namespace sysio::native;

// Codegen is skipped for native builds, so provide a dispatcher manually
SYSIO_DISPATCH(hello, (hi)(check))

SYSIO_TEST_BEGIN(hello_test)
   // Mock the read_action_data intrinsic to return "hello"
   intrinsics::set_intrinsic<intrinsics::read_action_data>(
         [](void* m, uint32_t len) {
            check(len <= sizeof(sysio::name), "failed from read_action_data");
            *((sysio::name*)m) = "hello"_n;
            return len;
         });

   intrinsics::set_intrinsic<intrinsics::action_data_size>(
         []() {
            return (uint32_t)sizeof(sysio::name);
         });

   intrinsics::set_intrinsic<intrinsics::require_auth>(
         [](capi_name nm) {
         });

   // Verify the hi action prints the expected output
   CHECK_PRINT("Name : hello\n",
         []() {
            apply("test"_n.value, "test"_n.value, "hi"_n.value);
            });

   // check action should not assert with name "hello"
   apply("test"_n.value, "test"_n.value, "check"_n.value);

   // Change the mock to return "null" instead
   name nm = "null"_n;
   intrinsics::set_intrinsic<intrinsics::read_action_data>(
         [&](void* m, uint32_t len) {
            check(len <= sizeof(sysio::name), "failed from read_action_data");
            *((sysio::name*)m) = nm;
            return len;
         });

   // check action should assert with name "null"
   REQUIRE_ASSERT( "check name not equal to `hello`",
         []() {
            apply("test"_n.value, "test"_n.value, "check"_n.value);
            });

SYSIO_TEST_END

int main(int argc, char** argv) {
   silence_output(true);
   SYSIO_TEST(hello_test);
   return has_failed();
}
```

## Compiling Native Code

### Command Line

Use `cdt-cpp` with the `-fnative` flag to compile to native host code instead of WebAssembly:

```bash
cdt-cpp -fnative -o hello_test hello_test.cpp hello.cpp -I./include
```

The `-Wno-unknown-attributes` flag is added automatically for native builds, so WASM-specific attributes like `[[sysio::action]]` and `__attribute__((sysio_wasm_import))` do not produce warnings.

To compile with debug symbols for use with `gdb` or `lldb`:

```bash
cdt-cpp -fnative -g -o hello_test hello_test.cpp hello.cpp -I./include
```

### CMake

Wire CDT provides CMake macros for native compilation. These are available after `find_package(cdt)` when using the CDT WASM toolchain.

**`add_native_executable`** — Build a native test executable:

```cmake
project(hello_tests)
find_package(cdt)

add_native_executable(hello_test hello_test.cpp ../src/hello.cpp)
target_include_directories(hello_test PUBLIC ../include)
```

**`add_native_library`** — Build a native library:

```cmake
add_native_library(my_lib src/util.cpp)
```

**`add_contract_native`** — Build a contract as a native executable (for use alongside `add_contract` in contract projects):

```cmake
add_contract(mycontract mycontract src/mycontract.cpp)

# Optionally build a native version for debugging
add_contract_native(mycontract mycontract_native src/mycontract.cpp src/tests.cpp)
target_include_directories(mycontract_native PUBLIC include)
```

All native macros automatically suppress unknown-attribute warnings.

## Running Tests

Run the compiled native executable directly:

```bash
./hello_test
```

Example output:
```
hello_test unit test passed
```

### Debugging with GDB

```bash
gdb ./hello_test
(gdb) break hello::hi
(gdb) run
```

## Intrinsics

All blockchain intrinsics are mockable through the `intrinsics::set_intrinsic` / `intrinsics::get_intrinsic` API. The native runtime pre-registers default implementations for `prints_l`, `prints`, `printi`, `printui`, and other print functions. All other intrinsics (e.g., `require_auth`, `read_action_data`, `db_*`) must be mocked by your test before calling contract code that uses them.

```cpp
// Set a mock intrinsic
intrinsics::set_intrinsic<intrinsics::require_auth>(
      [](capi_name nm) {
         // custom validation or no-op
      });

// Retrieve the current intrinsic implementation
auto current = intrinsics::get_intrinsic<intrinsics::require_auth>();
```

## Tester API Reference

### Test Structure

| Macro | Description |
|-------|-------------|
| `SYSIO_TEST_BEGIN(name)` | Start a unit test function named `name` |
| `SYSIO_TEST_END` | End a unit test function |
| `SYSIO_TEST(name)` | Execute the named unit test in `main()` |
| `SYSIO_DISPATCH(contract, (action1)(action2)...)` | Generate the `apply()` dispatcher for native builds |

### Assertions

| Macro | On Failure | Description |
|-------|-----------|-------------|
| `CHECK_ASSERT(expected, func)` | Continues | Verify `func` triggers an assert with the expected message |
| `CHECK_PRINT(expected, func)` | Continues | Verify `func` produces the expected print output |
| `CHECK_EQUAL(x, y)` | Continues | Verify `x == y` |
| `REQUIRE_ASSERT(expected, func)` | Halts test | Verify `func` triggers an assert with the expected message |
| `REQUIRE_PRINT(expected, func)` | Halts test | Verify `func` produces the expected print output |
| `REQUIRE_EQUAL(x, y)` | Halts test | Verify `x == y` |

`CHECK_*` macros record the failure and continue running the test. `REQUIRE_*` macros halt the test immediately on failure.

Both `CHECK_ASSERT` / `REQUIRE_ASSERT` and `CHECK_PRINT` / `REQUIRE_PRINT` accept either a string literal for exact matching or a lambda predicate for custom matching:

```cpp
// Exact match
CHECK_PRINT("Name : hello\n", []() { apply(...); });

// Custom predicate
CHECK_PRINT([](const std::string& output) {
   return output.find("hello") != std::string::npos;
}, []() { apply(...); });
```

### Utility Functions

| Function | Description |
|----------|-------------|
| `silence_output(bool)` | Enable/disable printing to stdout during tests |
| `has_failed()` | Returns `true` if any `CHECK_*` assertion has failed |

## Working Example

A complete working example is included in the repository at `examples/hello/`. To build and run it:

```bash
mkdir build && cd build
cmake ../examples/hello -DCMAKE_TOOLCHAIN_FILE=<CDT_ROOT>/lib/cmake/cdt/CDTWasmToolchain.cmake
make
./tests/hello_test
```

Where `<CDT_ROOT>` is the Wire CDT install or build prefix.
