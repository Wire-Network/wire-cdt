# sysio::multi\_index

> **Backward compatibility only.** For new scoped contracts, use [`kv::scoped_table`](kv-scoped-table.md) — same scope semantics, byte-identical primary keys, but no object cache overhead. For unscoped contracts, use [`kv::table`](kv-table.md). See the [KV Storage Guide](kv-storage-guide.md) for the full comparison.

## Include

```cpp
#include <sysio/multi_index.hpp>   // explicit
#include <sysio/sysio.hpp>          // or via umbrella
```

## Overview

`sysio::multi_index` is a source-compatible shim for the EOSIO `multi_index` — the same API over a
different store, not the same implementation. Nearly all contract code carries over unchanged; the
the known divergences are:

- the postfix iterator operators `it++` / `it--` are deleted, because copying a KV iterator
  duplicates a host-side handle. Rewrite those to `++it` / `--it`; the compiler finds every one;
- the primary `lower_bound` / `upper_bound` are overloads on `uint64_t` and `name` rather than
  upstream's member template, so the bare `&table_type::lower_bound` does not compile (a named
  `static_cast` still resolves one);
- the mutation guards below arrive with #113 and are absent from earlier toolchains. Under the hood, primary rows are stored as 16-byte KV keys and secondary indices use `kv_idx_*` intrinsics.

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
  `erase` reject a handle whose code is not the receiving account — **these arrive with
  [wire-cdt#113](https://github.com/Wire-Network/wire-cdt/pull/113) and are absent from any CDT
  built before it.** On an older toolchain a duplicate `emplace` silently overwrites the row and
  strands its secondary mapping.

## Singleton

```cpp
#include <sysio/singleton.hpp>
```

`sysio::singleton<Name, T>` is backed by `kv_multi_index`. API: `exists`, `get`, `get_or_default`, `get_or_create`, `set`, `remove`.

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
