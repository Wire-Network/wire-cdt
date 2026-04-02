# sysio::kv::raw\_table

## Include

```cpp
#include <sysio/kv_raw_table.hpp>
```

## Overview

`sysio::kv::raw_table<K, V>` provides an ordered key-value store with custom key types, similar to RocksDB or LevelDB. It uses format=0 raw byte keys -- no scope or table-name prefix overhead.

Key properties:

- **Custom key types**: keys can be any struct with `SYSLIB_SERIALIZE`
- **Big-endian key encoding**: keys are BE-encoded via `be_key_stream` for correct `memcmp` ordering
- **Ordered iteration**: `begin`/`end`, `lower_bound`/`upper_bound`
- **No secondary indices** -- use `kv::indexed_table` if you need secondary lookups with format=0 keys
- **No scope** -- all entries share a flat namespace (see warning below)
- **Standard ABI values**: values use LE serialization, decodable by SHiP clients via `abieos`
- **Zero-copy** for `trivially_copyable` value types: values are stored/loaded via `memcpy` instead of datastream serialization when `sizeof(V) == pack_size(V)` (no struct padding)
- Compact keys reduce per-row RAM billing (key bytes are billed directly)
- Optional `payer` parameter on `set()` (defaults to receiver)

## Flat namespace warning

All `raw_table` instances on the same contract share a **single flat namespace** (all format=0 entries). If your contract uses multiple `raw_table` instances, add a discriminator field to your key struct:

```cpp
struct user_key {
   uint8_t  tag = 1;   // unique per raw_table instance
   uint64_t id;
   SYSLIB_SERIALIZE(user_key, (tag)(id))
};
```

Format=0 entries are stored separately from format=1 entries (used by `multi_index` and `kv::table`), so there is no collision between `raw_table` and those APIs.

## Constructor

```cpp
kv::raw_table<my_key, my_val> store;                  // reads own contract data
kv::raw_table<my_key, my_val> other("othercon"_n);    // reads another contract's data (read-only)
```

## API reference

| Method | Description |
|--------|-------------|
| `set(key, value)` | Store or update a key-value pair (receiver pays) |
| `set(key, value, payer)` | Store or update with explicit payer |
| `get(key)` | Returns `std::optional<V>`, empty if not found |
| `contains(key)` | Returns `bool` |
| `erase(key)` | Delete a key-value pair, returns RAM delta |
| `begin()` / `end()` | Forward iteration over all entries |
| `lower_bound(key)` | First entry with key >= given key |
| `upper_bound(key)` | First entry with key > given key |

Iterators are move-only and bidirectional (`++it` / `--it`). Post-increment (`it++`) and post-decrement (`it--`) are deleted to avoid hidden copy overhead.

## Key encoding

Keys are encoded in big-endian byte order so that `memcmp`-based comparison in the underlying store produces correct lexicographic ordering:

| Type | Encoding | Size |
|------|----------|------|
| `uint8` | Raw byte | 1 |
| `int8` | XOR sign bit | 1 |
| `uint16` / `int16` | BE (int: XOR sign bit) | 2 |
| `uint32` / `int32` | BE (int: XOR sign bit) | 4 |
| `uint64` / `int64` / `name` | BE (int: XOR sign bit) | 8 |
| `uint128` / `int128` | BE (int: XOR sign bit) | 16 |
| `float` | IEEE 754, sign-magnitude to unsigned sortable | 4 |
| `double` | IEEE 754, sign-magnitude to unsigned sortable | 8 |
| `bool` | 0 or 1 | 1 |
| `string` / `vector<char>` | NUL-escape encoding + `0x00 0x00` terminator | variable |

Composite key structs are encoded by concatenating each field in `SYSLIB_SERIALIZE` declaration order. The sign-bit XOR for signed integers ensures that negative values sort before positive values in byte comparison.

**NUL-escape encoding** for variable-length fields: `0x00` bytes in the data are escaped as `0x00 0x01`, and the field is terminated by `0x00 0x00`. This preserves lexicographic sort order for arbitrary byte sequences including embedded null bytes.

## ABI key metadata

Annotate the value struct with `[[sysio::table("name"), sysio::kv_key("key_struct")]]` so SHiP clients can decode raw format=0 keys. CDT extracts all fields from the key struct in `SYSLIB_SERIALIZE` declaration order, matching the BE encoding order:

```cpp
struct my_key {
   std::string region;
   uint64_t    id;
   SYSLIB_SERIALIZE(my_key, (region)(id))
};

struct [[sysio::table("geodata"), sysio::kv_key("my_key")]] my_value {
   std::string payload;
   uint64_t    amount;
   SYSLIB_SERIALIZE(my_value, (payload)(amount))
};
```

This generates an ABI table entry with key field metadata:

```json
{
   "name": "geodata",
   "type": "my_value",
   "key_names": ["region", "id"],
   "key_types": ["string", "uint64"]
}
```

The `my_key` struct is also added to the ABI's `structs` section. Clients decode the raw key bytes field by field using the BE encoding rules in the table above. Without the annotation, the table functions correctly but SHiP clients won't have key field metadata.

## Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_raw_table.hpp>

using namespace sysio;

class [[sysio::contract]] inventory : public contract {
public:
   using contract::contract;

   struct my_key {
      std::string region;
      uint64_t    id;
      SYSLIB_SERIALIZE(my_key, (region)(id))
   };

   struct [[sysio::table("items"), sysio::kv_key("my_key")]] item {
      std::string name;
      uint64_t    quantity;
      SYSLIB_SERIALIZE(item, (name)(quantity))
   };

   kv::raw_table<my_key, item> items;

   [[sysio::action]]
   void additem(std::string region, uint64_t id, std::string name, uint64_t qty) {
      items.set({region, id}, {name, qty});
   }

   [[sysio::action]]
   void getitem(std::string region, uint64_t id) {
      auto val = items.get({region, id});
      check(val.has_value(), "item not found");
      print(val->name, ": ", val->quantity);
   }

   [[sysio::action]]
   void listitems() {
      for (auto it = items.begin(); it != items.end(); ++it) {
         print((*it).name, ": ", (*it).quantity, "\n");
      }
   }
};
```
