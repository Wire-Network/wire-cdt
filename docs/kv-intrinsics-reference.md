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

Erase a key-value pair. Returns the deleted primary row's chainbase id.
Callers with secondary indexes should call `kv_erase` BEFORE `kv_idx_remove`
and thread the returned id into each secondary removal.

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

`primary_id` must match the id stored when the secondary row was inserted.
Obtain it from the preceding `kv_erase` (erase primary first), or cache it
from a prior `kv_set`.

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
