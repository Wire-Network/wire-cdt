# sysio::kv::indexed\_table

## Include

```cpp
#include <sysio/kv_indexed_table.hpp>
```

## Overview

`sysio::kv::indexed_table<TableName, K, V, Indices...>` combines the compact format=0 keys of `kv::raw_table` with `multi_index`-style secondary indices. Designed for new contracts that don't need legacy scope overhead.

Key properties:

- **Format=0 raw byte keys** -- no 16-byte table+scope prefix. Keys are as compact as the struct allows
- **Custom key types** (`K`) and value types (`V`) -- both are separate template parameters
- **Up to 16 secondary indices** via `kv::index<Name, Extractor>`
- **Two extractor styles**: `kv::member_data` (data member pointer) and `const_mem_fun` (member function)
- **Explicit mutation API**: `emplace`/`modify`/`erase` -- no raw `set()` (prevents secondary index corruption)
- **Optional payer**: overloaded signatures with payer-first or defaulting to receiver
- **Key-only secondary iteration**: `key_begin()`/`key_end()` for scanning without loading values
- **Zero-copy** for `trivially_copyable` value types: values are stored/loaded via `memcpy` instead of datastream serialization when `sizeof(V) == pack_size(V)` (no struct padding)
- Iterators yield `row { K key; V value; }` (both primary and secondary)
- Move-only iterators (no expensive handle cloning)
- Big-endian key encoding via `be_key_stream` for correct sort order
- `be_key_reader` for decoding keys from raw bytes (used internally by iterators)

## Flat namespace warning

`TableName` scopes **secondary index entries only** (via the `kv_idx_*` intrinsics' `table` parameter). It does **not** namespace primary keys.

All format=0 primary keys share a single flat namespace within a contract. If your contract uses multiple `indexed_table` or `raw_table` instances, add a discriminator field to your key struct to prevent collisions:

```cpp
struct user_key {
   uint8_t  tag = 1;   // unique per table instance
   uint64_t id;
   SYSLIB_SERIALIZE(user_key, (tag)(id))
};
```

## Template parameters

```cpp
template<name::raw TableName, typename K, typename V, typename... Indices>
class indexed_table;
```

| Parameter | Description |
|-----------|-------------|
| `TableName` | `sysio::name` constant. Scopes secondary index entries only |
| `K` | Key struct. Must have `SYSLIB_SERIALIZE`. BE-encoded for storage and ordering |
| `V` | Value struct. Must have `SYSLIB_SERIALIZE`. ABI-serialized (LE) for SHiP compatibility |
| `Indices...` | Zero or more `kv::index<Name, Extractor>` declarations (max 16) |

## Index declaration

Two extractor styles are supported:

```cpp
// Pointer to data member -- for direct field access
kv::index<"byowner"_n, kv::member_data<my_val, name, &my_val::owner>>

// const_mem_fun -- for computed keys (member function)
kv::index<"bybal"_n, const_mem_fun<my_val, uint64_t, &my_val::get_balance>>
```

`kv::member_data<Class, Type, &Class::field>` extracts a data member directly.

`sysio::const_mem_fun<Class, Type, &Class::method>` calls a const member function. The function can compute any value, including lookups in other tables (but see note on consistency below).

## Constructor

```cpp
kv::indexed_table<"mytbl"_n, my_key, my_val, ...> tbl;              // own contract
kv::indexed_table<"mytbl"_n, my_key, my_val, ...> tbl("other"_n);   // read another contract
```

## API reference

### Point operations

| Method | Description |
|--------|-------------|
| `find(key)` | Returns primary iterator, or `end()` if not found |
| `require_find(key, msg)` | find() + assert |
| `get(key)` | Returns `std::optional<V>`, empty if not found |
| `contains(key)` | Returns `bool` |

### Primary iteration

| Method | Description |
|--------|-------------|
| `begin()` / `end()` | Forward iteration over all entries in key order |
| `lower_bound(key)` | First entry with key >= given key |
| `upper_bound(key)` | First entry with key > given key |

### Mutation

All mutations automatically maintain secondary index consistency.

```cpp
// Emplace -- create a new row
tbl.emplace(key, value);                  // receiver pays
tbl.emplace(payer, key, value);           // explicit payer

// Modify -- update an existing row (via primary iterator)
tbl.modify(it, new_value);               // receiver pays
tbl.modify(payer, it, new_value);        // explicit payer

// Erase -- delete a row, returns next iterator
auto next = tbl.erase(std::move(it));
```

`modify` takes the new value directly (not a lambda). The old value is read from the iterator's cache to compute secondary key deltas.

**Important:** `emplace` must only be called for keys that do not already exist. Calling `emplace` on an existing key corrupts secondary indexes — the primary value is overwritten but old secondary index entries are orphaned (see below). Use `upsert()` if the key may already exist, or `modify()` when you have an iterator.

#### `upsert` — safe insert-or-update

`upsert(key, value)` checks whether the key exists. If new, it inserts (like `emplace`). If existing, it reads the old value and updates with correct secondary index maintenance (like `modify`). This costs one extra `kv_get` on the insert path but is safe for both cases:

```cpp
tbl.upsert({1}, {1000, "alice"_n});   // inserts (key is new)
tbl.upsert({1}, {2000, "bob"_n});     // updates (key exists, secondaries updated correctly)
```

#### Why `emplace` on existing keys is dangerous

When `emplace` is called on a key that already has a row:
1. `kv_set` overwrites the primary value
2. `kv_idx_store` adds **new** secondary entries for the new value
3. The **old** secondary entries remain in chainbase — they are never removed

These orphaned entries cause wrong results: a secondary lookup for the old value (e.g., the previous owner) still returns this primary key, but the actual data no longer matches. Orphans survive `erase` — erasing the row only removes secondary entries for the *current* value, not the stale ones. The orphaned entries are permanent and unrecoverable without knowing the old secondary key bytes.

### Secondary index access

```cpp
auto idx = tbl.get_index<"byowner"_n>();
```

The returned `secondary_index_view` supports:

| Method | Description |
|--------|-------------|
| `find(sec_key)` | Exact match on secondary key |
| `lower_bound(sec_key)` | First entry >= sec\_key |
| `upper_bound(sec_key)` | First entry > sec\_key |
| `require_find(sec_key, msg)` | find() + assert |
| `begin()` / `end()` | Full range in secondary key order |
| `modify(itr, new_value)` | Modify via secondary iterator (receiver pays) |
| `modify(payer, itr, new_value)` | Modify with explicit payer |
| `erase(itr)` | Erase via secondary iterator, returns next |
| `key_begin()` / `key_end()` | Key-only iteration (no value loading) |

### Key-only iteration

`key_begin()` / `key_end()` return a `key_iterator` that yields `key_row { K key; SecKey sec_key; }` without calling `kv_get` to load the value. Use this for scanning secondary indices when you only need the keys:

```cpp
auto idx = tbl.get_index<"bybal"_n>();
std::vector<my_key> high_balance_keys;
for (auto it = idx.key_begin(); it != idx.key_end(); ++it) {
   if (it->sec_key >= 1000)
      high_balance_keys.push_back(it->key);
}
```

## Iterator behavior

- **Move-only**: iterators cannot be copied (avoids expensive handle cloning). Post-increment/decrement are deleted -- use `++it` / `--it`
- **Dereference returns `row`**: `it->key` and `it->value` for both primary and secondary iterators
- **Bidirectional**: `--end()` gives the last element; `--it` steps backward
- **Invalidated after mutation**: calling `modify` through a secondary iterator invalidates it — the cached value is stale, and if the secondary key changed, the iterator's position in the index is also invalid (the underlying entry was moved by `kv_idx_update`). Do not dereference or advance the iterator after `modify`; re-find instead. `erase` consumes the iterator and returns the next one, so this does not apply to erase

## Extractor consistency note

`const_mem_fun` extractors can call any function, including lookups in other tables. However, secondary index keys are stored at `emplace` time and updated at `modify` time using the extractor. If the extractor depends on external state that changes independently, the secondary index will become stale. Only use extractors that are **pure functions of `V`'s fields** unless you manage consistency manually.

## Comparison with other table types

| Feature | `indexed_table` | `multi_index` | `raw_table` | `kv::table` |
|---------|----------------|---------------|-------------|-------------|
| Key format | Format=0 (compact) | Format=1 (24B) | Format=0 (compact) | Format=1 (24B) |
| Scope | No | Yes | No | Yes |
| Secondary indices | Yes (up to 16) | Yes (up to 16) | No | No |
| Key type | Custom struct | `uint64_t` | Custom struct | `uint64_t` |
| Value type | Separate from key | Unified row | Separate from key | Unified row |
| Mutation API | emplace/modify/erase | emplace/modify/erase | set/erase | emplace/modify/erase |
| Payer | Optional (overloads) | Required (first arg) | Optional (default arg) | Required |
| Object caching | No (each dereference reads from KV storage) | Yes | No | No |
| Key-only sec. iteration | Yes | No | N/A | N/A |
| Move-only iterators | Yes | No (copyable) | Yes | Yes |

## ABI key metadata

To declare the key type in the ABI (so SHiP clients and block explorers can decode raw format=0 keys), annotate the value struct with `[[sysio::table("name"), sysio::kv_key("key_struct")]]`:

```cpp
struct asset_key {
   uint64_t id;
   SYSLIB_SERIALIZE(asset_key, (id))
};

struct [[sysio::table("assets"), sysio::kv_key("asset_key")]] asset_val {
   name     owner;
   uint64_t value;
   SYSLIB_SERIALIZE(asset_val, (owner)(value))
};

using assets = kv::indexed_table<"assets"_n, asset_key, asset_val,
   kv::index<"byowner"_n, kv::member_data<asset_val, name, &asset_val::owner>>
>;
```

This generates an ABI table entry with key field metadata:

```json
{
   "name": "assets",
   "type": "asset_val",
   "key_names": ["id"],
   "key_types": ["uint64"]
}
```

### Multi-field key structs

The annotation extracts **all fields** from the key struct, in `SYSLIB_SERIALIZE` declaration order. This matches the BE encoding order, so clients can decode key bytes field by field:

```cpp
struct order_key {
   uint8_t     tag;
   std::string market;
   uint64_t    seq;
   SYSLIB_SERIALIZE(order_key, (tag)(market)(seq))
};

struct [[sysio::table("orders"), sysio::kv_key("order_key")]] order_val {
   name     trader;
   uint64_t amount;
   SYSLIB_SERIALIZE(order_val, (trader)(amount))
};
```

Generates:

```json
{
   "name": "orders",
   "type": "order_val",
   "key_names": ["tag", "market", "seq"],
   "key_types": ["uint8", "string", "uint64"]
}
```

The key struct is also added to the ABI's `structs` section with full field definitions. Clients decode the raw key bytes using the BE encoding rules (big-endian integers, sign-bit XOR for signed types, NUL-escape for strings -- see [kv-raw-table.md](kv-raw-table.md#key-encoding) for the full encoding table).

The `[[sysio::table]]` name should match the `TableName` template parameter for consistency, though the chain does not enforce this. Without the annotation, the table functions correctly but SHiP clients won't have key field metadata to decode raw key bytes.

## Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_indexed_table.hpp>

using namespace sysio;

class [[sysio::contract]] registry : public contract {
public:
   using contract::contract;

   struct asset_key {
      uint64_t id;
      SYSLIB_SERIALIZE(asset_key, (id))
   };

   struct asset_val {
      name     owner;
      uint64_t value;
      name get_owner() const { return owner; }
      SYSLIB_SERIALIZE(asset_val, (owner)(value))
   };

   using assets = kv::indexed_table<"assets"_n, asset_key, asset_val,
      kv::index<"byowner"_n, kv::member_data<asset_val, name, &asset_val::owner>>
   >;

   assets tbl;

   [[sysio::action]]
   void create(uint64_t id, name owner, uint64_t value) {
      check(!tbl.contains({id}), "asset already exists");
      tbl.emplace({id}, {owner, value});
   }

   [[sysio::action]]
   void transfer(uint64_t id, name new_owner) {
      auto it = tbl.require_find({id}, "asset not found");
      require_auth(it->value.owner);
      tbl.modify(it, {new_owner, it->value.value});
   }

   [[sysio::action]]
   void listbyowner(name owner) {
      auto idx = tbl.get_index<"byowner"_n>();
      for (auto it = idx.lower_bound(owner); it != idx.end(); ++it) {
         if (it->value.owner != owner) break;
         print("id=", it->key.id, " value=", it->value.value, "\n");
      }
   }

   [[sysio::action]]
   void ownerkeys(name owner) {
      // Key-only scan -- no value deserialization
      auto idx = tbl.get_index<"byowner"_n>();
      for (auto it = idx.key_begin(); it != idx.key_end(); ++it) {
         if (it->sec_key != owner) continue;
         print("id=", it->key.id, "\n");
      }
   }
};
```

## Example: multiple tables with tag discriminator

All `indexed_table` and `raw_table` instances on the same contract share a flat format=0 key namespace. Use a `uint8_t tag` field in the key struct to partition them. The tag costs only 1 byte per key and is checked at the BE-encoding level, so different tables never collide.

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_indexed_table.hpp>

using namespace sysio;

class [[sysio::contract]] marketplace : public contract {
public:
   using contract::contract;

   // ── Table 1: Users (tag=1) ───────────────────────────────────────────

   struct user_key {
      uint8_t  tag = 1;
      uint64_t id;
      SYSLIB_SERIALIZE(user_key, (tag)(id))
   };

   struct user_val {
      name     account;
      uint64_t reputation;
      name get_account() const { return account; }
      SYSLIB_SERIALIZE(user_val, (account)(reputation))
   };

   using users = kv::indexed_table<"users"_n, user_key, user_val,
      kv::index<"byacct"_n, const_mem_fun<user_val, name, &user_val::get_account>>
   >;

   // ── Table 2: Listings (tag=2) ────────────────────────────────────────

   struct listing_key {
      uint8_t  tag = 2;
      uint64_t id;
      SYSLIB_SERIALIZE(listing_key, (tag)(id))
   };

   struct listing_val {
      uint64_t seller_id;
      uint64_t price;
      std::string title;
      uint64_t get_price() const { return price; }
      SYSLIB_SERIALIZE(listing_val, (seller_id)(price)(title))
   };

   using listings = kv::indexed_table<"listings"_n, listing_key, listing_val,
      kv::index<"byprice"_n, const_mem_fun<listing_val, uint64_t, &listing_val::get_price>>
   >;

   // ── Both tables coexist safely ───────────────────────────────────────

   users    user_tbl;
   listings listing_tbl;

   [[sysio::action]]
   void adduser(uint64_t id, name account) {
      user_tbl.emplace({1, id}, {account, 0});
   }

   [[sysio::action]]
   void addlisting(uint64_t id, uint64_t seller, uint64_t price, std::string title) {
      listing_tbl.emplace({2, id}, {seller, price, title});
   }

   [[sysio::action]]
   void cheapest(uint32_t limit) {
      auto idx = listing_tbl.get_index<"byprice"_n>();
      uint32_t count = 0;
      for (auto it = idx.begin(); it != idx.end() && count < limit; ++it, ++count) {
         print(it->value.title, " - ", it->value.price, "\n");
      }
   }
};
```

In this example, user keys are encoded as `[0x01][id:8B]` (9 bytes) and listing keys as `[0x02][id:8B]` (9 bytes). The tag byte ensures they never collide in the flat format=0 namespace, even though both tables use the same contract. Keeping keys small (single-digit bytes) minimizes per-row RAM billing since key bytes are billed directly.
