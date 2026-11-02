# sysio::multi\_index

> **Backward compatibility only.** For new scoped contracts, use [`kv::scoped_table`](kv-scoped-table.md) — same scope semantics, byte-identical primary keys, but no object cache overhead. For unscoped contracts, use [`kv::table`](kv-table.md). See the [KV Storage Guide](kv-storage-guide.md) for the full comparison.

## Include

```cpp
#include <sysio/multi_index.hpp>   // explicit
#include <sysio/sysio.hpp>          // or via umbrella
```

## Overview

`sysio::multi_index` is a **compatibility shim** for the EOSIO `multi_index` — the same API over a
different store, not the same implementation. Nearly all contract code carries over unchanged, but
it is not fully source-compatible: the divergences below include edits every port must make. They
are:

- the postfix iterator operators `it++` / `it--` are deleted, because copying a KV iterator
  duplicates a host-side handle. Rewrite those to `++it` / `--it`. The compiler finds every
  *direct* use, but **not the reverse ones**: the iterator from `rbegin()` keeps its postfix
  operators so upstream's reverse loops still compile, and
  `for (auto rit = t.rbegin(); rit != t.rend(); rit++)` performs exactly the handle-duplicating
  copy the deletion exists to prevent. Sweep reverse loops by hand;
- the five primary lookups — `find`, `require_find`, `get`, `lower_bound`, `upper_bound` — take a
  `name` as well as a `uint64_t`, where upstream declares each as a member template. That is a real
  source break in two shapes, and it applies to all five:
  - **an explicit template argument stops compiling.** `t.template lower_bound<uint64_t>(k)` is
    valid upstream, where the API is a member template; against Wire's concrete overloads it is
    `error: 'lower_bound' following the 'template' keyword does not refer to a template`. Drop the
    `template` keyword and the explicit argument — `t.lower_bound(k)`;
  - **a wrapper convertible to both `name` and `uint64_t` becomes ambiguous**, where against a
    single `uint64_t` parameter it selected the `uint64_t` conversion. Convert at the call site.

  (The bare `&table_type::lower_bound` also does not compile — here because it is an overload set,
  upstream because `PK` cannot be deduced — so that one is not a Wire-only incompatibility. A named
  `static_cast<table_type::const_iterator (table_type::*)(uint64_t) const>(&table_type::lower_bound)`
  resolves one on Wire.)
- secondary key types are limited to five: `uint64_t`, `uint128_t`, `double`, `long double` and
  `checksum256`. A sixth is a `static_assert`, not a silent misorder — a secondary index is a
  byte-ordered map, so `kv_idx_lower_bound` compares the *encoded* key with `memcmp`, and only
  these five have an order-preserving encoder. The same five upstream supports, so a ported
  contract already satisfies it.

  **For a wider key — a narrow integer, a `name`, or a composite struct — use `kv::table` with
  `kv::index`**, which encodes through `be_key_stream`. That is what the chain uses to build
  `get_table_rows` bounds, so such an index stays queryable by name over RPC. Not enums:
  `be_key_stream` has no enum overload, so give an enum key an explicit integer member.

  Both checks — this one and `std::is_trivially_copyable`, which is why `std::string` and
  `std::vector` keys are rejected — run in `secondary_index_view` when `get_index<...>()` is
  instantiated. The encoder checks the supported set again on the write path, reached by `emplace`
  / `modify` / `erase` maintaining secondary entries, so an unsupported key is refused at the first
  query or mutator and is never stored.
- `const_reverse_iterator` is its own class rather than a `std::reverse_iterator`, so
  `std::reverse_iterator<table_type::const_iterator> r = t.rbegin();` fails with `no viable
  conversion`, and `iterator_type` is gone. Declare it `auto` or `table_type::const_reverse_iterator`.
  `std::make_reverse_iterator(it)` still compiles and compares against `rend()`.

Two more compile unchanged and behave differently, so the compiler finds neither:

- **A reverse iterator does not follow the table after a write.** `std::reverse_iterator` holds the
  row past the one it reads and works the row out on every read. This one holds the row it reads,
  and stays there:
  - `while (rit != t.rend()) t.erase(*rit);` never moves `rit`, so its second pass aborts with
    `cannot use an iterator after erasing its row`.
  - Reading through `rit` after an erase, as in `t.erase(*rit); rit->id`, aborts the same way. So
    does copying it, which `rit.base()` and `rit++` do.
  - A row emplaced past the position is not seen.
  - `t.erase(*rit); ++rit;` reaches the row before, which upstream skips.

  To carry on from an erase, step with `++rit` or rebuild the iterator from the result of `erase`:
  `rit = table_type::const_reverse_iterator(t.erase(--rit.base()));`. A secondary index takes the
  same form through `idx.erase`.
- **A secondary iterator steps from its old key after a `modify` moves that key.** It reads the new
  value, but `++` and `--` start from the old position, because the host keeps a handle on the key
  it was on. Upstream's iterator moves with the entry. Rows at 10, 20 and 30 with `it` on 10, moved
  to 25: `++it` gives 20. A copy of `it`, or a new `find`, starts from 25 and gives 30.

Under the hood, primary rows are stored as 16-byte KV keys and secondary indices use `kv_idx_*`
intrinsics. `multi_index` is the compatibility shim: performance work targets `kv::table`, so its
`find` costs 4 host calls where `kv::table::find` costs 1, and a scan row 2 where `kv::table` pays 1.

Each table gets a `table_id` (uint16) computed via `compute_table_id(name::raw)` from the template parameter.

Key layout (16 bytes):

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8B | `scope` (BE uint64) |
| 8 | 8B | `primary_key` (BE uint64) |

The table name is conveyed by `table_id`, not embedded in the key.

## Key Features

- Up to **16 secondary indices** via `indexed_by` / `const_mem_fun`
- `find`, `require_find`, `get`, `lower_bound`, `upper_bound`
- `emplace`, `modify`, `erase` (returns next iterator)
- `available_primary_key()` for auto-increment
- Object caching for repeated access
- `payer` parameter honored for RAM billing
- `rbegin/rend`, `cbegin/cend` support
- Upstream's mutation guards: `emplace` rejects a duplicate primary key, `emplace` / `modify` /
  `erase` reject a handle whose code is not the receiving account, and `modify` / `erase` reject an
  object that is not a row of this handle, such as a copy, with
  `object passed to modify is not in multi_index`.
- Upstream's key limit: `available_primary_key()` never hands out the top two keys and aborts past
  them. `emplace` can still store any key.
- `--end()` on an empty table or index aborts, as upstream's does. `rbegin()` there is `rend()`.
- Upstream's row cache: iterators and references point at one cached object per row, which `modify`
  updates in place, so they stay good across a `modify` and see the new value, with the step noted
  above for a secondary key that moved. A `modify` of a row that has gone aborts with
  `cannot modify a row that no longer exists`.
- An `erase` ends every iterator and reference on the row, secondary iterators included, since they
  point at the same object. Reading through one, copying it, or passing it to `modify` or `erase`
  aborts with `cannot use an iterator after erasing its row`. Comparing it and stepping it stay good,
  and `++` and `--` reach the rows either side. This covers an erase through the same table object:
  another object holding the same table has a row cache of its own.
- The table must outlive its iterators.

## Singleton

```cpp
#include <sysio/singleton.hpp>
```

`sysio::singleton<Name, T>` is an alias for `kv_singleton`, which holds a `kv_multi_index` as its
storage. API: `exists`, `get`, `get_or_default`, `get_or_create`, `set`, `remove`.

`get_or_create`, `set` and `remove` mutate through that member, so they inherit the guards above: a
singleton handle constructed on another account's code is read-only, on the same terms as a table
handle.

## Example

```cpp
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
```
