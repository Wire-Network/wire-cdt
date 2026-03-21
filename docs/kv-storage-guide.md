# Wire KV Storage Guide

Wire uses a key-value database as the storage backend for smart contract state, replacing the EOSIO `db_*_i64` host functions with a simpler set of KV intrinsics. Two C++ APIs are available to contract developers:

- **`sysio::multi_index`** -- Full-featured table with secondary indices, reverse iteration, object caching, and singletons. Recommended for most contracts. API-compatible with EOSIO `multi_index`.
- **`sysio::kv::table`** -- High-performance, zero-copy table for simple CRUD workloads. No secondary index support, but ~15% faster for trivially copyable structs.

Both APIs use the same underlying KV host functions (intrinsics). Existing contracts compile unchanged with the new CDT -- no source code modifications required.

---

## sysio::multi\_index (recommended for most contracts)

### Include

```cpp
#include <sysio/multi_index.hpp>   // explicit
// or automatically via:
#include <sysio/sysio.hpp>
```

### Overview

The API surface is identical to the EOSIO `multi_index`. Under the hood, primary rows are stored as 24-byte KV keys and secondary indices use the `kv_idx_*` intrinsics, but from the contract author's perspective the interface is the same.

Key properties:

- Up to **16 secondary indices** via `indexed_by` / `const_mem_fun`
- Full bidirectional iterator support: `begin`/`end`, `rbegin`/`rend`, `cbegin`/`cend`
- `find`, `require_find`, `get`, `lower_bound`, `upper_bound`, `iterator_to`
- `emplace`, `modify`, `erase` (erase returns next iterator)
- `available_primary_key()` for auto-increment
- Object caching: repeated access to the same primary key returns the cached pointer
- `payer` parameter is accepted for API compatibility but ignored -- RAM is charged to the contract account
- Row types must use `SYSLIB_SERIALIZE`

### Singleton support

```cpp
#include <sysio/singleton.hpp>
```

`sysio::singleton<Name, T>` is now an alias for `sysio::kv_singleton`, backed by the KV database. The API is unchanged:

| Method | Description |
|--------|-------------|
| `exists()` | Returns true if a value has been stored |
| `get()` | Returns stored value, asserts if missing |
| `get_or_default(def)` | Returns stored value or `def` |
| `get_or_create(payer, def)` | Returns stored value, or stores and returns `def` |
| `set(value, payer)` | Stores or updates the value |
| `remove()` | Deletes the stored value |

### Secondary index views

Access a secondary index with `get_index<"indexname"_n>()`. The returned view supports:

| Method | Description |
|--------|-------------|
| `find(sec_key)` | Exact match on secondary key |
| `lower_bound(sec_key)` | First entry >= sec\_key |
| `require_find(sec_key, msg)` | find() + assert |
| `begin()` / `end()` | Full range in secondary key order |
| `rbegin()` / `rend()` | Reverse iteration |
| `modify(itr, payer, updater)` | Modify the primary row via secondary iterator |
| `erase(itr)` | Erase the primary row, returns next secondary iterator |

Supported secondary key types: `uint64_t`, `uint128_t`, `double`, `long double`, and any serializable type.

### Example: token-like contract with secondary index

```cpp
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] mytoken : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] account {
      name     owner;
      uint64_t balance;

      uint64_t primary_key() const { return owner.value; }
      uint64_t by_balance()  const { return balance; }

      SYSLIB_SERIALIZE(account, (owner)(balance))
   };

   using accounts_table = multi_index<"accounts"_n, account,
      indexed_by<"bybalance"_n, const_mem_fun<account, uint64_t, &account::by_balance>>
   >;

   [[sysio::action]]
   void transfer(name from, name to, uint64_t amount) {
      require_auth(from);

      accounts_table accts(get_self(), get_self().value);

      // Debit sender
      auto from_itr = accts.require_find(from.value, "sender not found");
      check(from_itr->balance >= amount, "insufficient balance");
      accts.modify(from_itr, same_payer, [&](auto& a) {
         a.balance -= amount;
      });

      // Credit receiver
      auto to_itr = accts.find(to.value);
      if (to_itr == accts.end()) {
         accts.emplace(get_self(), [&](auto& a) {
            a.owner = to;
            a.balance = amount;
         });
      } else {
         accts.modify(to_itr, same_payer, [&](auto& a) {
            a.balance += amount;
         });
      }
   }

   [[sysio::action]]
   void topbalances(uint32_t limit) {
      accounts_table accts(get_self(), get_self().value);
      auto idx = accts.get_index<"bybalance"_n>();

      // Iterate in reverse (highest balance first)
      uint32_t count = 0;
      for (auto it = idx.rbegin(); it != idx.rend() && count < limit; ++it, ++count) {
         print(it->owner, ": ", it->balance, "\n");
      }
   }
};
```

---

## sysio::kv::table (for performance-critical contracts)

### Include

```cpp
#include <sysio/kv_table.hpp>
```

### Overview

`sysio::kv::table<TableName, T>` provides a simpler, lower-overhead interface for contracts that do not need secondary indices. Key properties:

- **Zero-copy** for `trivially_copyable` structs: values are stored/loaded via `memcpy` instead of datastream serialization, provided `sizeof(T) == pack_size(T)` (no struct padding)
- Falls back to datastream serialization for complex types (vectors, strings, nested structs)
- No secondary index support
- No object caching -- each `find`/`get` call reads from storage
- Bidirectional iterators: `begin`/`end`, `lower_bound`/`upper_bound`
- Cross-scope iteration via `begin_all_scopes()` / `end_all_scopes()` (not possible with multi\_index)
- Row type must have a `uint64_t primary_key() const` method

### API reference

| Method | Description |
|--------|-------------|
| `find(pk)` | Returns iterator, or `end()` if not found |
| `require_find(pk, msg)` | find() + assert |
| `get(pk, msg)` | Returns `const T&`, asserts if missing |
| `contains(pk)` | Returns `bool`, single intrinsic call |
| `lower_bound(pk)` | First entry with key >= pk |
| `upper_bound(pk)` | First entry with key > pk |
| `begin()` / `end()` | Forward iteration over all rows in scope |
| `set(pk, obj)` | Store directly by primary key |
| `emplace(payer, constructor)` | Construct and store a new row |
| `modify(itr, payer, updater)` | Update an existing row |
| `modify(obj, payer, updater)` | Update via object reference |
| `erase(pk)` / `erase(obj)` / `erase(itr)` | Delete a row |
| `available_primary_key()` | Next auto-increment key |
| `begin_all_scopes()` | Iterate ALL rows across ALL scopes (returns `scoped_row` with scope, primary\_key, obj) |

### Example: high-performance account balance table

```cpp
#include <sysio/kv_table.hpp>
#include <sysio/sysio.hpp>

using namespace sysio;

struct balance_row {
   uint64_t account;  // name as uint64_t
   uint64_t amount;

   uint64_t primary_key() const { return account; }

   // trivially_copyable + no padding = zero-copy path
   SYSLIB_SERIALIZE(balance_row, (account)(amount))
};

using balance_table = kv::table<"balances"_n, balance_row>;

class [[sysio::contract]] fasttoken : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void transfer(name from, name to, uint64_t amount) {
      require_auth(from);
      balance_table bal(get_self(), get_self().value);

      // Debit
      auto sender = bal.get(from.value, "sender not found");
      check(sender.amount >= amount, "insufficient balance");
      balance_row updated_sender = sender;
      updated_sender.amount -= amount;
      bal.set(from.value, updated_sender);

      // Credit
      balance_row receiver;
      if (bal.contains(to.value)) {
         receiver = bal.get(to.value);
         receiver.amount += amount;
      } else {
         receiver.account = to.value;
         receiver.amount = amount;
      }
      bal.set(to.value, receiver);
   }
};
```

---

## Key Encoding

Both APIs encode primary keys as 24 bytes:

```
[table_name: 8 bytes, big-endian] [scope: 8 bytes, big-endian] [primary_key: 8 bytes, big-endian]
```

This encoding has several desirable properties:

- **SSO (Small String Optimization)**: keys <= 24 bytes are stored inline in the `kv_object` without heap allocation
- **Integer fast-path**: 8-byte big-endian keys can be compared with a single `bswap64` instruction
- **Lexicographic ordering**: big-endian encoding preserves numeric sort order in byte comparison, enabling prefix-scoped iteration
- **SHiP compatible**: keys can be reverse-mapped to the legacy `contract_row` format (table, scope, primary\_key) for State History Plugin consumers

Secondary index keys are encoded using `kv_idx_*` intrinsics with a composite primary key of `[scope:8B][pk:8B]` to ensure uniqueness across scopes.

---

## Performance Comparison

| Scenario | multi\_index | kv::table (zero-copy) | kv::table (complex) |
|----------|-------------|----------------------|---------------------|
| Simple row read | Baseline | ~15% faster | ~same |
| Simple row write | Baseline | ~15% faster | ~same |
| With secondary indices | Supported | N/A | N/A |
| OC runtime (token transfer) | ~0.5 us | ~0.5 us | ~0.5 us |
| Serialization overhead | Always serializes | memcpy for POD | Serializes |

The zero-copy path in `kv::table` skips datastream serialization entirely for structs where `std::is_trivially_copyable<T>` is true and `sizeof(T) == pack_size(T)` (no padding bytes). For complex types with vectors, strings, or optional fields, both APIs have equivalent performance since both use datastream serialization.

At the OC (Optimized Compiler) runtime tier, intrinsic call overhead dominates and both APIs perform at near-parity with the EOSIO `db_*_i64` implementation.

---

## Performance Best Practices

### What's Fast

- **Point lookups by primary key** (`find(pk)`, `get(pk)`) -- single KV read, O(1) with SSO key
- **kv::table with trivially\_copyable structs** -- zero-copy (memcpy, no pack/unpack)
- **Emplace/modify with same value size** -- no reallocation
- **Iterating forward** (`begin()` to `end()`, `++`) -- sequential access pattern, cache-friendly
- **SSO keys (<=24 bytes)** -- no heap allocation, inline in chainbase object
- **8-byte integer keys** -- single `bswap64` comparison instruction
- **Scope-based partitioning** -- each scope has its own key prefix, avoids scanning unrelated data

### What's Slow

- **Reverse iteration** (`rbegin()`/`rend()`) -- requires seeking to end first, then stepping backward. Use forward iteration where possible
- **Secondary index lookups** -- two-step: find in secondary index, then load primary row. ~2x the cost of a primary lookup
- **Large values (>1KB)** -- heap allocation + copy on every read/write. Keep row sizes small
- **Serialization overhead** (multi\_index only) -- pack/unpack on every read/write. Use `kv::table` with POD structs for hot paths
- **Creating many secondary indices** -- each index doubles the write cost (one KV write + one secondary index write per index per row). Only add indices you actually query
- **Table scans** -- iterating the entire table is O(n). Design your key structure to enable prefix-based range queries instead
- **Frequent erase + re-insert** -- generates chainbase undo history. Prefer `modify` over erase+emplace when updating

### Design Tips

- **Keep primary keys small** -- prefer `uint64_t` (8 bytes). The 24-byte key layout is `[table:8][scope:8][pk:8]`
- **Use scope for logical partitioning** -- e.g. sysio.token uses account name as scope for the `accounts` table, so balance lookups only scan one account's rows
- **Minimize secondary indices** -- each secondary index adds write amplification and storage. Only create indices you actively query
- **Use `kv::table` for simple CRUD** -- if you don't need secondary indices, `kv::table` avoids serialization overhead for POD types
- **Batch reads into single transaction** -- multiple `get()` calls in one action share the same chainbase session
- **Avoid storing large blobs** -- values are limited to 256 KiB; prefer multiple smaller rows over single large entries
- **Use `lower_bound` instead of scanning** -- for range queries, `lower_bound(start_key)` + iterate is much faster than scanning from `begin()`
- **secondary\_index\_view::find() with correct type** -- pass the exact secondary key type (e.g. `uint64_t(42)` not `42`) to avoid template-deduced type mismatch. The API handles coercion but explicit types are clearer

---

## Migration Guide

### From EOSIO multi\_index

No code changes are required. Simply recompile your contract with the new Wire CDT:

1. `#include <sysio/multi_index.hpp>` now provides `sysio::kv_multi_index`, aliased as `sysio::multi_index`
2. `#include <sysio/singleton.hpp>` now provides `sysio::kv_singleton`, aliased as `sysio::singleton`
3. All existing API calls work unchanged:
   - `emplace`, `modify`, `erase`
   - `find`, `require_find`, `get`
   - `lower_bound`, `upper_bound`
   - `begin`/`end`, `rbegin`/`rend`
   - `get_index<>()` with `find`, `lower_bound`, `modify`, `erase`
   - `available_primary_key()`
4. The `payer` parameter is accepted but ignored -- RAM is charged to the contract account
5. `same_payer` is still defined and accepted

### From kv::table to multi\_index

If you started with `kv::table` and later need secondary indices:

1. Change `#include <sysio/kv_table.hpp>` to `#include <sysio/multi_index.hpp>`
2. Add `SYSLIB_SERIALIZE` if not already present
3. Replace `kv::table<Name, T>` with `multi_index<Name, T, indexed_by<...>>`
4. Replace `set(pk, obj)` / `contains(pk)` with `emplace`/`modify`/`find` patterns
5. Note: `begin_all_scopes()` is only available on `kv::table`

---

## Choosing Between APIs

| Feature | multi\_index | kv::table |
|---------|-------------|-----------|
| Drop-in replacement for existing contracts | Yes | No |
| Secondary indices (via `kv_idx_*` intrinsics) | Yes (up to 16) | No |
| Reverse iteration | Yes (`rbegin`/`rend`) | Yes (bidirectional iterators) |
| Zero-copy for POD types | No (always serializes) | Yes |
| Object caching | Yes | No |
| Singleton support | Yes (`sysio::singleton`) | No (but can wrap manually) |
| Cross-scope iteration | No | Yes (`begin_all_scopes`) |
| `set(pk, obj)` convenience | No | Yes |
| `contains(pk)` | No (use `find`) | Yes (single intrinsic) |
| Best for | General purpose, complex queries | Hot path, simple CRUD |

**Decision matrix:**

- Need secondary indices or complex queries? Use **multi\_index**.
- Migrating an existing contract? Use **multi\_index** (zero code changes).
- Need maximum throughput on simple reads/writes with POD structs? Use **kv::table**.
- Need to iterate across all scopes? Use **kv::table** (`begin_all_scopes`).
- Need a simple ordered key-value store with custom keys? Use **kv::raw_table**.
- Not sure? Start with **multi\_index**. You can always switch later.

---

## sysio::kv::raw_table (ordered key-value store with custom keys)

### Include

```cpp
#include <sysio/kv_raw_table.hpp>
```

### Overview

`sysio::kv::raw_table<K, V>` provides an ordered key-value store similar to RocksDB or LevelDB. Define your key type, store key-value pairs, and iterate in key order. No scope or prefix overhead.

Key properties:

- **Custom key types**: keys can be any struct with `SYSLIB_SERIALIZE`
- **Big-endian key encoding**: keys are BE-encoded for correct `memcmp` ordering
- **String/binary key support**: strings and `vector<char>` use NUL-escape encoding, preserving lexicographic order for arbitrary byte sequences (including embedded null bytes)
- **Ordered iteration**: `begin`/`end`, `lower_bound`/`upper_bound`
- **Standard ABI values**: values use LE serialization, decodable by SHiP clients via `abieos`
- Uses format=0 raw KV storage (`contract_row_kv` SHiP delta type)
- Keys <= 24 bytes benefit from the host's SSO fast-path (inline storage, integer comparison)

> **Flat namespace warning:** All `raw_table` instances on the same contract share a single flat namespace. If your contract uses multiple `raw_table` instances, add a discriminator field to your key struct to prevent collisions. See the warning comment in `<sysio/kv_raw_table.hpp>` for details and examples.

### API reference

| Method | Description |
|--------|-------------|
| `set(key, value)` | Store or update a key-value pair |
| `get(key)` | Returns `std::optional<V>`, empty if not found |
| `contains(key)` | Returns `bool` |
| `erase(key)` | Delete a key-value pair, returns RAM delta |
| `begin()` / `end()` | Forward iteration over all entries |
| `lower_bound(key)` | First entry with key >= given key |
| `upper_bound(key)` | First entry with key > given key |

### ABI key metadata

To declare the key type in the ABI (so SHiP clients can decode raw keys), use `[[sysio::kv_key("key_struct")]]` on the value struct:

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

kv::raw_table<my_key, my_value> geodata;              // reads own contract data
kv::raw_table<my_key, my_value> other("othercon"_n);   // reads another contract's data (read-only)
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

The `my_key` struct is also added to the ABI's `structs` section.

### Key encoding details

Keys are encoded in big-endian byte order so that `memcmp`-based comparison in the underlying store produces correct lexicographic ordering:

| Type | Encoding |
|------|----------|
| `uint8` | 1 byte |
| `uint16` | 2 bytes BE |
| `uint32` | 4 bytes BE |
| `uint64` / `name` | 8 bytes BE |
| `uint128` | 16 bytes BE |
| `double` | 8 bytes (IEEE 754 with sign-flip for sort order) |
| `string` | Raw bytes + `0x00` terminator (no embedded nulls) |
| `bool` | 1 byte (0 or 1) |

Composite key structs are encoded by concatenating each field in declaration order using the above rules. The `SYSLIB_SERIALIZE` macro's field order determines the encoding order.

### Example

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
   }
};
```
