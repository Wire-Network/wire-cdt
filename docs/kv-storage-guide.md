# Wire KV Storage Guide

Wire uses a key-value database as the storage backend for smart contract state, replacing the EOSIO `db_*_i64` host functions with a simpler set of KV intrinsics. Four C++ APIs are available to contract developers:

- **[`sysio::multi_index`](kv-multi-index.md)** -- Full-featured table with secondary indices, reverse iteration, object caching, and singletons. API-compatible with EOSIO `multi_index`. Recommended for migrating existing contracts.
- **[`sysio::kv::indexed_table`](kv-indexed-table.md)** -- Compact format=0 keys with secondary indices. No legacy scope overhead. Recommended for new contracts that need secondary lookups.
- **[`sysio::kv::table`](kv-table.md)** -- High-performance table for simple CRUD workloads. No secondary index support. Scope-based partitioning with cross-scope iteration.
- **[`sysio::kv::raw_table`](kv-raw-table.md)** -- Low-level ordered key-value store with custom keys. No secondary indices, no scope. For contracts that need direct byte-level key control.

All APIs use the same underlying KV host functions (intrinsics). Existing EOSIO contracts compile unchanged with the new CDT -- no source code modifications required.

---

## Choosing Between APIs

| Feature | multi\_index | indexed\_table | kv::table | raw\_table |
|---------|-------------|---------------|-----------|-----------|
| Key format | Format=1 (24B) | Format=0 (compact) | Format=1 (24B) | Format=0 (compact) |
| Scope | Yes | No | Yes | No |
| Secondary indices | Yes (up to 16) | Yes (up to 16) | No | No |
| Key type | `uint64_t` | Custom struct | `uint64_t` | Custom struct |
| Value type | Unified row `T` | Separate `K`, `V` | Unified row `T` | Separate `K`, `V` |
| Mutation style | Lambda | Direct value | Lambda | Direct set |
| Object caching | Yes | No | No | No |
| Zero-copy for POD values | Yes | Yes | Yes | Yes |
| Cross-scope iteration | No | N/A (no scope) | Yes | N/A (no scope) |
| Key-only sec. iteration | No | Yes | N/A | N/A |
| Payer parameter | Required (first arg) | Optional (overloads) | Required | Optional (default) |
| EOSIO compatible | Yes (drop-in) | No | No | No |

**Decision matrix:**

- Migrating an existing EOSIO contract? Use **[multi\_index](kv-multi-index.md)** (zero code changes).
- New contract needing secondary indices? Use **[indexed\_table](kv-indexed-table.md)** (compact keys, no scope waste).
- Maximum throughput on simple reads/writes with POD structs and scope-based partitioning? Use **[kv::table](kv-table.md)**.
- Need to iterate across all scopes? Use **[kv::table](kv-table.md)** (`begin_all_scopes`).
- Need a simple ordered key-value store with custom keys, no indices? Use **[raw\_table](kv-raw-table.md)**.
- Not sure? Start with **[multi\_index](kv-multi-index.md)** or **[indexed\_table](kv-indexed-table.md)**.

---

## Key Encoding

Two key formats are used across the four APIs:

### Format=1 (multi\_index, kv::table)

```
[table_name: 8 bytes, big-endian] [scope: 8 bytes, big-endian] [primary_key: 8 bytes, big-endian]
```

Properties:
- Fixed 24 bytes
- Integer fast-path: 8-byte big-endian keys compared via single `bswap64`
- SHiP compatible: reversible to legacy `contract_row` format (table, scope, primary\_key)
- Secondary index keys include scope: `[scope:8B][pk:8B]` = 16 bytes

### Format=0 (indexed\_table, raw\_table)

Variable-length keys encoded via `be_key_stream`. Each field is concatenated in `SYSLIB_SERIALIZE` declaration order using big-endian encoding with sort-preserving transforms:

| Type | Encoding | Size |
|------|----------|------|
| `uint8` .. `uint128` | Big-endian | 1-16 |
| `int8` .. `int128` | XOR sign bit + big-endian | 1-16 |
| `name` | Big-endian uint64 | 8 |
| `float` / `double` | IEEE 754 sign-magnitude to unsigned sortable | 4/8 |
| `bool` | 0 or 1 | 1 |
| `string` / `vector<char>` | NUL-escape + `0x00 0x00` terminator | variable |

Compact keys reduce per-row RAM billing since key bytes are billed directly. The integer fast-path comparator (single `bswap64` for 8-byte keys) is independent of storage layout.

---

## Performance Comparison

| Scenario | multi\_index | indexed\_table | kv::table | raw\_table |
|----------|-------------|---------------|-----------|-----------|
| Simple row read | Baseline | ~same | ~same | ~same |
| Simple row write | Baseline | ~same | ~same | ~same |
| With secondary indices | Supported | Supported | N/A | N/A |
| Key storage overhead | 24B fixed | Variable (as small as needed) | 24B fixed | Variable |
| OC runtime (token transfer) | ~0.5 us | ~0.5 us | ~0.5 us | ~0.5 us |
| Serialization overhead | memcpy for POD | memcpy for POD | memcpy for POD | memcpy for POD |

At the OC (Optimized Compiler) runtime tier, intrinsic call overhead dominates and all APIs perform at near-parity.

---

## Performance Best Practices

### What's Fast

- **Point lookups by primary key** (`find(pk)`, `get(pk)`) -- single KV read
- **Trivially\_copyable value types** -- all four table APIs use zero-copy (memcpy) when `sizeof(V) == pack_size(V)`
- **Emplace/modify with same value size** -- no reallocation
- **Iterating forward** (`begin()` to `end()`, `++`) -- sequential access pattern, cache-friendly
- **8-byte integer keys** -- single `bswap64` comparison instruction
- **Compact keys** -- key bytes are billed directly to RAM; smaller keys = lower per-row cost
- **Key-only secondary iteration** (indexed\_table) -- skips `kv_get` and value deserialization

### What's Slow

- **Reverse iteration** (`--end()`) -- requires seeking to end first, then stepping backward
- **Secondary index lookups** -- two-step: find in secondary index, then load primary row. ~2x primary lookup cost
- **Large values (>1KB)** -- heap allocation + copy on every read/write. Keep row sizes small
- **Creating many secondary indices** -- each index doubles write cost per row mutation
- **Table scans** -- iterating the entire table is O(n). Use prefix-based range queries instead

### Design Tips

- **Keep keys small** -- prefer `uint64_t` (8 bytes) or small structs. Key bytes are billed directly to RAM
- **Minimize secondary indices** -- each adds write amplification and storage
- **Use `lower_bound` instead of scanning** -- `lower_bound(start_key)` + iterate is much faster than scanning from `begin()`
- **Use key-only iteration** for scanning -- `indexed_table`'s `key_begin()`/`key_end()` avoids value deserialization
- **Use `indexed_table` for new contracts** -- format=0 keys save 16 bytes per row vs format=1
- **Use POD value types** -- all table APIs use zero-copy (memcpy) for `trivially_copyable` values, eliminating serialization overhead

---

## Migration Guide

### From EOSIO multi\_index

No code changes required. Recompile with the new Wire CDT:

1. `#include <sysio/multi_index.hpp>` now provides `sysio::kv_multi_index`, aliased as `sysio::multi_index`
2. `#include <sysio/singleton.hpp>` now provides `sysio::kv_singleton`, aliased as `sysio::singleton`
3. All existing API calls work unchanged
4. The `payer` parameter is honored -- RAM is charged to the specified payer (requires `sysio.payer` permission for cross-account billing)
5. `same_payer` is still defined and accepted
6. Post-increment (`it++`) and post-decrement (`it--`) are deleted on all iterators -- use pre-increment (`++it`) and pre-decrement (`--it`) instead

### From multi\_index to indexed\_table

If you want to switch an existing contract to `indexed_table` for compact keys:

1. Change `#include <sysio/multi_index.hpp>` to `#include <sysio/kv_indexed_table.hpp>`
2. Split your row type into separate `K` (key) and `V` (value) structs
3. Replace `indexed_by<Name, const_mem_fun<...>>` with `kv::index<Name, ...>`
4. Replace lambda-based `emplace`/`modify` with direct-value calls
5. Note: existing on-chain data uses format=1 keys. Migrating requires re-inserting all rows in format=0

### From raw\_table to indexed\_table

If you started with `raw_table` and need secondary indices:

1. Change `#include <sysio/kv_raw_table.hpp>` to `#include <sysio/kv_indexed_table.hpp>`
2. Replace `raw_table<K, V>` with `indexed_table<"name"_n, K, V, kv::index<...>>`
3. Replace `set(key, value)` with `emplace(key, value)` for new rows and `modify(it, value)` for updates
4. Note: `indexed_table` has no raw `set()` -- this prevents secondary index corruption
5. Existing format=0 data is compatible (same key encoding). Add secondary indices by re-inserting rows
