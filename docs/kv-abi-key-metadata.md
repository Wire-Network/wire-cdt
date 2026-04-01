# KV Table Key Metadata in ABI

## Overview

The generated ABI includes key layout metadata in the `key_names` and `key_types` fields of each table entry. This allows SHiP clients (e.g., Hyperion) to decode raw KV key bytes using the contract's ABI.

Key metadata is generated in two ways:

1. **Automatically** for tables used in `kv_multi_index`, `multi_index`, `singleton`, or `kv::table` templates — standard 24-byte key layout.
2. **Explicitly** via `[[sysio::kv_key("key_struct")]]` for tables backed by `kv::raw_table` — custom key layout from the named struct's fields.

## Automatic Key Metadata (kv\_multi\_index)

For tables used in a `kv_multi_index` (or `multi_index`) template:

```cpp
struct [[sysio::table]] account {
    asset    balance;
    uint64_t primary_key() const { return balance.symbol.code().raw(); }
};
typedef kv_multi_index<"accounts"_n, account> accounts_table;
```

CDT generates the standard 24-byte key layout:

```json
{
    "name": "accounts",
    "type": "account",
    "key_names": ["table_name", "scope", "primary_key"],
    "key_types": ["name", "name", "uint64"]
}
```

## Custom Key Metadata (\[\[sysio::kv\_key\]\])

For tables backed by `kv::raw_table` with custom key types, annotate the value struct:

```cpp
struct my_key {
    std::string region;
    uint64_t    id;
    SYSLIB_SERIALIZE(my_key, (region)(id))
};

struct [[sysio::table("geodata"), sysio::kv_key("my_key")]] my_value {
    std::string payload;
    uint64_t    amount;
    SYSLIB_SERIALIZE(my_value, (payload)(amount))
};

kv::raw_table<my_key, my_value> geodata;
```

CDT generates key metadata from the named struct's fields and adds the key struct to the ABI:

```json
{
    "name": "geodata",
    "type": "my_value",
    "key_names": ["region", "id"],
    "key_types": ["string", "uint64"]
}
```

Using `[[sysio::kv_key]]` without an argument produces the standard 24-byte layout (same as automatic).

## Key Encoding

All KV keys use big-endian encoding for correct `memcmp`-based ordering.

### Standard layout (kv\_multi\_index)

| Offset | Size | Field | Type |
|--------|------|-------|------|
| 0 | 8 bytes | `table_name` | `name` (BE uint64) |
| 8 | 8 bytes | `scope` | `name` (BE uint64) |
| 16 | 8 bytes | `primary_key` | `uint64` (BE) |

### Custom layout (kv::raw_table)

Fields are concatenated in `SYSLIB_SERIALIZE` declaration order:

| Type | Encoding |
|------|----------|
| `uint8` | 1 byte |
| `uint16` | 2 bytes BE |
| `uint32` | 4 bytes BE |
| `uint64` / `name` | 8 bytes BE |
| `uint128` | 16 bytes BE |
| `double` | 8 bytes (IEEE 754 with sign-flip for sort order) |
| `string` | Raw bytes + `0x00` terminator (no embedded nulls) |
| `bool` | 1 byte (0 or 1) |

## How SHiP Clients Use This

### Format=1 (`contract_row` delta)

SHiP pre-decomposes the 24-byte key into named fields (`table`, `scope`, `primary_key`). Clients receive these directly and decode the `value` using the table's `type` field from the ABI. Key metadata is informational.

### Format=0 (`contract_row_kv` delta)

SHiP sends `{code, payer, key, value}` with the raw key as opaque bytes. Clients:

1. Load the contract ABI
2. Find the table entry with populated `key_names`/`key_types`
3. Decode key fields as big-endian per the declared types
4. Decode the value using `abieos` with the table's `type` field

## Value Encoding

Values use standard ABI serialization (little-endian) for both format=0 and format=1. Clients decode them using `abieos` with the struct type from the table's `type` field.

## Backward Compatibility

The `key_names` and `key_types` fields have always been present in the ABI schema but were previously empty arrays. Populating them is backward-compatible — existing parsers that ignore these fields are unaffected.
