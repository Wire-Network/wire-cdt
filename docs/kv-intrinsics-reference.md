# Wire KV Intrinsics Reference

22 host functions implementing the Wire KV database. Declared in `<sysio/kv.h>`, implemented by `nodeop`. Contract developers typically use higher-level APIs (`kv::table`, `multi_index`, `kv::global`).

## Primary KV Operations (5)

### kv\_set

```c
int64_t kv_set(uint32_t table_id, uint64_t payer,
               const void* key, uint32_t key_size,
               const void* value, uint32_t value_size);
```

Store or update a key-value pair. Returns the primary row's chainbase id.
Thread the returned id into `kv_idx_store`/`kv_idx_update` so secondary rows
reference the primary by id.

- **table_id** — table namespace identifier (lower 16 bits of the DJB2 hash of table name)
- **payer** — account to bill for RAM (0 = receiver)

### kv\_get

```c
int32_t kv_get(uint32_t table_id, uint64_t code,
               const void* key, uint32_t key_size,
               void* value, uint32_t value_size);
```

Read value by key. Returns actual value size, or -1 if not found.

### kv\_erase

```c
int64_t kv_erase(uint32_t table_id, const void* key, uint32_t key_size);
```

Erase a key-value pair. Returns the deleted primary row's chainbase id. The id
is captured before the `kv_object` is removed, so it remains a valid lookup
key for locating secondary rows that still reference the now-deleted primary.

**Required call order for tables with secondary indexes:**

1. `kv_erase(table_id, pk, pk_size)` → `primary_id`
2. One `kv_idx_remove(sec_table_id, primary_id, sec_key, sec_key_size)` per
   secondary index defined on the primary row.

Secondary rows are keyed by `(code, sec_table_id, sec_key, primary_id)`; the
contract cannot locate them until it learns `primary_id` from the erase return
value. This is the inverse of the pre-`primary_id` convention (which removed
secondaries first, then the primary) — pre-existing contracts that hand-call
the raw intrinsics must be updated.

The host does not cascade. Skipping step 2 leaves orphan `kv_index_object`
rows that still reference `primary_id`. Consequences:

- **RAM:** the sec row's RAM remains billed to its payer; `kv_erase` does not
  refund it.
- **Contract-side sec reads:** `kv_it_value` and `kv_idx_primary_key` on an
  iterator pointing at the orphan return `iterator_erased` (see the Read /
  iterator path table below) rather than aborting.
- **RPC `get_table_rows`** secondary scans abort on the missing primary —
  calls against the affected sec index fail loudly rather than silently
  skipping rows.
- **SHiP** serialization materializes `pri_key` from the primary and aborts
  on the missing row; deltas for the affected table cannot be emitted.
- **Recovery** is possible only if the contract (or an operator) can
  determine the leaked `primary_id` out of band; the host does not export a
  sec iterator's cached `primary_id`.

The `kv_multi_index` and `kv_table` wrappers in this CDT enforce the order
automatically; contracts built on the high-level APIs do not need to manage it.

### kv\_contains

```c
int32_t kv_contains(uint32_t table_id, uint64_t code,
                    const void* key, uint32_t key_size);
```

Test key existence. Returns 1 if exists, 0 otherwise.

### kv\_it\_create

```c
uint32_t kv_it_create(uint32_t table_id, uint64_t code,
                      const void* prefix, uint32_t prefix_size);
```

Create a forward iterator. Returns handle.

## Primary Iterator Operations (7)

```c
void    kv_it_destroy(uint32_t handle);
int32_t kv_it_status(uint32_t handle);       // 0=ok, 1=end, 2=begin
int32_t kv_it_next(uint32_t handle);
int32_t kv_it_prev(uint32_t handle);
int32_t kv_it_lower_bound(uint32_t handle, const void* key, uint32_t key_size);
int32_t kv_it_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
int32_t kv_it_value(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
```

## Secondary Index Operations (5)

### kv\_idx\_store

```c
void kv_idx_store(uint64_t payer, uint32_t table_id, int64_t primary_id,
                  const void* sec_key, uint32_t sec_key_size);
```

Insert a secondary index entry referencing a primary row by id. `table_id`
identifies the secondary index namespace. `primary_id` is the chainbase id
of the referenced primary row, returned by the preceding `kv_set`.

### kv\_idx\_remove

```c
void kv_idx_remove(uint32_t table_id, int64_t primary_id,
                   const void* sec_key, uint32_t sec_key_size);
```

Remove a secondary index entry. `primary_id` must match the id stored when
the secondary row was inserted; the composite `(code, table_id, sec_key,
primary_id)` tuple must exist or the call aborts with `kv_key_not_found`.
Obtain `primary_id` from the preceding `kv_erase` (erase primary first, per
the [ordering requirement](#kv_erase)), or cache it from a prior `kv_set`.
Call once per secondary index defined on the primary row.

### kv\_idx\_update

```c
void kv_idx_update(uint64_t payer, uint32_t table_id, int64_t primary_id,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size);
```

### kv\_idx\_find\_secondary

```c
int32_t kv_idx_find_secondary(uint64_t code, uint32_t table_id,
                              const void* sec_key, uint32_t sec_key_size);
```

Returns iterator handle >= 0, or -1 if not found.

### kv\_idx\_lower\_bound

```c
int32_t kv_idx_lower_bound(uint64_t code, uint32_t table_id,
                           const void* sec_key, uint32_t sec_key_size);
```

Returns handle >= 0, or -1 if table empty.

## Secondary Iterator Operations (5)

```c
int32_t kv_idx_next(uint32_t handle);
int32_t kv_idx_prev(uint32_t handle);
int32_t kv_idx_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
void    kv_idx_destroy(uint32_t handle);
```

## Design Notes

| Aspect | Description |
|--------|-------------|
| First param | `table_id` — uint16 namespace identifier (DJB2 hash of template parameter) |
| Secondary index ID | Single `table_id` per secondary index (replaces separate table + index_id params) |
| Standard key size | 16B `[scope:8B BE][pk:8B BE]` — table name conveyed by `table_id`, not in key |
| Table isolation | Each table and secondary index gets a unique `table_id` in the composite index |
| Total intrinsics | 22 (5 primary ops + 7 primary iterator + 5 secondary ops + 5 secondary iterator) |

## Error semantics

The host validates every intrinsic call and is robust to any argument a WASM (CDT-built or not) can pass. The split between "aborts the action" and "returns a sentinel" is intentional.

### Write path — aborts the action on bad input

| Intrinsic | Scenario | Host behavior |
|-----------|----------|---------------|
| `kv_set` | new key | Create row. Returns chainbase id (stable across subsequent in-place updates) |
| `kv_set` | existing key | Update in place via chainbase `modify`. **Returns the same id as the original insert** — secondary rows keyed by `primary_id` do not need to be rewritten |
| `kv_set` | oversized key/value | Aborts with `kv_key_too_large` / `kv_value_too_large` |
| `kv_set` / `kv_erase` | called in read-only transaction | Aborts with `table_operation_not_permitted` |
| `kv_erase` | key not found | Aborts with `kv_key_not_found` |
| `kv_idx_store` | `primary_id < 0` | Aborts with `kv_key_not_found` |
| `kv_idx_store` | `primary_id` references non-existent row | Aborts with `kv_key_not_found` |
| `kv_idx_store` | referenced primary owned by a different `code` | Aborts with `table_operation_not_permitted` (cross-contract protection) |
| `kv_idx_remove` | `(sec_key, primary_id)` tuple not present | Aborts with `kv_key_not_found` |
| `kv_idx_update` | old `(sec_key, primary_id)` tuple not present | Aborts with `kv_key_not_found` |

### Read / iterator path — returns sentinel, never aborts

| Intrinsic | Scenario | Host behavior |
|-----------|----------|---------------|
| `kv_get` | key not found | Returns `-1` |
| `kv_contains` | key not found | Returns `0` |
| `kv_idx_find_secondary` | not found | Returns `-1` |
| `kv_idx_lower_bound` | table empty | Returns `-1` |
| `kv_it_value` | slot not `iterator_ok` | Writes `actual_size = 0`, returns slot status |
| `kv_it_value` | secondary handle whose referenced primary has been deleted | Sets slot to `iterator_erased`, writes `actual_size = 0`, returns `iterator_erased` |
| `kv_idx_primary_key` | orphan secondary (primary deleted) | Sets slot to `iterator_erased`, writes `actual_size = 0`, returns `iterator_erased` |
| `kv_it_*` / `kv_idx_*` | invalid / freed handle | Aborts with `kv_invalid_iterator` (handle validation is unconditional) |
| `kv_idx_primary_key` | called with a primary-iterator handle | Aborts with `kv_invalid_iterator` |

### Iterator handle rules

- Handles are pool indices; every handle-accepting intrinsic validates `handle < pool_size && in_use` and aborts with `kv_invalid_iterator` if either fails.
- Primary vs secondary handles are distinct: passing a primary handle to a `kv_idx_*` intrinsic (or vice versa) aborts with `kv_invalid_iterator`.
- Exceeding the per-context iterator limit in `kv_it_create` / `kv_idx_find_secondary` / `kv_idx_lower_bound` aborts with `kv_iterator_limit_exceeded`.
- Chainbase ids are monotonic within a session and preserved across snapshot save/load (via `emplace_with_id`), so `primary_id` references cached in an iterator slot remain valid for as long as the referenced primary row exists.
