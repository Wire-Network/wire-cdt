#pragma once

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup kv_database_c KV Database C API
 * @ingroup c_api
 * @brief Defines API for KV database intrinsics
 *
 * Keys and values are arbitrary byte sequences, stored per contract and table_id, with no scope
 * indirection. Keys sort as unsigned bytes, shorter prefix first. Index entries sort by secondary key,
 * then primary key. This header is the only declaration of these imports.
 *
 * Rules every call below shares:
 *
 *  - table_id is the low 16 bits of a hash of the table name (see sysio::kv::compute_table_id). A value
 *    above 65535 aborts. The chain does not enforce uniqueness; CDT codegen checks it within one ABI.
 *  - Reads take a `code` account. Writes have none and always land on the receiver.
 *  - No row has an empty key, so an empty key aborts on every call that takes one. A prefix or a seek
 *    is not a key and may be empty. Key and secondary key sizes default to a 256
 *    byte limit, values to 256 KiB. Governance can raise them to 512 bytes and 1 MiB.
 *  - Payer 0 keeps the current payer when the row exists, and is rejected when creating one. A payer
 *    other than the receiver must authorize with sysio.payer.
 *  - A state miss is returned and misuse throws. A duplicate key or a missing row comes back as a
 *    status for the wrapper to report. Bad arguments, size limits and the iterator limit still abort.
 *  - One read buffer rule: a zero-size buffer skips the copy, a short buffer truncates, and an offset at
 *    or past the end copies nothing. On kv_status_ok the size outputs report the full stored sizes in
 *    every one of those cases; on any other status both are 0. They are always written.
 *  - An iterator's position is a key, not a row. Reads return the row stored at the current key, or
 *    kv_status_no_row if there is none. next and prev move strictly past that key whether or not its
 *    row is still there.
 *  - Handles are non-negative, and valid within one receiver's execution. Each iterator family holds
 *    up to 1024 open handles.
 * @{
 */

/** Result of a KV call. A call that returns a size or a handle uses -1 for none instead. */
enum kv_status : int32_t {
   kv_status_ok     = 0,   ///< the call succeeded, or the iterator is on a row
   kv_status_end    = 1,   ///< the iterator left its prefix range
   kv_status_no_row = 2,   ///< no row at that key
   kv_status_exists = 3    ///< a row already exists at that key
};

/** How kv_set treats an existing row. */
enum kv_set_mode : uint32_t {
   kv_set_upsert = 0,   ///< create the row, or replace it
   kv_set_insert = 1,   ///< create the row; kv_status_exists if it exists
   kv_set_update = 2    ///< replace the row; kv_status_no_row if it does not exist
};

// --- Rows ---

/**
 * Create or replace a row of the receiver.
 *
 * @param table_id - table identifier
 * @param payer - account billed for the row. See the payer rule above.
 * @param key - key bytes
 * @param key_size - length of key
 * @param value - value bytes
 * @param value_size - length of value
 * @param mode - a kv_set_mode. Any other value aborts.
 * @return kv_status_ok, kv_status_exists (kv_set_insert) or kv_status_no_row (kv_set_update)
 */
__attribute__((sysio_wasm_import, warn_unused_result))
int32_t kv_set(uint32_t table_id, uint64_t payer, const void* key, uint32_t key_size,
               const void* value, uint32_t value_size, uint32_t mode);

/**
 * Erase a row of the receiver. RAM is refunded to the row's current payer.
 *
 * @param table_id - table identifier
 * @param key - key bytes
 * @param key_size - length of key
 * @return kv_status_ok or kv_status_no_row
 */
__attribute__((sysio_wasm_import, warn_unused_result))
int32_t kv_erase(uint32_t table_id, const void* key, uint32_t key_size);

/**
 * Read a row's value.
 *
 * @param table_id - table identifier
 * @param code - account to read from
 * @param key - key bytes. Empty aborts.
 * @param key_size - length of key
 * @param offset - byte offset into the value
 * @param value - output buffer. Size 0 reads nothing, so this also tests whether the row exists.
 * @param value_size - size of output buffer
 * @return the full value size, or -1 if the row does not exist
 */
__attribute__((sysio_wasm_import))
int32_t kv_get(uint32_t table_id, capi_name code, const void* key, uint32_t key_size, uint32_t offset,
               void* value, uint32_t value_size);

// --- Secondary index entries ---

/**
 * Store a secondary index entry of the receiver.
 *
 * @param table_id - index identifier (see sysio::kv::compute_sec_table_id)
 * @param payer - account billed for the entry. 0 aborts unless the entry exists, which returns
 *                kv_status_exists.
 * @param pri_key - primary key bytes
 * @param pri_key_size - length of pri_key
 * @param sec_key - secondary key bytes
 * @param sec_key_size - length of sec_key
 * @return kv_status_ok or kv_status_exists
 */
__attribute__((sysio_wasm_import, warn_unused_result))
int32_t kv_idx_store(uint32_t table_id, uint64_t payer, const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);

/**
 * Move a secondary index entry of the receiver to a new secondary key, in place.
 *
 * @param table_id - index identifier
 * @param payer - account billed for the entry. See the payer rule above.
 * @param pri_key - primary key bytes. Empty aborts.
 * @param pri_key_size - length of pri_key
 * @param old_sec_key - current secondary key bytes. Empty aborts.
 * @param old_sec_key_size - length of old_sec_key
 * @param new_sec_key - new secondary key bytes. Empty aborts.
 * @param new_sec_key_size - length of new_sec_key
 * @return kv_status_ok, kv_status_no_row (no entry at the old key) or kv_status_exists (one at the new)
 */
__attribute__((sysio_wasm_import, warn_unused_result))
int32_t kv_idx_update(uint32_t table_id, uint64_t payer, const void* pri_key, uint32_t pri_key_size,
                      const void* old_sec_key, uint32_t old_sec_key_size,
                      const void* new_sec_key, uint32_t new_sec_key_size);

/**
 * Erase a secondary index entry of the receiver. RAM is refunded to the entry's current payer.
 *
 * @param table_id - index identifier
 * @param pri_key - primary key bytes
 * @param pri_key_size - length of pri_key
 * @param sec_key - secondary key bytes
 * @param sec_key_size - length of sec_key
 * @return kv_status_ok or kv_status_no_row
 */
__attribute__((sysio_wasm_import, warn_unused_result))
int32_t kv_idx_erase(uint32_t table_id, const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);

// --- Primary iterators ---

/**
 * Create an iterator over the rows whose key starts with a prefix.
 *
 * @param table_id - table identifier
 * @param code - account to read from
 * @param prefix - key prefix bytes. Empty iterates the whole table.
 * @param prefix_size - length of prefix
 * @param seek - key to start at. Empty starts at the first row. A seek below the prefix starts at the
 *   prefix; one above the prefix range starts at the end, from which kv_it_prev reaches the last row.
 * @param seek_size - length of seek
 * @return iterator handle
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_create(uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size,
                     const void* seek, uint32_t seek_size);

/**
 * Move to the next row and read it.
 *
 * @param handle - iterator handle
 * @param key - output buffer for the key
 * @param key_capacity - size of key buffer
 * @param value - output buffer for the value
 * @param value_capacity - size of value buffer
 * @param key_size - [out] full key size
 * @param value_size - [out] full value size
 * @return kv_status_ok on a row, or kv_status_end past the last row
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_next(int32_t handle, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity,
                   uint32_t* key_size, uint32_t* value_size);

/**
 * Move to the previous row and read it. From the end, moves to the last row of the prefix range.
 * Parameters as kv_it_next.
 *
 * @return kv_status_ok on a row, or kv_status_end before the first row
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_prev(int32_t handle, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity,
                   uint32_t* key_size, uint32_t* value_size);

/**
 * Re-seek an iterator to the first row at or after a key, and read it.
 *
 * @param handle - iterator handle
 * @param seek - key to seek. Treated as in kv_it_create.
 * @param seek_size - length of seek
 * @param key - output buffer for the key
 * @param key_capacity - size of key buffer
 * @param value - output buffer for the value
 * @param value_capacity - size of value buffer
 * @param key_size - [out] full key size
 * @param value_size - [out] full value size
 * @return kv_status_ok on a row, or kv_status_end
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_lower_bound(int32_t handle, const void* seek, uint32_t seek_size, void* key, uint32_t key_capacity,
                          void* value, uint32_t value_capacity, uint32_t* key_size, uint32_t* value_size);

/**
 * Read the row at the iterator's position without moving it.
 *
 * @param handle - iterator handle
 * @param offset - byte offset into the value. Keys have no offset.
 * @param key - output buffer for the key
 * @param key_capacity - size of key buffer
 * @param value - output buffer for the value
 * @param value_capacity - size of value buffer
 * @param key_size - [out] full key size
 * @param value_size - [out] full value size
 * @return kv_status_ok, kv_status_end, or kv_status_no_row if the row was erased
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_read(int32_t handle, uint32_t offset, void* key, uint32_t key_capacity, void* value,
                   uint32_t value_capacity, uint32_t* key_size, uint32_t* value_size);

/**
 * Destroy an iterator, freeing its handle.
 * @param handle - iterator handle
 */
__attribute__((sysio_wasm_import))
void kv_it_destroy(int32_t handle);

// --- Secondary iterators ---

/**
 * Create an iterator over the index entries whose secondary key starts with a prefix. A table with
 * scopes passes the scope bytes here, so a step never has to read the key to see where it landed.
 *
 * @param table_id - index identifier
 * @param code - account to read from
 * @param prefix - secondary key prefix bytes. Empty iterates the whole index.
 * @param prefix_size - length of prefix
 * @param seek - secondary key to start at. Empty starts at the first entry. Bounded as in kv_it_create.
 * @param seek_size - length of seek
 * @return iterator handle, always, even over an empty index
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_create(uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size,
                      const void* seek, uint32_t seek_size);

/**
 * Move to the next entry and read it.
 *
 * @param handle - iterator handle
 * @param sec_key - output buffer for the secondary key
 * @param sec_capacity - size of sec_key buffer
 * @param pri_key - output buffer for the primary key
 * @param pri_capacity - size of pri_key buffer
 * @param sec_size - [out] full secondary key size
 * @param pri_size - [out] full primary key size
 * @return kv_status_ok on an entry, or kv_status_end past the last entry
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_next(int32_t handle, void* sec_key, uint32_t sec_capacity, void* pri_key, uint32_t pri_capacity,
                    uint32_t* sec_size, uint32_t* pri_size);

/**
 * Move to the previous entry and read it. From the end, moves to the last entry of the prefix range.
 * Parameters as kv_idx_next.
 *
 * @return kv_status_ok on an entry, or kv_status_end before the first entry
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_prev(int32_t handle, void* sec_key, uint32_t sec_capacity, void* pri_key, uint32_t pri_capacity,
                    uint32_t* sec_size, uint32_t* pri_size);

/**
 * Re-seek an iterator to the first entry at or after a secondary key, and read it.
 *
 * @param handle - iterator handle
 * @param seek - secondary key to seek. Treated as in kv_idx_create.
 * @param seek_size - length of seek
 * @param sec_key - output buffer for the secondary key
 * @param sec_capacity - size of sec_key buffer
 * @param pri_key - output buffer for the primary key
 * @param pri_capacity - size of pri_key buffer
 * @param sec_size - [out] full secondary key size
 * @param pri_size - [out] full primary key size
 * @return kv_status_ok on an entry, or kv_status_end
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_lower_bound(int32_t handle, const void* seek, uint32_t seek_size, void* sec_key,
                           uint32_t sec_capacity, void* pri_key, uint32_t pri_capacity,
                           uint32_t* sec_size, uint32_t* pri_size);

/**
 * Read the entry at the iterator's position without moving it. Parameters as kv_idx_next.
 *
 * @return kv_status_ok, kv_status_end, or kv_status_no_row if the entry was erased or moved off this
 *         key by kv_idx_update
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_read(int32_t handle, void* sec_key, uint32_t sec_capacity, void* pri_key, uint32_t pri_capacity,
                    uint32_t* sec_size, uint32_t* pri_size);

/**
 * Destroy a secondary iterator, freeing its handle.
 * @param handle - iterator handle
 */
__attribute__((sysio_wasm_import))
void kv_idx_destroy(int32_t handle);

/**
 * Find the first entry with an exact secondary key.
 *
 * @param table_id - index identifier
 * @param code - account to read from
 * @param prefix - secondary key prefix bounding later steps, as in kv_idx_create
 * @param prefix_size - length of prefix
 * @param sec_key - secondary key bytes, including the prefix. Empty aborts, as does a key that does not
 *                  start with prefix.
 * @param sec_key_size - length of sec_key
 * @return iterator handle, or -1 with no handle allocated, in which case kv_idx_destroy must not be called
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_find(uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size,
                    const void* sec_key, uint32_t sec_key_size);

/** @} */

#ifdef __cplusplus
}
#endif
