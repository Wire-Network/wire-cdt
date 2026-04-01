# Wire KV Intrinsics Reference

This document describes the 22 host functions (intrinsics) that implement the Wire KV database. These are declared in `<sysio/kv.h>` and implemented by the `nodeop` runtime.

Contract developers typically use the higher-level `sysio::multi_index` or `sysio::kv::table` APIs rather than calling these intrinsics directly. This reference is intended for CDT library authors, tooling developers, and anyone who needs to understand the low-level interface.

---

## General Notes

### key\_format parameter

Several intrinsics accept a `key_format` parameter:

| Value | Meaning |
|-------|---------|
| 0 | Raw key -- arbitrary bytes, no special encoding assumed |
| 1 | Standard 24-byte key -- `[table:8B BE][scope:8B BE][pk:8B BE]`. Enables SSO fast-path and SHiP translation |

The `multi_index` and `kv::table` APIs pass `key_format=1` (standard 24-byte keys). The `kv::raw_table` API passes `key_format=0` (raw keys with variable-length encoding).

### payer parameter

The `payer` parameter on `kv_set`:

| Value | Meaning |
|-------|---------|
| 0 | RAM is charged to the executing contract account (normal case) |
| nonzero | RAM is charged to the specified account. Payer must have authorized the action or net RAM usage must not increase |

### Limits

| Resource | Limit |
|----------|-------|
| Maximum key size | 256 bytes |
| Maximum value size | 256 KiB (262,144 bytes) |
| Simultaneous primary iterator handles | 16 per action |
| Simultaneous secondary iterator handles | 16 per action |
| Maximum secondary key size | 256 bytes |
| Maximum primary key size (secondary index) | 256 bytes |

### Iterator status codes

Primary iterators (`kv_it_*`) and secondary iterators (`kv_idx_*`) return status codes:

| Code | Meaning |
|------|---------|
| 0 | OK -- iterator points to a valid entry |
| 1 | End -- iterator is past the last entry in the prefix/index range |
| 2 | Erased -- the entry the iterator pointed to was deleted |

---

## 1. Primary Operations

### kv\_set

Store or update a key-value pair.

```c
int64_t kv_set(uint32_t key_format, uint64_t payer,
               const void* key, uint32_t key_size,
               const void* value, uint32_t value_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key_format` | `uint32_t` | Key encoding format (0=raw, 1=standard 24-byte) |
| `payer` | `uint64_t` | RAM payer account (0=self) |
| `key` | `const void*` | Pointer to key bytes |
| `key_size` | `uint32_t` | Length of key in bytes (max 256) |
| `value` | `const void*` | Pointer to value bytes |
| `value_size` | `uint32_t` | Length of value in bytes (max 256 KiB) |

**Returns:** `int64_t` -- RAM byte delta. Positive for growth (new key or larger value), zero for same-size update, negative if value shrunk.

**Behavior:**
- If the key does not exist, creates a new entry
- If the key exists, replaces the entire value
- RAM usage delta is applied to the payer account

**Error conditions:**
- `key_size > 256`: aborts with "key too large"
- `value_size > 262144`: aborts with "value too large"
- Nonzero `payer` without payer authorization: aborts with `unauthorized_ram_usage_increase` if net RAM increases

---

### kv\_get

Read a value by key.

```c
int32_t kv_get(uint32_t key_format, uint64_t code,
               const void* key, uint32_t key_size,
               void* value, uint32_t value_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key_format` | `uint32_t` | 0 for raw keys (kv::raw\_table), 1 for standard 24-byte keys (multi\_index / kv::table). Determines which index partition to query. |
| `code` | `uint64_t` | Contract account to read from (allows cross-contract reads) |
| `key` | `const void*` | Pointer to key bytes |
| `key_size` | `uint32_t` | Length of key |
| `value` | `void*` | Output buffer for the value |
| `value_size` | `uint32_t` | Size of the output buffer |

**Returns:** `int32_t` -- Actual value size in bytes, or `-1` if the key was not found.

**Behavior:**
- Reads the value associated with `key` from the specified contract's storage
- If `value_size` is smaller than the actual value, only `value_size` bytes are written to the buffer. The return value still reports the full size, allowing the caller to allocate a larger buffer and retry.
- Passing `value=nullptr, value_size=0` is valid and returns the size without copying data (size probe)
- Cross-contract reads are allowed: any contract can read another contract's KV data by specifying its account as `code`

**Error conditions:**
- None (returns -1 for missing keys rather than aborting)

---

### kv\_erase

Delete a key-value pair.

```c
int64_t kv_erase(uint32_t key_format, const void* key, uint32_t key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key_format` | `uint32_t` | 0 for raw keys (kv::raw\_table), 1 for standard 24-byte keys (multi\_index / kv::table). Determines which index partition to query. |
| `key` | `const void*` | Pointer to key bytes |
| `key_size` | `uint32_t` | Length of key |

**Returns:** `int64_t` -- Negative RAM byte delta (bytes freed).

**Behavior:**
- Removes the key-value pair from the executing contract's storage
- Any iterators pointing to the erased key become invalid (status = 2/erased)

**Error conditions:**
- Key not found: aborts with "key not found"
- Cannot erase keys belonging to other contracts

---

### kv\_contains

Check if a key exists.

```c
int32_t kv_contains(uint32_t key_format, uint64_t code,
                    const void* key, uint32_t key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key_format` | `uint32_t` | 0 for raw keys (kv::raw\_table), 1 for standard 24-byte keys (multi\_index / kv::table). Determines which index partition to query. |
| `code` | `uint64_t` | Contract account to check |
| `key` | `const void*` | Pointer to key bytes |
| `key_size` | `uint32_t` | Length of key |

**Returns:** `int32_t` -- `1` if the key exists, `0` otherwise.

**Behavior:**
- Lightweight existence check without reading the value
- Cross-contract reads allowed (same as `kv_get`)

**Error conditions:**
- None

---

## 2. Primary Iterators

Primary iterators traverse KV entries matching a key prefix. They are used to implement table scans, range queries, and ordered iteration.

### kv\_it\_create

Create a new iterator over keys matching a prefix.

```c
uint32_t kv_it_create(uint32_t key_format, uint64_t code,
                      const void* prefix, uint32_t prefix_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `key_format` | `uint32_t` | 0 for raw keys (kv::raw\_table), 1 for standard 24-byte keys (multi\_index / kv::table). Determines which index partition to query. |
| `code` | `uint64_t` | Contract account to iterate over |
| `prefix` | `const void*` | Key prefix bytes. Empty prefix (`prefix_size=0`) iterates all keys |
| `prefix_size` | `uint32_t` | Length of prefix |

**Returns:** `uint32_t` -- Iterator handle (0..15).

**Behavior:**
- Creates a prefix-scoped iterator positioned at the first key that starts with `prefix`
- If no keys match the prefix, the iterator starts at end (status = 1)
- The CDT APIs use a 16-byte prefix (`[table:8B][scope:8B]`) to scope iteration to a single table+scope

**Error conditions:**
- More than 16 simultaneous iterators: aborts with "too many iterators"

---

### kv\_it\_destroy

Destroy an iterator and free its slot.

```c
void kv_it_destroy(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle to destroy |

**Behavior:**
- Frees the iterator slot for reuse
- Must be called for every created iterator to avoid exhausting the pool

**Error conditions:**
- Invalid handle: aborts

---

### kv\_it\_status

Query the current status of an iterator.

```c
int32_t kv_it_status(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |

**Returns:** `int32_t` -- Status code (0=OK, 1=end, 2=erased).

---

### kv\_it\_next

Advance the iterator to the next key within the prefix range.

```c
int32_t kv_it_next(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |

**Returns:** `int32_t` -- Status after advancing (0=OK, 1=end).

**Behavior:**
- Moves to the next key in lexicographic order that still matches the prefix
- If already at the last matching key, moves to end (returns 1)

---

### kv\_it\_prev

Move the iterator to the previous key within the prefix range.

```c
int32_t kv_it_prev(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |

**Returns:** `int32_t` -- Status after retreating (0=OK, 1=end).

**Behavior:**
- Moves to the previous key in lexicographic order that still matches the prefix
- If at the first matching key, returns 1 (end/before-begin)
- If at end, moves to the last matching key

---

### kv\_it\_lower\_bound

Seek the iterator to the lower bound of a key within the prefix range.

```c
int32_t kv_it_lower_bound(uint32_t handle, const void* key, uint32_t key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |
| `key` | `const void*` | Seek key bytes |
| `key_size` | `uint32_t` | Length of seek key |

**Returns:** `int32_t` -- Status after seeking (0=OK, 1=end).

**Behavior:**
- Positions the iterator at the first key >= `key` that matches the iterator's prefix
- If no such key exists, positions at end (returns 1)

---

### kv\_it\_key

Read the current key from the iterator.

```c
int32_t kv_it_key(uint32_t handle, uint32_t offset,
                  void* dest, uint32_t dest_size,
                  uint32_t* actual_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |
| `offset` | `uint32_t` | Byte offset into the key (for partial reads) |
| `dest` | `void*` | Output buffer |
| `dest_size` | `uint32_t` | Size of output buffer |
| `actual_size` | `uint32_t*` | [out] Actual total key size |

**Returns:** `int32_t` -- Status code (0=OK, nonzero=invalid position).

**Behavior:**
- Copies up to `dest_size` bytes of the key (starting from `offset`) into `dest`
- Writes the total key size to `*actual_size` regardless of how many bytes were copied
- The `offset` parameter enables reading large keys in chunks

---

### kv\_it\_value

Read the current value from the iterator.

```c
int32_t kv_it_value(uint32_t handle, uint32_t offset,
                    void* dest, uint32_t dest_size,
                    uint32_t* actual_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Iterator handle |
| `offset` | `uint32_t` | Byte offset into the value |
| `dest` | `void*` | Output buffer |
| `dest_size` | `uint32_t` | Size of output buffer |
| `actual_size` | `uint32_t*` | [out] Actual total value size |

**Returns:** `int32_t` -- Status code (0=OK, nonzero=invalid position).

**Behavior:**
- Same semantics as `kv_it_key` but reads the value instead of the key

---

## 3. Secondary Index Operations

Secondary indices map secondary keys to primary keys. They are used by `sysio::multi_index` to implement `indexed_by` / `get_index<>()`. Each secondary index is identified by a `(table, index_id)` pair.

Secondary iterators use a separate handle pool from primary iterators (16 handles each).

### kv\_idx\_store

Store a secondary index entry.

```c
void kv_idx_store(uint64_t payer, uint64_t table, uint32_t index_id,
                  const void* pri_key, uint32_t pri_key_size,
                  const void* sec_key, uint32_t sec_key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `payer` | `uint64_t` | Account to bill for RAM (0 = receiver) |
| `table` | `uint64_t` | Logical table name (e.g., `"accounts"_n.value`) |
| `index_id` | `uint32_t` | Index identifier (0-255, corresponding to the Nth `indexed_by`) |
| `pri_key` | `const void*` | Primary key bytes (`[scope:8B][pk:8B]` = 16 bytes in CDT) |
| `pri_key_size` | `uint32_t` | Primary key size (max 256) |
| `sec_key` | `const void*` | Secondary key bytes (big-endian encoded) |
| `sec_key_size` | `uint32_t` | Secondary key size (max 256) |

**Behavior:**
- Inserts a (secondary\_key, primary\_key) pair into the index
- Multiple entries with the same secondary key are allowed (sorted by primary key)
- RAM is charged to the specified payer (same account as the primary row's payer)
- The payer is stored on the index entry for correct refunds on removal

**Error conditions:**
- Duplicate (sec\_key, pri\_key) pair: aborts

---

### kv\_idx\_remove

Remove a secondary index entry.

```c
void kv_idx_remove(uint64_t table, uint32_t index_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* sec_key, uint32_t sec_key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `table` | `uint64_t` | Logical table name |
| `index_id` | `uint32_t` | Index identifier |
| `pri_key` | `const void*` | Primary key bytes |
| `pri_key_size` | `uint32_t` | Primary key size |
| `sec_key` | `const void*` | Secondary key bytes |
| `sec_key_size` | `uint32_t` | Secondary key size |

**Behavior:**
- Removes the exact (sec\_key, pri\_key) pair from the index
- RAM is refunded to the stored payer (the account that was billed on `kv_idx_store`)
- Any iterators pointing to the removed entry become invalid

**Error conditions:**
- Entry not found: aborts

---

### kv\_idx\_update

Update a secondary index entry (change the secondary key, keep the primary key).

```c
void kv_idx_update(uint64_t payer, uint64_t table, uint32_t index_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `payer` | `uint64_t` | Account to bill for RAM (0 = receiver) |
| `table` | `uint64_t` | Logical table name |
| `index_id` | `uint32_t` | Index identifier |
| `pri_key` | `const void*` | Primary key bytes (unchanged) |
| `pri_key_size` | `uint32_t` | Primary key size |
| `old_sec_key` | `const void*` | Current secondary key bytes |
| `old_sec_key_size` | `uint32_t` | Current secondary key size |
| `new_sec_key` | `const void*` | New secondary key bytes |
| `new_sec_key_size` | `uint32_t` | New secondary key size |

**Behavior:**
- Atomically removes the old (old\_sec\_key, pri\_key) entry and inserts (new\_sec\_key, pri\_key)
- If payer changed from stored payer: refunds old payer full amount, charges new payer full amount
- If same payer: charges/refunds only the size delta
- If old\_sec\_key == new\_sec\_key, this is a no-op (called by CDT, skipped at CDT layer)
- More efficient than separate remove + store calls

**Error conditions:**
- Old entry not found: aborts

---

### kv\_idx\_find\_secondary

Find an exact secondary key match. Returns an iterator handle.

```c
uint32_t kv_idx_find_secondary(uint64_t code, uint64_t table, uint32_t index_id,
                               const void* sec_key, uint32_t sec_key_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `code` | `uint64_t` | Contract account to search |
| `table` | `uint64_t` | Logical table name |
| `index_id` | `uint32_t` | Index identifier |
| `sec_key` | `const void*` | Secondary key bytes to find |
| `sec_key_size` | `uint32_t` | Secondary key size |

**Returns:** `uint32_t` -- Secondary iterator handle. Check status to determine if a match was found.

**Behavior:**
- Positions the iterator at the first entry with an exact match on `sec_key`
- If no match, iterator is at end (status = 1)
- Cross-contract reads allowed

---

### kv\_idx\_lower\_bound

Find the lower bound on a secondary key.

```c
uint32_t kv_idx_lower_bound(uint64_t code, uint64_t table, uint32_t index_id,
                            const void* sec_key, uint32_t sec_key_size);
```

**Parameters:** Same as `kv_idx_find_secondary`.

**Returns:** `uint32_t` -- Secondary iterator handle positioned at the first entry with sec\_key >= the given key.

**Behavior:**
- Positions at the first entry whose secondary key is >= `sec_key`
- If no such entry exists, positioned at end
- Passing `sec_key=nullptr, sec_key_size=0` positions at the very first entry in the index

---

### kv\_idx\_next

Advance the secondary iterator to the next entry.

```c
int32_t kv_idx_next(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Secondary iterator handle |

**Returns:** `int32_t` -- Status after advancing (0=OK, 1=end).

**Behavior:**
- Moves to the next entry in secondary key order
- Entries with the same secondary key are ordered by primary key

---

### kv\_idx\_prev

Move the secondary iterator to the previous entry.

```c
int32_t kv_idx_prev(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Secondary iterator handle |

**Returns:** `int32_t` -- Status after retreating (0=OK, 1=end).

---

### kv\_idx\_key

Read the current secondary key from the iterator.

```c
int32_t kv_idx_key(uint32_t handle, uint32_t offset,
                   void* dest, uint32_t dest_size,
                   uint32_t* actual_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Secondary iterator handle |
| `offset` | `uint32_t` | Byte offset into the secondary key |
| `dest` | `void*` | Output buffer |
| `dest_size` | `uint32_t` | Size of output buffer |
| `actual_size` | `uint32_t*` | [out] Actual total secondary key size |

**Returns:** `int32_t` -- Status code (0=OK).

---

### kv\_idx\_primary\_key

Read the primary key associated with the current secondary index entry.

```c
int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset,
                           void* dest, uint32_t dest_size,
                           uint32_t* actual_size);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Secondary iterator handle |
| `offset` | `uint32_t` | Byte offset into the primary key |
| `dest` | `void*` | Output buffer |
| `dest_size` | `uint32_t` | Size of output buffer |
| `actual_size` | `uint32_t*` | [out] Actual total primary key size |

**Returns:** `int32_t` -- Status code (0=OK).

**Behavior:**
- In the CDT multi\_index implementation, the stored primary key is `[scope:8B][pk:8B]` = 16 bytes
- The caller extracts the 8-byte primary key from offset 8

---

### kv\_idx\_destroy

Destroy a secondary iterator and free its slot.

```c
void kv_idx_destroy(uint32_t handle);
```

**Parameters:**

| Name | Type | Description |
|------|------|-------------|
| `handle` | `uint32_t` | Secondary iterator handle to destroy |

**Behavior:**
- Frees the secondary iterator slot for reuse
- Must be called for every created secondary iterator

---

## Source Files

| File | Description |
|------|-------------|
| `libraries/sysiolib/capi/sysio/kv.h` | C API declarations (CDT side) |
| `libraries/sysiolib/contracts/sysio/kv_table.hpp` | `sysio::kv::table` implementation |
| `libraries/sysiolib/contracts/sysio/kv_multi_index.hpp` | `sysio::multi_index` (KV-backed) implementation |
| `libraries/sysiolib/contracts/sysio/kv_singleton.hpp` | `sysio::singleton` (KV-backed) implementation |
| `libraries/sysiolib/contracts/sysio/singleton.hpp` | Backward-compat alias to kv\_singleton |
| `libraries/sysiolib/contracts/sysio/multi_index.hpp` | Backward-compat alias to kv\_multi\_index |

On the node side (wire-sysio repository):

| File | Description |
|------|-------------|
| `libraries/chain/include/sysio/chain/webassembly/interface.hpp` | Host function declarations |
| `libraries/chain/apply_context.cpp` | KV intrinsic implementations |
| `libraries/chain/include/sysio/chain/kv_table_objects.hpp` | KV storage objects (chainbase) |
| `libraries/chain/webassembly/kv_database.cpp` | WASM-to-native bridge |
