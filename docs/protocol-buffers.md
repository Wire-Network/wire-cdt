# Protocol Buffer Support

Wire CDT supports [Protocol Buffers](https://protobuf.dev/) (protobuf) as an alternative serialization format for smart contract action data. This allows contracts to use protobuf's binary wire format instead of the default CDT datastream packing, providing:

- **ID-based field encoding** for stable on-chain data formats
- **Language-neutral** message definitions with extensive library support
- **Backwards compatibility** for schema evolution (add/remove fields without breaking existing data)
- **Compact binary encoding** with fast serialization/deserialization

## How It Works

Instead of using the standard CDT serialization (which packs struct fields sequentially), protobuf actions use [zpp_bits](https://github.com/eyalz800/zpp_bits) — a header-only C++20 library that implements protobuf wire format without linking `libprotobuf`. This is critical because WASM contracts cannot use exceptions, and `zpp_bits` uses `std::errc` return codes for error handling.

The workflow:

1. Define messages in `.proto` files using proto3 syntax
2. `cdt-protoc-gen-zpp` (a custom protoc plugin) generates C++ structs with `zpp_bits` annotations
3. Wrap action parameters in `sysio::pb<T>` to use protobuf serialization
4. The generated ABI includes a `protobuf_types` section with the FileDescriptorSet, enabling tools (clio, SDKs) to serialize/deserialize protobuf action data

## Step 1: Define Proto Messages

Create a `.proto` file with your message definitions:

```protobuf
// mycontract.proto
syntax = "proto3";
package mypackage;

message TransferData {
  string from = 1;
  string to = 2;
  uint64 amount = 3;
  string memo = 4;
}

message TransferResult {
  uint64 balance = 1;
}
```

### Custom Field Options

To use custom field options, add `import "zpp/zpp_options.proto";` at the top of your `.proto` file.

- `[(zpp.zpp_type) = "sysio::name"]` — overrides the generated C++ type for a field

## Step 2: Configure CMake

```cmake
find_package(cdt)

# Create an INTERFACE library for the proto definitions
add_library(my_protos INTERFACE)
target_add_protobuf(my_protos
  OUTPUT_DIRECTORY mypackage
  FILES mycontract.proto
)

# Create the contract and link protobuf definitions
add_contract(mycontract mycontract mycontract.cpp)
contract_use_protobuf(mycontract my_protos)
```

The `target_add_protobuf()` function:
- Runs `cdt-protoc` with the `cdt-protoc-gen-zpp` plugin to generate `.pb.hpp` headers
- Adds the generated headers as sources to the target
- Sets up include directories so `#include <mypackage/mycontract.pb.hpp>` works

The `contract_use_protobuf()` function:
- Links the proto definitions to the contract for ABI generation
- Passes proto file information to `cdt-codegen` so the ABI includes `protobuf_types`

## Step 3: Use in Contract Code

### Single protobuf parameter (flattened)

When an action has a single `sysio::pb<T>` parameter, the ABI points the action type directly at the protobuf type — no wrapper struct is generated. This gives a clean, flat JSON interface:

```cpp
#include <sysio/sysio.hpp>
#include <sysio/pb.hpp>
#include <mypackage/mycontract.pb.hpp>

namespace mypackage {

class [[sysio::contract]] mycontract : public sysio::contract {
public:
   using sysio::contract::contract;

   // Single pb<T> param → action type is "protobuf::mypackage.TransferData"
   [[sysio::action]]
   sysio::pb<TransferResult> transfer(const sysio::pb<TransferData>& data) {
      sysio::check(static_cast<uint64_t>(data.amount) > 0, "amount must be positive");
      // ... transfer logic ...
      TransferResult result;
      result.balance = zpp::bits::vuint64_t(new_balance);
      return result;
   }
};

} // namespace mypackage
```

JSON for pushing this action is flat — protobuf fields at the top level:

```bash
clio push action mycontract transfer \
  '{"from":"alice","to":"bob","amount":1000,"memo":"payment"}' \
  -p alice@active
```

### Multiple protobuf parameters (wrapper struct)

When an action has multiple parameters (protobuf or mixed), a wrapper struct is generated as usual, with each parameter as a named field:

```cpp
   // Multiple params → wrapper struct "settle" with fields "header" and "body"
   [[sysio::action]]
   void settle(const sysio::pb<Header>& header, const sysio::pb<Body>& body) {
      // ...
   }
```

JSON includes the wrapper field names:

```bash
clio push action mycontract settle \
  '{"header":{"version":1},"body":{"items":[...]}}' \
  -p alice@active
```

### Key points

- Use `sysio::pb<T>` to wrap protobuf message types in action parameters and return types
- The ABI generator detects `sysio::pb<T>` and encodes the type as `protobuf::mypackage.TransferData`
- **Single `pb<T>` parameter**: action type points directly at the protobuf type (flat JSON)
- **Multiple parameters**: a wrapper struct is generated (nested JSON)
- Protobuf integer types use `zpp::bits` varint wrappers (`vint64_t`, `vuint64_t`, etc.)
- Varint types don't implicitly convert — use `static_cast<int32_t>(field)` to access the underlying value

## Generated ABI

The generated `.abi` file uses version `sysio::abi/1.3` and includes a `protobuf_types` section containing the FileDescriptorSet in JSON format. Non-protobuf contracts continue to use `sysio::abi/1.2`.

### Single parameter (flattened)

The action type references the protobuf type directly. No wrapper struct is generated:

```json
{
  "version": "sysio::abi/1.3",
  "structs": [],
  "actions": [
    {
      "name": "transfer",
      "type": "protobuf::mypackage.TransferData",
      "ricardian_contract": ""
    }
  ],
  "action_results": [
    { "name": "transfer", "result_type": "protobuf::mypackage.TransferResult" }
  ],
  "protobuf_types": {
    "file": [
      {
        "name": "mycontract.proto",
        "package": "mypackage",
        "messageType": [...]
      }
    ]
  }
}
```

### Multiple parameters (wrapper struct)

A wrapper struct is generated with one field per parameter:

```json
{
  "version": "sysio::abi/1.3",
  "structs": [
    {
      "name": "settle",
      "fields": [
        { "name": "header", "type": "protobuf::mypackage.Header" },
        { "name": "body", "type": "protobuf::mypackage.Body" }
      ]
    }
  ],
  "actions": [
    {
      "name": "settle",
      "type": "settle",
      "ricardian_contract": ""
    }
  ],
  "protobuf_types": { ... }
}
```

## Generated Code

The `cdt-protoc-gen-zpp` plugin generates C++ structs with `zpp_bits` protobuf annotations. Each struct includes a `using serialize` declaration that tells `zpp_bits` to use protobuf wire format:

```cpp
// Generated from mycontract.proto
namespace mypackage {
struct TransferData {
   std::string from = {};
   std::string to = {};
   zpp::bits::vuint64_t amount = {};
   std::string memo = {};
   using serialize = zpp::bits::pb_members<4>;
   bool operator == (const TransferData&) const = default;
};
} // namespace mypackage
```

The `pb_members<N>` declaration (where N is the number of fields) enables protobuf serialization. When fields have non-sequential proto field numbers, the generator uses `zpp::bits::protocol<...>` with explicit field number mappings instead.

## Supported Proto3 Types

| Proto3 Type | C++ Type |
|------------|----------|
| `int32` | `zpp::bits::vint64_t` |
| `int64` | `zpp::bits::vint64_t` |
| `uint32` | `zpp::bits::vuint32_t` |
| `uint64` | `zpp::bits::vuint64_t` |
| `sint32` | `zpp::bits::vsint32_t` |
| `sint64` | `zpp::bits::vsint64_t` |
| `fixed32` | `uint32_t` |
| `fixed64` | `uint64_t` |
| `sfixed32` | `int32_t` |
| `sfixed64` | `int64_t` |
| `float` | `float` |
| `double` | `double` |
| `bool` | `bool` |
| `string` | `std::string` |
| `bytes` | `std::vector<char>` |
| `enum` | C++ `enum : int32_t` |
| `message` | C++ `struct` |
| `repeated T` | `std::vector<T>` |
| `map<K,V>` | `std::map<K,V>` |

## Limitations

- Only proto3 syntax is supported
- `oneof` fields are not supported
- `std::optional` fields (`[(zpp.zpp_optional) = true]`) are not supported — stock `zpp_bits` does not support optional fields in protobuf serialization mode
- Negative enum values are not supported
- Unpacked repeated fields are not supported
- WASM contracts have no exception support; serialization errors abort via `sysio::check()`
