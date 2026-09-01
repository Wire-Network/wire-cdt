# Wire KV Storage Guide

Wire uses a key-value database for smart contract state, replacing EOSIO's `db_*_i64` host functions with KV intrinsics. Each table gets a unique `table_id` (uint16, DJB2 hash) that isolates its keyspace.

## API Lineup

### Recommended for new contracts

| Type | Use Case | Header |
|------|----------|--------|
| [`kv::table`](kv-table.md) | Multiple rows, custom keys, optional secondary indices | `<sysio/kv_table.hpp>` |
| [`kv::scoped_table`](kv-scoped-table.md) | Scoped rows (like multi_index, but faster) | `<sysio/kv_scoped_table.hpp>` |
| [`kv::global`](kv-global.md) | Single value per contract (config, counters) | `<sysio/kv_global.hpp>` |

### Backward compatibility

| Type | Use Case | Header |
|------|----------|--------|
| [`multi_index`](kv-multi-index.md) | Source-compatible EOSIO shim (scoped, uint64 pk) | `<sysio/multi_index.hpp>` |
| `singleton` | Scoped single value | `<sysio/singleton.hpp>` |

## Decision Matrix

| Feature | `kv::table` | `kv::scoped_table` | `kv::global` | `multi_index` |
|---------|-------------|---------------------|--------------|---------------|
| Key type | User-defined struct | User-defined struct | Fixed (name) | `uint64_t` |
| Multiple rows | Yes | Yes | No (single value) | Yes |
| Secondary indices | Optional (up to 16) | Optional (up to 16) | No | Up to 16 |
| Scope | No (table_id isolation) | Yes (required) | No | Yes |
| Key layout | `[K encoded]` | `[scope:8B][K encoded]` | `[name:8B]` | `[scope:8B][pk:8B]` |
| Long table names (`_i`) | Yes | Yes | Not usable — see below | No (`_n` only) |
| Zero-copy | Yes (trivially_copyable) | Yes | Yes | Yes |
| Lambda emplace | Yes | Yes | No | Yes |
| auto-increment PK | Yes (`primary_key()`) | Yes (`primary_key()`) | No | `available_primary_key()` |
| Scope iteration | N/A | `scope_lower_bound()` | N/A | `get_table_by_scope` RPC |
| Object cache | No | No | No | Yes (overhead) |

## table\_id Namespace Isolation

Each table and each secondary index gets a unique `table_id` computed at compile time:

```cpp
// Primary table:   table_id = compute_table_id("accounts"_n.value)  → 25660
// Secondary index: sec_table_id = compute_sec_table_id("accounts"_n, "byowner"_n) → 41023
```

The `table_id` is passed as the first parameter to all KV intrinsics. Keys no longer embed the table name — saving 8 bytes per row compared to the old 24-byte key layout.

## The `_i` Literal

For table names longer than 13 characters or with characters outside `a-z1-5.`:

```cpp
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>

kv::table<"user_balance_history"_i, my_key, my_val>  users(get_self());
```

The `_i` literal computes a DJB2 hash and returns `name::raw`. When using `_i`, annotate the value struct with `[[sysio::table("user_balance_history")]]` for ABI generation — **and keep the annotated name above 13 characters**, or abigen will describe the table under a different `table_id` than the one the rows use.

> **Not for `kv::global`.** A `_i`-named `kv::global` emits two ABI table entries — the annotated
> name under one `table_id`, and a decoded-hash name under the one the rows actually use — so a
> `get_table_rows` by the readable name finds nothing. Above 13 characters it fails to link with
> a `table_id collision`. Reads and writes through the contract are correct either way. Use `_n`
> for a config singleton; see
> [migrating-from-antelope.md](migrating-from-antelope.md#step-2--storage).

## Key Encoding

All keys use big-endian encoding for correct `memcmp`-based lexicographic ordering.

### Standard layout (multi\_index / singleton)

```
[scope:8B BE][primary_key:8B BE] = 16 bytes
```

### Custom layout (kv::table)

Fields serialized via `be_key_stream` in `SYSLIB_SERIALIZE` order:

| Type | Size | Encoding |
|------|------|----------|
| `uint8/16/32/64` | 1/2/4/8B | Big-endian |
| `int8/16/32/64` | 1/2/4/8B | Sign-flip + BE |
| `double`/`float` | 8/4B | IEEE754 sign-flip |
| `string` | variable | NUL-escape + terminator |
| `name` | 8B | Big-endian |
| `bool` | 1B | 0 or 1 |

**Key buffer limit:** The `be_key_stream` encoder uses a 256-byte stack buffer (`KV_KEY_BUF_CAP`). Keys exceeding this size abort with `"be_key_stream: key too large"`. For most use cases 256 bytes is more than sufficient (e.g., 32 `uint64_t` fields = 256 bytes). If you need larger keys, define `KV_KEY_BUF_CAP` before including any KV header:

```cpp
#define KV_KEY_BUF_CAP 512
#include <sysio/kv_table.hpp>
```

Note: the chain enforces a separate maximum key size (`max_kv_key_size`, default 256 bytes). Increasing `KV_KEY_BUF_CAP` beyond the chain limit has no effect — the intrinsic will reject the key.

### Global layout

```
[name:8B BE] = 8 bytes
```

## Choosing a Primary Key When Using Secondary Indexes

Every secondary-index row stores a copy of the primary key bytes as a reference back to the primary row. The smaller the primary key, the cheaper each secondary row.

**Default rule:** the primary key should be the smallest unique identifier for the row. For most tables that means a `uint64`.

### Picking the primary

**Have a uint64 natural unique identifier already (name, id, etc.)?**

Use it. Add secondaries for the other lookups you need.

**Have only larger natural unique candidates (strings, composites)?**

You can either keep the natural field as the primary or switch to a `uint64` surrogate and demote the natural field to a secondary.

Switching to a surrogate has two effects per primary row:

- **It saves** `(natural_size - 8)` bytes on every secondary row, because secondaries now point at an 8-byte surrogate instead of the full natural key.
- **It costs** about 128 bytes for the one extra secondary you now need (to look up by the natural field that used to be the primary).

The surrogate becomes the cheaper layout once the per-row savings on the existing secondaries outweigh the cost of the one added secondary. Concrete break-even points:

| Number of lookup paths total | Natural primary stays cheaper if its size is at most |
|---|---|
| 2 | ~136 B |
| 3 | ~72 B |
| 4 | ~50 B |
| 5 | ~40 B |
| 10 | ~22 B |
| 20+ | always switch to surrogate |

When in doubt, go with the surrogate - it is harder to be surprised by future growth (each new secondary makes the surrogate relatively cheaper).

**Have no natural unique identifier at all (log, queue, append-only)?**

Introduce a surrogate `uint64` - a sequence counter, hash-truncated id, or auto-incrementing identifier. It is the smallest possible primary, guarantees uniqueness, future-proofs against later secondaries, and gives you a stable handle for cross-table references.

Do **not** pick an arbitrary larger field "because it's there." The primary key is duplicated onto every secondary row, so picking a 30 B field with no query reason costs you 30 bytes per (row * sec index) plus the field on the primary itself.

### Scaling

Extra RAM versus the 8 B uint64 baseline grows linearly in row count, secondary index count, and primary key size. At 1M rows with 3 secondary indexes:

| `pri_key_size` | Extra sec RAM |
|----------------|---------------|
| 8 B (uint64) | baseline |
| 32 B | ~72 MB |
| 64 B | ~168 MB |
| 128 B | ~360 MB |

## Zero-Copy Serialization

When `V` is `trivially_copyable` and `sizeof(V) == pack_size(V)`, the value is stored and retrieved via direct `memcpy` — no datastream encoding. This eliminates serialization overhead for POD structs:

```cpp
struct config {
   uint64_t rate;    // 8 bytes
   uint32_t flags;   // 4 bytes
   // sizeof = 12, pack_size = 12 → zero-copy
   SYSLIB_SERIALIZE(config, (rate)(flags))
};
static_assert(kv::is_fixed_serializable_v<config>);
```

**Caution:** Adding `std::string` or `std::vector` fields disables zero-copy.

## ABI Output

Each table entry in the ABI includes:

```json
{
   "name": "accounts",
   "type": "account",
   "table_id": 25660,
   "key_names": ["scope", "primary_key"],
   "key_types": ["name", "uint64"]
}
```

For custom keys via `[[sysio::kv_key("key_struct")]]`, `key_names`/`key_types` reflect the key struct's fields. See [ABI Key Metadata](kv-abi-key-metadata.md).

## Migration from multi\_index

> Porting a contract from EOS/Telos/WAX or another Antelope chain? Start with
> [Migrating a Contract from an Antelope Chain to Wire](migrating-from-antelope.md). `multi_index` is
> a compatibility shim rather than a drop-in — it needs a mechanical `it++` → `++it` sweep, since the
> postfix iterator operators are deleted — so make those adjustments first; this section is the
> optional second step.

`multi_index` continues to work. To migrate to `kv::table`:

1. Define a key struct with your primary key fields + `SYSLIB_SERIALIZE`
2. Define your value struct with `SYSLIB_SERIALIZE`
3. Replace `multi_index<"name"_n, T, ...>` with `kv::table<"name"_n, K, V, ...>`
4. Replace `emplace(payer, [](T& r){ ... })` with `emplace(payer, key, value)` or lambda variant
5. Replace `it->field` with `it->field` (same! iterator returns V& directly)
6. Replace `it->primary_key()` with `it.key().pk_field`
7. Replace `modify(it, payer, lambda)` with `modify(payer, it, new_value)` or `modify(payer, key, lambda)`
