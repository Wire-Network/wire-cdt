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
- **V** — value struct (must have `SYSLIB_SERIALIZE`). A stored row shorter than `V` aborts. A longer one reads the fields `V` declares and ignores the rest, so a contract can read rows a wider struct wrote. `kv::global` of a fixed-size value is stricter, and aborts on any other size.
- **Indices...** — zero or more `kv::index<"name"_n, extractor>` declarations

\*\*ABI annotations are optional for `_n` tables\*\* — the abigen auto-derives the table name, value type, and key metadata from the template parameters. For `_i` tables, `[[sysio::table("long_name")]]` is still required (DJB2 hash can\'t reverse to the original string). `[[sysio::kv_key("struct")]]` can override the auto-derived key metadata if needed.

## Constructor

```cpp
kv::table<"mytbl"_n, K, V> tbl(get_self());   // reads own data
kv::table<"mytbl"_n, K, V> tbl("other"_n);    // reads another contract's data
```

Writes always land on the receiver, whatever account the handle names, so a handle on another account is for reading. On such a handle `erase(key)` erases the receiver's row of that key. A table with no index never reads the other account first, and a table with one reads the row there and fails if it is missing. `modify` through an iterator on such a handle aborts when the receiver has no row of that key: with `cannot modify a row the receiver does not hold`, or with `KV secondary index entry not found for update` when the new value moves an index entry.

Do not write through a handle on another account. Neither the wrappers nor the host stop it, and if the receiver holds a row of that key, the result can be wrong without an abort. The wrapper works out index changes from the row it read on the other account, but the write lands on the receiver. So the receiver's row can end up with a new secondary key while its index entry keeps the old one, or `upsert` can leave the row with two index entries. `erase(key)` aborts in this case with `KV secondary index entry not found for remove`, unless the two secondary keys match.

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
| `erase(iter)` | Erase via iterator; returns next, resolved on first use |
| `erase(key)` | Erase by key; asserts if missing |

`emplace` keeps its optional `exists_msg` argument, and `erase(key)` and `modify(payer, key, lambda)` their `not_found_msg`, so a contract raises its own text when the host reports a duplicate or a missing row. `modify` through an iterator whose row has gone raises `cannot modify a row that no longer exists` rather than creating the row again.

## Host calls

| Operation | Host calls |
|-----------|------------|
| `find`, `contains`, `get`, `try_get` | 1 |
| `emplace`, `upsert` | 1, plus the index work below |
| `erase(key)` | 1; with indexes, 2 plus 1 per index |
| `find` then `erase(it)`, result unused | 2, plus 1 per index |
| `modify(payer, iter, value)` | 1; with indexes, 2 plus 1 per index whose key changed |
| `begin()`, `lower_bound()`, `upper_bound()` | 2 |
| `++it` in a scan | 1 |
| `--end()`, `rbegin()` | 2 |
| `++rit` in a reverse scan | 1 |
| `rend()`, `*rit`, `rit->` | 0 |
| Index `begin()`, `lower_bound()`, `upper_bound()`, `find()` | 3 |
| Index `++it` | 2 |
| Key-only index `++it` | 1 |

`emplace` adds one `kv_idx_store` per index. `upsert` on a table with indexes also reads the old value first, and moves each entry whose key changed. A read adds one call when a value overflows its 256-byte stack buffer or a key overflows its 64-byte one. An iterator holding a handle adds one `kv_it_destroy` or `kv_idx_destroy` when it goes, which these counts leave out.

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
| `find` + `emplace`/`modify(iter)` | 2 | The same cost, written as a branch |
| `try_get` + `set` | 2, or 3 with an index | `set` reads the row again to move its entries |
| `contains` + `emplace`/`modify(key)` | 2-3 | `modify(key)` reads the row again |

The lambda variant of `upsert` reads the old value once (to decide insert vs update and handle secondary indexes), applies the lambda if the row exists, and writes exactly once — the theoretical minimum for this operation.

## Iterator

- `*it` returns `const V&` (value only, like multi_index)
- `it->field` accesses value fields directly
- `it.key()` returns `const K&` for the key
- Bidirectional: `++it`, `--it`
- Copy-constructible. A copy makes no host call
- Post-increment/decrement deleted

An iterator holds a copy of its row, so reading it never reaches the host. It takes a host handle only once it has to step:

- `find()` and a copy start with no handle. The first `++` or `--` opens one, by key.
- `erase(iter)` returns an iterator that still owes its step, so ignoring the result costs nothing. The step runs on first use and lands on the first row past the erased key at that moment, so **a row written between the erase and that use is seen**. A row re-inserted at the erased key is not: the step is always strictly past it.
- The result of `erase` takes over the iterator's handle, so `it = tbl.erase(it)` and `it = tbl.erase(std::move(it))` are both 2 host calls per row. An iterator you pass by name stays on the erased row with no handle, and stepping it opens one by key. If it still owed a step from an earlier `erase` it keeps owing that one, and takes it by key at first use.
- `modify(payer, iter, value)` updates that iterator's copy, so it reads the new value and a further `modify` or `erase` through it is good. Any other iterator on the row, a copy included, keeps what it read. On a table with secondary indices, `modify` through an iterator reads the stored row first, for one more `kv_get`, and moves index entries from that, so a write through an old copy keeps the row and its index together. A secondary iterator does the same. If the row has gone it aborts. `erase` still moves entries from the iterator's copy, and aborts if the row's index key has changed.

The table must outlive its iterators, as with `multi_index`: opening a handle reads the table's code and scope.

### Reverse iterator

`rbegin()` returns a `const_reverse_iterator`, which holds one iterator on the row it reads and steps it in place, so a reverse scan costs what a forward scan costs.

- `*rit` and `rit->` read the row, and `rit.key()` its key. A reference lasts until the iterator next moves, as on the forward iterator.
- `rit.base()` is one row past the position, as on `std::reverse_iterator`. It pays for a copy and a step.
- `++rit` past the first row reaches `rend()`. `++` at `rend()` and `--` at `rbegin()` abort, and so does either on an empty table, where the two are one position.
- `std::make_reverse_iterator(it)` still compiles and compares against `rend()`, but its `*` hands back a reference into a temporary. Read through `rbegin()`.

**It does not follow the table after a write.** `std::reverse_iterator` holds the row past the one it reads and works the row out on every read, so what it reads moves with the table. This iterator holds the row it reads, which is why a read makes no host call, and stays there:

- `while (rit != t.rend()) t.erase(*rit);` is the upstream `multi_index` loop that relies on the difference. Nothing moves `rit`, so on `multi_index` its second pass aborts with `cannot use an iterator after erasing its row`. Here it does not compile, because `*rit` is the value and not the key, and `t.erase(rit.key())` in its place aborts with `key not found`.
- Reading through `rit` after an erase returns the iterator's own copy of the erased row, with no abort. On `multi_index` it aborts with the same message.
- A row written past the position is not seen. `rit` keeps reading its row, and a new `rbegin()` finds the new one.

To erase while walking back, step on from the erased row, or rebuild the iterator from the result of `erase`, where `--rit.base()` is the forward iterator on the row `rit` reads:

```cpp
t.erase(rit.key());
++rit;                                                           // the row before

rit = my_table::const_reverse_iterator(t.erase(--rit.base()));   // the same, with no separate step
```

## Secondary Indices

Declare with `kv::index`:

```cpp
using my_table = kv::table<"users"_n, user_key, user_val,
   kv::index<"byowner"_n, kv::member_data<user_val, name, &user_val::owner>>,
   kv::index<"bybal"_n, sysio::const_mem_fun<user_val, uint64_t, &user_val::get_balance>>
>;
```

`sysio::const_mem_fun` comes from `<sysio/multi_index.hpp>`, which `<sysio/kv_table.hpp>` does not include. `kv::member_data` needs nothing more.

Access via `get_index`:

```cpp
auto idx = tbl.get_index<"byowner"_n>();
auto it = idx.find("alice"_n);
auto lb = idx.lower_bound(100);
```

Secondary iterator: `*it` returns `const V&`, `it.key()` returns `const K&`. Supports `modify`, `erase` and `begin/end`. It is move-only and has no reverse iterator: walk back with `--` from `end()`.

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
