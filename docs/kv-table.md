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

## Zero-copy path

When `T` satisfies **both** conditions:
1. `std::is_trivially_copyable<T>` is true
2. `sizeof(T) == pack_size(T)` (no struct padding)

...values are stored and loaded via `memcpy` instead of datastream serialization. This eliminates pack/unpack overhead entirely. Note: all four table APIs (`multi_index`, `indexed_table`, `kv::table`, `raw_table`) now use zero-copy for trivially copyable types.

Types with `std::string`, `std::vector`, `std::optional`, or nested structs cannot use the zero-copy path and fall back to standard serialization (equivalent performance to `multi_index`).

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
