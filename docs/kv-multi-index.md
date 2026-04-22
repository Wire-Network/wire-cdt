# sysio::multi\_index

> **Backward compatibility only.** For new scoped contracts, use [`kv::scoped_table`](kv-scoped-table.md) — same scope semantics, byte-identical primary keys, but no object cache overhead. For unscoped contracts, use [`kv::table`](kv-table.md). See the [KV Storage Guide](kv-storage-guide.md) for the full comparison.

## Include

```cpp
#include <sysio/multi_index.hpp>   // explicit
#include <sysio/sysio.hpp>          // or via umbrella
```

## Overview

`sysio::multi_index` is a drop-in replacement for the EOSIO `multi_index`. Under the hood, primary rows are stored as 16-byte KV keys and secondary indices use `kv_idx_*` intrinsics.

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

## Secondary Index Storage and Ordering

Secondary rows store an 8-byte `primary_id` (the referenced primary row's chainbase id), not a copy of the primary-key bytes. Iteration resolves the primary lazily via a by_id lookup, and `kv_idx_primary_key` materializes the primary-key bytes on demand.

Within duplicate secondary keys, iteration order is **chainbase insertion order** (the order rows were inserted) rather than primary-key byte-lex order. Do not rely on a specific ordering for rows whose secondary keys collide; if you need deterministic per-key ordering, sort in the contract after collecting matches.

This is baseline behavior at launch. Wire has no pre-existing `kv_multi_index` chain state, so no protocol feature gate or migration is involved.

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
