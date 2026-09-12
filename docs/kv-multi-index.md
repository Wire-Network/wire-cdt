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
  *direct* use, but **not the reverse ones**: `rbegin()` / `rend()` hand back a
  `std::reverse_iterator`, whose postfix operators belong to the adaptor and are not deleted, so
  `for (auto rit = t.rbegin(); rit != t.rend(); rit++)` compiles clean and performs exactly the
  handle-duplicating copy the deletion exists to prevent. Sweep reverse loops by hand;
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
- secondary key types must be `std::is_trivially_copyable` — and that is the *only* check. Upstream
  supports exactly five, `uint64_t`, `uint128_t`, `double`, `long double` and `checksum256`, because
  its index backend is the five `db_idx*` intrinsic families and no more. Wire does not enforce that
  set: `encode_secondary` is order-preserving for those five and falls back to a generic `pack()`
  for anything else, so a `uint32_t` key compiles and then sorts by its native little-endian bytes
  — `find` still matches, `lower_bound` and ordered iteration are silently wrong. **Keep to the
  five.** The diagnosis also differs: Wire's `static_assert` sits in `secondary_index_view`, so it
  fires when you first call `get_index<...>()` rather than at the declaration.

Under the hood, primary rows are stored as 16-byte KV keys and secondary indices use `kv_idx_*`
intrinsics.

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
- Upstream's mutation guards: `emplace` rejects a duplicate primary key, and `emplace` / `modify` /
  `erase` reject a handle whose code is not the receiving account.

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
