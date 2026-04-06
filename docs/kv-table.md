# sysio::kv::table

## Include

```cpp
#include <sysio/kv_table.hpp>
```

## Overview

`sysio::kv::table<TableName, T>` provides a simpler, lower-overhead interface for contracts that do not need secondary indices. It uses format=1 keys (`[table:8B][scope:8B][pk:8B]`), the same as `multi_index`.

Key properties:

- **Zero-copy** for `trivially_copyable` structs: values are stored/loaded via `memcpy` instead of datastream serialization, provided `sizeof(T) == pack_size(T)` (no struct padding)
- Falls back to datastream serialization for complex types (vectors, strings, nested structs)
- No secondary index support -- use `multi_index` or `indexed_table` if you need secondary lookups
- No object caching -- each `find`/`get` call reads from storage
- Bidirectional iterators: `begin`/`end`, `lower_bound`/`upper_bound`
- Cross-scope iteration via `begin_all_scopes()` / `end_all_scopes()` (unique to `kv::table`)
- Row type must have a `uint64_t primary_key() const` method

## When to use kv::table

Use `kv::table` when you need:
- Maximum read/write throughput on simple POD structs (zero-copy path)
- Cross-scope iteration (`begin_all_scopes`)
- A scope-based table without secondary index overhead

Do **not** use `kv::table` if you need secondary indices -- use `multi_index` or `kv::indexed_table` instead.

## Constructor

```cpp
kv::table<"balances"_n, balance_row> bal(code, scope);
```

- `code` -- the contract account that owns the data
- `scope` -- partitions rows within the same table (e.g., use account name as scope for per-account tables)

## API reference

| Method | Description |
|--------|-------------|
| `find(pk)` | Returns iterator, or `end()` if not found |
| `require_find(pk, msg)` | find() + assert |
| `get(pk, msg)` | Returns row by value, asserts if missing |
| `contains(pk)` | Returns `bool`, single intrinsic call |
| `lower_bound(pk)` | First entry with key >= pk |
| `upper_bound(pk)` | First entry with key > pk |
| `begin()` / `end()` | Forward iteration over all rows in scope |
| `emplace(payer, constructor)` | Construct and store a new row |
| `modify(itr, payer, updater)` | Update an existing row |
| `erase(pk)` / `erase(itr)` | Delete a row |
| `available_primary_key()` | Next auto-increment key |
| `begin_all_scopes()` / `end_all_scopes()` | Iterate ALL rows across ALL scopes (returns `scoped_row`) |

## Zero-copy optimization

See [Zero-Copy Serialization](kv-storage-guide.md#zero-copy-serialization) in the storage guide. This optimization applies to all table APIs — `kv::table`, `kv::indexed_table`, `kv::raw_table`, `kv::global`, `singleton`, and `multi_index`.

## Example

```cpp
#include <sysio/kv_table.hpp>
#include <sysio/sysio.hpp>

using namespace sysio;

struct balance_row {
   uint64_t account;
   uint64_t amount;

   uint64_t primary_key() const { return account; }

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
      balance_row updated = sender;
      updated.amount -= amount;
      bal.modify(bal.find(from.value), get_self(), [&](auto& r) { r = updated; });

      // Credit
      auto to_itr = bal.find(to.value);
      if (to_itr == bal.end()) {
         bal.emplace(get_self(), [&](auto& r) {
            r.account = to.value;
            r.amount = amount;
         });
      } else {
         bal.modify(to_itr, get_self(), [&](auto& r) { r.amount += amount; });
      }
   }

   [[sysio::action]]
   void dumpall() {
      balance_table bal(get_self(), get_self().value);
      // Cross-scope iteration: see ALL balances regardless of scope
      for (auto it = bal.begin_all_scopes(); it != bal.end_all_scopes(); ++it) {
         print("scope=", it->scope, " pk=", it->primary_key, " amount=", it->obj.amount, "\n");
      }
   }
};
```

---

## Singleton

`sysio::singleton<Name, T>` is built on `kv::table` and stores a single value per scope. It is the scoped equivalent of [`kv::global`](kv-global.md).

### Include

```cpp
#include <sysio/singleton.hpp>
```

### API reference

| Method | Description |
|--------|-------------|
| `exists()` | Returns true if a value has been stored |
| `get()` | Returns stored value, asserts if missing |
| `get_or_default(def)` | Returns stored value or `def` |
| `get_or_create(payer, def)` | Returns stored value, or stores and returns `def` |
| `set(value, payer)` | Stores or overwrites the value |
| `remove()` | Deletes the stored value |

### When to use singleton vs kv::global

- **singleton** -- one value per scope. Use when different scopes need different config (e.g., per-account settings).
- **[kv::global](kv-global.md)** -- one value per contract, no scope. Use for contract-wide config (rate limits, feature flags). Simpler API, smaller key (8B vs 24B).

### Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

struct app_config {
   uint64_t version;
   std::string name;
   SYSLIB_SERIALIZE(app_config, (version)(name))
};

using config_singleton = singleton<"config"_n, app_config>;

class [[sysio::contract]] myapp : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void init() {
      config_singleton cfg(get_self(), get_self().value);
      cfg.set({1, "myapp"}, get_self());
   }

   [[sysio::action]]
   void getver() {
      config_singleton cfg(get_self(), get_self().value);
      auto c = cfg.get_or_default({0, ""});
      print("version: ", c.version);
   }
};
```
