# Wire KV Intrinsics Reference

19 host functions implementing the Wire KV database. Declared in `<sysio/kv.h>`, implemented by `nodeop`. Contract developers typically use higher-level APIs (`kv::table`, `multi_index`, `kv::global`).

C++ contract code cannot include `<sysio/kv.h>` by that name, because `cdt-cpp` keeps the C API off a contract's include path. `<sysio/kv_utils.hpp>` brings the declarations in, and native builds and `cdt-cc` can include the header directly.

## Conventions

- **Parameter order:** `table_id`, then `code` for reads or `payer` for writes, then keys (primary before secondary), then values or output buffers, then out sizes. The `mode` of `kv_set` comes last, and the `offset` of `kv_get` and `kv_it_read` comes just before their buffers.
- **A state miss is returned and misuse throws.** Duplicate keys and missing rows come back as a status, and the wrapper raises the contract's own message. Bad arguments, size limits and the iterator limit still abort.
- **Reads take a `code`. Writes do not** and always land on the receiver.
- **`table_id`** is the low 16 bits of a DJB2 hash of the table name. A value above 65535 aborts. The chain does not enforce uniqueness; CDT codegen checks it within one ABI.
- **Payer:** `0` (`same_payer`) keeps the current payer when the row exists, and is rejected with `invalid_table_payer` when the call would create one. A payer other than the receiver must authorize with `sysio.payer`.
- **Limits:** key and secondary key sizes default to 256 bytes and values to 256 KiB. Governance can raise them to 512 bytes and 1 MiB. No row has an empty key, so an empty key aborts on every call that takes one. A `prefix` or a `seek` is not a key and may be empty.
- **Handles** are non-negative, valid within one receiver's execution, and limited to 1024 open per family. The lowest free slot is reused.

## Status

```c
enum kv_status : int32_t {
   kv_status_ok     = 0,   // the call succeeded, or the iterator is on a row
   kv_status_end    = 1,   // the iterator left its prefix range
   kv_status_no_row = 2,   // no row at that key
   kv_status_exists = 3    // a row already exists at that key
};
```

One enum serves every call. A call that returns a size or a handle uses `-1` for none instead. The enumerators carry the prefix so `ok` and `end` stay out of every contract's global namespace.

## Rows (3)

### kv\_set

```c
enum kv_set_mode : uint32_t { kv_set_upsert = 0, kv_set_insert = 1, kv_set_update = 2 };

int32_t kv_set(uint32_t table_id, uint64_t payer,
               const void* key, uint32_t key_size,
               const void* value, uint32_t value_size,
               uint32_t mode);
```

Create or replace a row of the receiver. Returns `ok`, `exists` (insert, row exists) or `no_row` (update, row missing). Any other mode aborts. Insert with payer 0 on an existing key returns `exists`; it aborts only on a missing key.

Marked `warn_unused_result`, as are the other four writes: dropping the status turns a failed write into a silent success.

### kv\_erase

```c
int32_t kv_erase(uint32_t table_id, const void* key, uint32_t key_size);
```

Erase a row of the receiver, refunding its current payer. Returns `ok` or `no_row`.

### kv\_get

```c
int32_t kv_get(uint32_t table_id, uint64_t code,
               const void* key, uint32_t key_size,
               uint32_t offset, void* value, uint32_t value_size);
```

Read a row's value. Returns the full value size, or `-1` if the row does not exist. An empty buffer reads nothing, so this is also the existence test.

## Index entries (3)

```c
int32_t kv_idx_store(uint32_t table_id, uint64_t payer,
                     const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);       // ok, exists

int32_t kv_idx_update(uint32_t table_id, uint64_t payer,
                      const void* pri_key, uint32_t pri_key_size,
                      const void* old_sec_key, uint32_t old_sec_key_size,
                      const void* new_sec_key, uint32_t new_sec_key_size);   // ok, no_row, exists

int32_t kv_idx_erase(uint32_t table_id,
                     const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);       // ok, no_row
```

`table_id` identifies the index, not the table. `kv_idx_store` only creates: payer 0 returns `exists` for an entry already there, and aborts otherwise. `kv_idx_update` moves the entry in place: `no_row` means no entry at the old key, `exists` means one at the new key. With old and new equal it returns `ok` and can still change the payer.

## Primary iterators (6)

```c
int32_t kv_it_create(uint32_t table_id, uint64_t code,
                     const void* prefix, uint32_t prefix_size,
                     const void* seek, uint32_t seek_size);             // handle

int32_t kv_it_next(int32_t handle,
                   void* key, uint32_t key_capacity,
                   void* value, uint32_t value_capacity,
                   uint32_t* key_size, uint32_t* value_size);           // kv_it_prev: same

int32_t kv_it_lower_bound(int32_t handle, const void* seek, uint32_t seek_size,
                          void* key, uint32_t key_capacity,
                          void* value, uint32_t value_capacity,
                          uint32_t* key_size, uint32_t* value_size);

int32_t kv_it_read(int32_t handle, uint32_t offset,
                   void* key, uint32_t key_capacity,
                   void* value, uint32_t value_capacity,
                   uint32_t* key_size, uint32_t* value_size);

void    kv_it_destroy(int32_t handle);
```

`kv_it_create` opens at the first row at or after `seek` within `prefix`, and an empty seek means the first row. It always returns a handle.

A move fills the buffers with the row it lands on, so a scan is one host call per row. Moves return `ok` or `end`. Only `kv_it_read` returns `no_row`, for a row erased under the iterator.

## Secondary iterators (7)

```c
int32_t kv_idx_create(uint32_t table_id, uint64_t code,
                      const void* prefix, uint32_t prefix_size,
                      const void* seek, uint32_t seek_size);            // handle

int32_t kv_idx_next(int32_t handle,
                    void* sec_key, uint32_t sec_capacity,
                    void* pri_key, uint32_t pri_capacity,
                    uint32_t* sec_size, uint32_t* pri_size);            // kv_idx_prev, kv_idx_read: same

int32_t kv_idx_lower_bound(int32_t handle, const void* seek, uint32_t seek_size,
                           void* sec_key, uint32_t sec_capacity,
                           void* pri_key, uint32_t pri_capacity,
                           uint32_t* sec_size, uint32_t* pri_size);

void    kv_idx_destroy(int32_t handle);

int32_t kv_idx_find(uint32_t table_id, uint64_t code,
                    const void* prefix, uint32_t prefix_size,
                    const void* sec_key, uint32_t sec_key_size);        // handle, or -1
```

These match the primary family, with `prefix` and `seek` comparing the secondary key only. A table with scopes passes the scope bytes as `prefix`, so a step never has to read the key to see where it landed. `kv_idx_read` also returns `no_row` after `kv_idx_update` moved the entry off the iterator's key.

`kv_idx_create` always returns a handle, even over an empty index. `kv_idx_find` returns a handle on the first entry with that exact secondary key, or `-1` with no handle allocated, in which case `kv_idx_destroy` must not be called. A `sec_key` that does not start with `prefix` aborts.

## Reads

Every call that fills a buffer follows one rule:

- A zero-size buffer skips the copy, not the size.
- A short buffer truncates and reports the full size.
- An offset at or past the end copies nothing. The offset exists on value reads only: `kv_get` and `kv_it_read`.
- On `ok` the size outputs report the full stored sizes. On `end` or `no_row` both are 0. They are always written, so they must be valid pointers.

## Iterator position

An iterator's position is a key, not a row.

- A read returns the row stored at the current key, or `no_row` if there is none.
- `next` and `prev` move strictly past that key whether or not its row is still there.
- A seek below the prefix clamps up to the prefix, and one above the prefix range lands on `end`.
- From `end`, `prev` moves to the last row of the prefix range. From the first row it moves to `end`.
- A seek has no size limit. CDT positions at the end with a 513-byte all-0xFF seek, one byte longer than the longest key, and just past a key with the key followed by a NUL byte.

## Design Notes

| Aspect | Description |
|--------|-------------|
| First param | `table_id` for every call but those taking a `handle` |
| Secondary index ID | Single `table_id` per secondary index (replaces separate table + index_id params) |
| Standard key size | 16B `[scope:8B BE][pk:8B BE]` — table name conveyed by `table_id`, not in key |
| Table isolation | Each table and secondary index gets a unique `table_id` in the composite index |
| Total intrinsics | 19 (3 rows + 3 index entries + 6 primary iterator + 7 secondary iterator) |
