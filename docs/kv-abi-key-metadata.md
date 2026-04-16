# KV Table Key Metadata in ABI

## Overview

The generated ABI includes key layout metadata in `key_names`, `key_types`, and `table_id` fields for each table entry. This allows SHiP clients (e.g., Hyperion) to decode raw KV key bytes and route deltas to the correct table.

## table\_id

Each table gets a unique `table_id` (uint16) computed from the template parameter via DJB2 hash. The `table_id` is passed as the first parameter to all KV intrinsics and appears in the ABI:

```json
{
   "name": "accounts",
   "type": "account",
   "table_id": 25660,
   "key_names": ["scope", "primary_key"],
   "key_types": ["name", "uint64"]
}
```

## Auto-Generated Key Metadata

All table ABI entries are auto-generated from template parameters — **no annotations required** for `_n` tables.

### multi\_index / singleton

CDT auto-generates the standard 16-byte scoped key layout:

```json
"key_names": ["scope", "primary_key"],
"key_types": ["name", "uint64"]
```

### kv::table

For `kv::table<Name, K, V>`, CDT auto-derives `key_names` and `key_types` from the K template parameter's fields:

```cpp
struct order_key {
   std::string region;
   uint64_t    seq;
   SYSLIB_SERIALIZE(order_key, (region)(seq))
};

struct order_val {
   uint64_t amount;
   SYSLIB_SERIALIZE(order_val, (amount))
};

// No annotations — everything derived from template params
using orders = kv::table<"orders"_n, order_key, order_val>;
```

ABI output:
```json
{
   "name": "orders",
   "type": "order_val",
   "table_id": 12345,
   "key_names": ["region", "seq"],
   "key_types": ["string", "uint64"]
}
```

### kv::global

CDT auto-generates a single 8-byte name key:

```json
"key_names": ["name"],
"key_types": ["name"]
```

## When Annotations Are Needed

| Scenario | Annotation | Why |
|----------|-----------|-----|
| `_n` table, default key metadata | None | Auto-derived from template |
| `_i` table (long name) | `[[sysio::table("long_name")]]` on V | DJB2 hash can't reverse to string |
| Override key field names in ABI | `[[sysio::kv_key("alt_struct")]]` on V | Use different names than K's fields |
| Both long name + override | Both annotations on V | |

### Override example

`[[sysio::kv_key]]` overrides the auto-derived key metadata when you want the ABI to expose different field names than the actual key struct:

```cpp
struct real_key { uint64_t category; uint64_t id; SYSLIB_SERIALIZE(real_key, (category)(id)) };
struct abi_key  { uint64_t cat; uint64_t item_id; SYSLIB_SERIALIZE(abi_key, (cat)(item_id)) };

struct [[sysio::table("items"), sysio::kv_key("abi_key")]] item_val {
   std::string name;
   SYSLIB_SERIALIZE(item_val, (name))
};

using items = kv::table<"items"_n, real_key, item_val>;
// ABI key_names: ["cat", "item_id"] (from abi_key, NOT real_key)
```

## The `_i` Literal and Long Table Names

Table names can exceed 13 characters using `_i`:

```cpp
kv::table<"user_preferences"_i, pref_key, pref_val> prefs(get_self());
```

Requires `[[sysio::table("user_preferences")]]` on the value struct so CDT can emit the human-readable name in the ABI.

## Key Encoding

All KV keys use big-endian encoding. The `table_id` is **not** part of the key bytes — it is passed separately to intrinsics.

### Legacy multi_index layout (16 bytes)

| Offset | Size | Field |
|--------|------|-------|
| 0 | 8B | `scope` (BE uint64) |
| 8 | 8B | `primary_key` (BE uint64) |

### Custom layout (kv::table)

Fields concatenated in SYSLIB_SERIALIZE order:

| Type | Encoding |
|------|----------|
| `uint8/16/32/64` | Big-endian |
| `int8/16/32/64` | Sign-flip + BE |
| `double` / `float` | IEEE754 sign-flip |
| `string` | NUL-escaped + terminated |
| `name` | 8B big-endian |
| `bool` | 1 byte (0 or 1) |
