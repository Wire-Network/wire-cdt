# Wire KV Storage Guide

Wire uses a key-value database as the storage backend for smart contract state, replacing the EOSIO `db_*_i64` host functions with a simpler set of KV intrinsics. Five C++ APIs are available to contract developers:

### Recommended for new contracts

- **[`sysio::kv::table`](kv-table.md)** -- High-performance table with scope-based partitioning and cross-scope iteration. No secondary indices. Includes **[`sysio::singleton`](kv-table.md#singleton)** support.
- **[`sysio::kv::indexed_table`](kv-indexed-table.md)** -- Compact format=0 keys with secondary indices. No legacy scope overhead. Best choice when secondary lookups are needed.
- **[`sysio::kv::global`](kv-global.md)** -- Non-scoped singleton for contract-wide config/state. One value per name, no scope parameter. Simplest storage API.
- **[`sysio::kv::raw_table`](kv-raw-table.md)** -- Low-level ordered key-value store with custom compound keys. For contracts that need direct byte-level key control.

### Backward compatibility

- **[`sysio::multi_index`](kv-multi-index.md)** -- Drop-in replacement for the EOSIO `multi_index`. API-compatible so existing contracts compile unchanged. **For new contracts, prefer the APIs above** (see [why upgrade](#why-upgrade-from-multi_index) below).
- **[`sysio::singleton`](kv-table.md#singleton)** -- Drop-in replacement for the EOSIO `singleton`. Backed by `kv::table`. **For new contracts that don't need scope, prefer [`kv::global`](kv-global.md)**.

All APIs use the same underlying KV host functions (intrinsics). Existing EOSIO contracts compile unchanged with the new CDT -- no source code modifications required.

---

## Choosing Between APIs

| Feature | multi\_index | indexed\_table | kv::table | raw\_table | singleton | kv::global |
|---------|-------------|---------------|-----------|-----------|-----------|------------|
| Key format | Format=1 (24B) | Format=0 (compact) | Format=1 (24B) | Format=0 (compact) | Format=1 (24B) | Format=0 (8B) |
| Scope | Yes | No | Yes | No | Yes | No |
| Secondary indices | Yes (up to 16) | Yes (up to 16) | No | No | No | No |
| Key type | `uint64_t` | Custom struct | `uint64_t` | Custom struct | Fixed (pk=0) | Fixed (name) |
| Value type | Unified row `T` | Separate `K`, `V` | Unified row `T` | Separate `K`, `V` | `T` | `T` |
| Mutation style | Lambda | Direct value | Lambda | Direct set | `set(val, payer)` | `set(val, payer)` |
| Object caching | Yes | No | No | No | No | No |
| Zero-copy for POD values | Yes | Yes | Yes | Yes | Yes | Yes |
| Cross-scope iteration | No | N/A (no scope) | Yes | N/A (no scope) | N/A | N/A |
| Key-only sec. iteration | No | Yes | N/A | N/A | N/A | N/A |
| Heap-free hot path (POD) | No | Yes | Yes | Yes | Yes | Yes |
| Payer parameter | Required (first arg) | Optional (overloads) | Required | Optional (default) | Required | Required |
| EOSIO compatible | Yes (drop-in) | No | No | No | Yes (drop-in) | No |

**Decision matrix:**

- Migrating an existing EOSIO contract with no code changes? Use **[multi\_index](kv-multi-index.md)** (drop-in).
- New contract needing secondary indices? Use **[indexed\_table](kv-indexed-table.md)**.
- Simple table with scope-based partitioning, no secondary indices? Use **[kv::table](kv-table.md)**.
- Need to iterate across all scopes? Use **[kv::table](kv-table.md)** (`begin_all_scopes`).
- Need a single config/state value per scope? Use **[singleton](kv-table.md#singleton)** (built on `kv::table`).
- Need a single contract-wide config with no scope? Use **[kv::global](kv-global.md)** (simplest API, 8-byte key).
- Need a simple ordered key-value store with custom compound keys? Use **[raw\_table](kv-raw-table.md)**.
- Not sure? Start with **[kv::table](kv-table.md)** or **[indexed\_table](kv-indexed-table.md)**.

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
- Secondary index keys prepend scope to the secondary value: `[scope:8B BE][secondary_value]`
- Primary keys stored in secondary index entries are `[pk:8B]` = 8 bytes (scope is in the secondary key)

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
- **Use POD value types** -- all table APIs use zero-copy (memcpy) for `trivially_copyable` values, eliminating serialization overhead (see [zero-copy optimization](#zero-copy-optimization) below)

### Key Size Limit (`KV_KEY_BUF_CAP`)

Format=0 keys (`raw_table`, `indexed_table`) are encoded on the stack via `be_key_stream`, which uses a fixed buffer. The default capacity is **256 bytes**, matching the default chain `max_kv_key_size`. Keys that exceed this limit will abort with `"be_key_stream: key too large"`.

To increase the limit (e.g., for contracts with long string keys), pass `-DKV_KEY_BUF_CAP=512` to `cdt-cpp`. The chain supports up to 1024 bytes.

```bash
cdt-cpp -DKV_KEY_BUF_CAP=512 -abigen -o mycontract.wasm mycontract.cpp
```

**Stack usage note:** Each `be_key_stream` instance uses `KV_KEY_BUF_CAP` bytes on the WASM stack. Contracts with many secondary indices create one instance per index during `emplace`/`modify`/`erase`. With 16 indices at `KV_KEY_BUF_CAP=256`, this is ~4KB of the default 8KB WASM stack. If you increase the buffer and have many indices, you may also need to increase the WASM stack size.

Format=1 keys (`kv::table`, `multi_index`) use fixed 24-byte keys and are not affected by this limit.

### Zero-Copy Serialization

All table APIs automatically use zero-copy `memcpy` serialization for value types that satisfy two compile-time conditions:

1. `std::is_trivially_copyable<T>` — no `std::string`, `std::vector`, `std::optional`, virtual functions, or non-trivial constructors/destructors
2. `sizeof(T) == pack_size(T{})` — no struct padding (the raw memory layout matches the serialized field layout)

When both conditions hold, `sysio::kv::is_fixed_serializable_v<T>` is `true` and the compiler generates a single code path using a fixed `char[sizeof(T)]` stack buffer and `memcpy` — no dynamic allocation, no size probing, exactly one host call per read or write.

Types that don't qualify fall back to field-by-field datastream serialization with a stack-first buffer (`kv_value_stack_size` = 256 bytes inline, heap fallback for larger values). This is still efficient but involves per-field encoding/decoding and may require a size-probe host call.

#### Verifying your type qualifies

Use `static_assert` with `is_fixed_serializable_v`. The assert **must be placed after the class definition**, not inside it. The `SYSLIB_SERIALIZE` macro generates `constexpr` friend operators that are only visible via argument-dependent lookup (ADL) after the enclosing class is complete. A `static_assert` inside the class body will always evaluate to `false` because the operators aren't defined yet at that point.

```cpp
class [[sysio::contract]] mycontract : public contract {
public:
   using contract::contract;

   struct balance_row {
      uint64_t account;
      uint64_t amount;
      uint64_t primary_key() const { return account; }
      SYSLIB_SERIALIZE(balance_row, (account)(amount))
   };

   // WRONG — always fails inside the class body:
   // static_assert(kv::is_fixed_serializable_v<balance_row>, "...");

   // ... actions ...
};

// CORRECT — after the closing brace of the class:
static_assert(sysio::kv::is_fixed_serializable_v<mycontract::balance_row>,
              "balance_row should qualify for zero-copy serialization");
```

#### When the check fails: struct padding

The most common reason `is_fixed_serializable_v` is `false` for an all-POD struct is **trailing padding**. The compiler inserts padding bytes at the end of a struct to satisfy the alignment requirement of the largest member:

```cpp
struct row {
   uint64_t id;     // 8 bytes at offset 0
   uint32_t flags;  // 4 bytes at offset 8
   // 4 bytes PADDING inserted here to align struct to 8 bytes
};
// sizeof(row) = 16, pack_size(row{}) = 12 (only the actual fields)
// is_fixed_serializable_v<row> = false
```

The zero-copy path stores `sizeof(T)` bytes via `memcpy`. If `sizeof != pack_size`, the stored data would include uninitialized padding bytes that don't correspond to any field.

#### Enabling zero-copy for padded structs

Add an explicit padding field so that `sizeof == pack_size`:

```cpp
struct row {
   uint64_t id;
   uint32_t flags;
   uint32_t _padding = 0;  // fills the 4-byte gap
   SYSLIB_SERIALIZE(row, (id)(flags)(_padding))
};
// sizeof(row) = 16, pack_size(row{}) = 16
// is_fixed_serializable_v<row> = true
```

**The tradeoff:** The padding field costs extra bytes of RAM per row stored on-chain. In this example, 4 bytes per row. For tables with millions of rows this adds up. For small tables, singletons, or globals the cost is negligible.

| Approach | RAM per row | Serialization | Host calls per read |
|----------|------------|---------------|---------------------|
| No padding (datastream fallback) | Compact — only field bytes | Field-by-field encode/decode | 1-2 (size probe + read if value > stack buffer) |
| Explicit padding (zero-copy) | +N padding bytes | Single `memcpy` | 1 (exact `sizeof(T)` buffer) |

**When zero-copy matters most:** Contracts that do heavy iteration (scanning tables, bulk reads in a loop) benefit from eliminating per-field serialization and guaranteeing a single host call per row. Contracts that mostly do point lookups on small tables will see negligible difference — the host call overhead dominates either way.

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

### From multi\_index to kv::table (no secondary indices)

If your contract uses `multi_index` but does not need secondary indices:

1. Change `#include <sysio/multi_index.hpp>` to `#include <sysio/kv_table.hpp>`
2. Replace `multi_index<"tbl"_n, T>` with `kv::table<"tbl"_n, T>`
3. The `emplace`/`modify`/`erase`/`find` API is the same
4. `get()` returns `T` by value instead of `const T&` (no object caching)
5. Bonus: `begin_all_scopes()` for cross-scope iteration

### From singleton to kv::global (no scope needed)

If your singleton stores contract-wide config that doesn't vary by scope:

1. Change `#include <sysio/singleton.hpp>` to `#include <sysio/kv_global.hpp>`
2. Replace `singleton<"cfg"_n, T> cfg(get_self(), get_self().value)` with `kv::global<"cfg"_n, T> cfg(get_self())`
3. Same API: `set`, `get`, `exists`, `remove`, `get_or_default`, `get_or_create`
4. No scope parameter -- simpler and 16 bytes less key overhead per entry

### From raw\_table to indexed\_table

If you started with `raw_table` and need secondary indices:

1. Change `#include <sysio/kv_raw_table.hpp>` to `#include <sysio/kv_indexed_table.hpp>`
2. Replace `raw_table<K, V>` with `indexed_table<"name"_n, K, V, kv::index<...>>`
3. Replace `set(key, value)` with `emplace(key, value)` for new rows and `modify(it, value)` for updates
4. Note: `indexed_table` has no raw `set()` -- this prevents secondary index corruption
5. Existing format=0 data is compatible (same key encoding). Add secondary indices by re-inserting rows

---

## Why Upgrade from multi\_index

`sysio::multi_index` is provided for backward compatibility with existing EOSIO contracts. It works correctly but carries overhead that the newer APIs avoid. For new contracts, prefer `kv::table`, `kv::indexed_table`, `kv::global`, or `kv::raw_table`.

**Benefits of upgrading:**

| | multi\_index | New APIs |
|---|---|---|
| **Heap allocations per operation** | `std::map` cache + `std::unique_ptr` per row + `std::vector<char>` for serialization | Zero for trivially-copyable types; stack-first for others |
| **Template instantiation cost** | Pulls in `<map>`, `<memory>`, `<vector>` | Minimal headers; `kv::global` needs only `<cstring>` |
| **Object caching** | `std::map` cache grows unbounded within an action | No cache; each read goes to KV (fast with OC runtime) |
| **Serialization for POD types** | Runtime `if (sizeof == pack_size)` check | Compile-time `is_fixed_serializable_v` -- zero branching, exact `sizeof(T)` buffer |
| **Host calls per read** | 2 (probe size + read) on cache miss | 1 for trivially-copyable; 1-2 for others (stack-buffer-first) |
| **Host calls per iteration step** | 2 (key read + value read) on cache miss | 1-2 (stack-buffer-first for both key and value) |
| **Key overhead** | 24B fixed (format=1) | 24B (`kv::table`) or as small as needed (`indexed_table`, `raw_table`, `kv::global` = 8B) |
| **Cross-scope iteration** | Not possible | `kv::table::begin_all_scopes()` |
| **Key-only secondary iteration** | Not possible | `indexed_table::key_begin()` / `key_end()` |
| **Iterator safety** | `const T*` into cache -- invalidated by erase on copied iterator | `T` by value in iterator -- each copy is independent |

**When to keep multi\_index:**

- Drop-in compatibility is required (no code changes)
- Contract relies on object caching semantics (holding `const T&` across multiple `get()` calls on the same table instance)
- Contract uses `iterator_to()` to map a cached object back to an iterator
