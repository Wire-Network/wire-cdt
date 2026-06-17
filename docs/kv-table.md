# sysio::kv::table

> This is the primary KV table abstraction for new contracts.

## Include

```cpp
#include <sysio/kv_table.hpp>
```

For `_i` literal support:
```cpp
#include <sysio/hash_id.hpp>
```

## Overview

`kv::table<TableName, K, V, Indices...>` is a strongly-typed ordered key-value table with optional secondary indices. Keys are user-defined structs serialized to big-endian byte order for correct lexicographic comparison.

Each table gets a unique `table_id` (uint16, DJB2 hash of template parameter), providing automatic namespace isolation — no key collisions between different tables.

## Template Parameters

```cpp
template<name::raw TableName, typename K, typename V, typename... Indices>
```

- **TableName** — table identifier. Both `_n` and `_i` literals work:
  ```cpp
  kv::table<"accounts"_n, my_key, my_val>              // short name
  kv::table<"user_balance_history"_i, my_key, my_val>   // long name (>13 chars)
  ```
- **K** — key struct (must have `SYSLIB_SERIALIZE`)
- **V** — value struct (must have `SYSLIB_SERIALIZE`)
- **Indices...** — zero or more `kv::index<"name"_n, extractor>` declarations

\*\*ABI annotations are optional for `_n` tables\*\* — the abigen auto-derives the table name, value type, and key metadata from the template parameters. For `_i` tables, `[[sysio::table("long_name")]]` is still required (DJB2 hash can\'t reverse to the original string). `[[sysio::kv_key("struct")]]` can override the auto-derived key metadata if needed.

## Constructor

```cpp
kv::table<"mytbl"_n, K, V> tbl(get_self());   // reads own data
kv::table<"mytbl"_n, K, V> tbl("other"_n);    // reads another contract's data
```

## Query Methods

| Method | Returns | Description |
|--------|---------|-------------|
| `find(key)` | `const_iterator` | Find by primary key; `end()` if missing |
| `require_find(key, msg)` | `const_iterator` | Find or assert |
| `get(key, msg)` | `V` | Get value by key; asserts if missing |
| `try_get(key)` | `std::optional<V>` | Get value; nullopt if missing |
| `contains(key)` | `bool` | Check existence |
| `lower_bound(key)` | `const_iterator` | First entry >= key |
| `upper_bound(key)` | `const_iterator` | First entry > key |
| `begin()` / `end()` | `const_iterator` | Full range in key order |
| `cbegin()` / `cend()` | `const_iterator` | Const aliases |
| `rbegin()` / `rend()` | `const_reverse_iterator` | Reverse iteration |

## Mutation Methods

| Method | Description |
|--------|-------------|
| `emplace(payer, key, value)` | Insert new row. **Asserts if key exists.** |
| `emplace(payer, key, lambda)` | Lambda emplace: `[](V& v){ v.x = 1; }` |
| `upsert(payer, key, value)` | Insert or update (handles secondary index cleanup) |
| `upsert(payer, key, default, lambda)` | Insert default or apply lambda to existing. **2 intrinsic calls — optimal for insert-or-modify patterns.** |
| `set(payer, key, value)` | Alias for `upsert` |
| `modify(payer, iter, value)` | Update via iterator |
| `modify(payer, key, lambda)` | Update by key + lambda |
| `erase(iter)` | Erase via iterator; returns next |
| `erase(key)` | Erase by key; asserts if missing |

### Insert-or-modify pattern: `upsert` with lambda

A common pattern is "create a row if it doesn't exist, or update it if it does." For example, adding a token balance:

```cpp
// Optimal: 2 intrinsic calls (1 kv_get + 1 kv_set)
tbl.upsert(payer, key,
   account{deposit},                         // default if key is new
   [&](account& a) { a.balance += deposit; } // updater if key exists
);
```

**Why this is better than alternatives:**

| Approach | Intrinsic calls | Notes |
|----------|----------------|-------|
| `upsert(payer, key, default, lambda)` | 2 (1 get + 1 set) | Optimal |
| `contains` + `emplace`/`modify` | 3-4 | Extra contains + emplace's internal contains |
| `try_get` + `set` | 4 | try_get reads, set reads again internally |
| `find` + `emplace`/`modify(iter)` | 3-4 | Iterator creation + positioning overhead |

The lambda variant of `upsert` reads the old value once (to decide insert vs update and handle secondary indexes), applies the lambda if the row exists, and writes exactly once — the theoretical minimum for this operation.

## Iterator

- `*it` returns `const V&` (value only, like multi_index)
- `it->field` accesses value fields directly
- `it.key()` returns `const K&` for the key
- Bidirectional: `++it`, `--it`
- Copy-constructible (supports `std::reverse_iterator`)
- Post-increment/decrement deleted

## Secondary Indices

Declare with `kv::index`:

```cpp
using my_table = kv::table<"users"_n, user_key, user_val,
   kv::index<"byowner"_n, kv::member_data<user_val, name, &user_val::owner>>,
   kv::index<"bybal"_n, kv::const_mem_fun<user_val, uint64_t, &user_val::get_balance>>
>;
```

Access via `get_index`:

```cpp
auto idx = tbl.get_index<"byowner"_n>();
auto it = idx.find("alice"_n);
auto lb = idx.lower_bound(100);
```

Secondary iterator: `*it` returns `const V&`, `it.key()` returns `const K&`. Supports `modify`, `erase`, `begin/end`, `rbegin/rend`.

Key-only iteration (no value deserialization): `key_begin()` / `key_end()`.

## Complete Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

struct user_key {
   uint64_t id;
   SYSLIB_SERIALIZE(user_key, (id))
};

struct [[sysio::table("users")]] user_val {
   uint64_t balance;
   name     owner;
   uint64_t get_balance() const { return balance; }
   SYSLIB_SERIALIZE(user_val, (balance)(owner))
};

using users_table = kv::table<"users"_n, user_key, user_val,
   kv::index<"byowner"_n, kv::member_data<user_val, name, &user_val::owner>>
>;

class [[sysio::contract]] myapp : public contract {
public:
   using contract::contract;
   users_table users{get_self()};

   [[sysio::action]]
   void adduser(uint64_t id, uint64_t balance, name owner) {
      users.emplace(get_self(), {id}, {balance, owner});
   }

   [[sysio::action]]
   void pay(uint64_t id, uint64_t amount) {
      users.modify(get_self(), {id}, [&](user_val& u) {
         u.balance += amount;
      });
   }

   [[sysio::action]]
   void lookup(name owner) {
      auto idx = users.get_index<"byowner"_n>();
      auto it = idx.find(owner);
      check(it != idx.end(), "user not found");
      print("balance: ", it->balance);
   }

   [[sysio::action]]
   void rmuser(uint64_t id) {
      users.erase({id});
   }
};
```
