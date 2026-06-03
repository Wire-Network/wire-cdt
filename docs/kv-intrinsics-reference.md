# Wire KV Intrinsics Reference

22 host functions implementing the Wire KV database. Declared in `<sysio/kv.h>`, implemented by `nodeop`. Contract developers typically use higher-level APIs (`kv::table`, `multi_index`, `kv::global`).

## Primary KV Operations (5)

### kv\_set

```c
int64_t kv_set(uint32_t table_id, uint64_t payer,
               const void* key, uint32_t key_size,
               const void* value, uint32_t value_size);
```

Store a key-value pair. Returns RAM byte delta.

- **table_id** — table namespace identifier (lower 16 bits of the DJB2 hash of table name)
- **payer** — account to bill for RAM. `0` (`same_payer`) is valid only on update (existing key), where it keeps the row's existing payer; on insert (new key) there is no existing payer to keep, so the host rejects `0` with `invalid_table_payer` and the caller must name a paying account.

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

Erase a key-value pair. Returns negative RAM delta.

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
void kv_idx_store(uint64_t payer, uint32_t table_id,
                  const void* pri_key, uint32_t pri_key_size,
                  const void* sec_key, uint32_t sec_key_size);
```

Insert secondary index entry. `table_id` identifies the secondary index namespace.

- **payer** — account to bill for RAM. `kv_idx_store` always inserts, so a paying account must be named; `0` (`same_payer`) is rejected with `invalid_table_payer`.

### kv\_idx\_remove

```c
void kv_idx_remove(uint32_t table_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* sec_key, uint32_t sec_key_size);
```

### kv\_idx\_update

```c
void kv_idx_update(uint64_t payer, uint32_t table_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size);
```

Update a secondary index entry's key (operates on an existing entry).

- **payer** — account to bill for RAM. `0` (`same_payer`) keeps the entry's existing payer; pass a non-zero payer to bill that account explicitly.

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
