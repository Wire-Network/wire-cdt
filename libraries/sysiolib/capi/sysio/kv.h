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
 * Replaces the legacy db_*_i64 intrinsics with a simpler key-value interface.
 * Keys and values are arbitrary byte sequences. No scope indirection.
 * RAM is charged to the payer account (defaults to the executing contract).
 * @{
 */

/**
 * Store or update a key-value pair.
 *
 * @param key_format - encoding format: 0 = raw bytes, 1 = standard 24-byte
 *   [table:8B BE][scope:8B BE][pk:8B BE] layout
 * @param payer - account to bill for RAM. Pass 0 to bill the executing contract.
 *   Non-zero payer requires payer authorization at the transaction level.
 * @param key - pointer to key bytes
 * @param key_size - length of key in bytes (max 256)
 * @param value - pointer to value bytes
 * @param value_size - length of value in bytes (max 256 KiB)
 * @return RAM byte delta (positive for growth, 0 for same-size update)
 */
__attribute__((sysio_wasm_import))
int64_t kv_set(uint32_t key_format, uint64_t payer, const void* key, uint32_t key_size, const void* value, uint32_t value_size);

/**
 * Read a value by key. If buffer is too small, returns the actual size without error.
 *
 * @param key_format - encoding format: 0 = raw bytes, 1 = standard 24-byte
 *   [table:8B BE][scope:8B BE][pk:8B BE] layout
 * @param code - contract account to read from
 * @param key - pointer to key bytes
 * @param key_size - length of key
 * @param value - output buffer
 * @param value_size - size of output buffer
 * @return actual value size, or -1 if key not found
 */
__attribute__((sysio_wasm_import))
int32_t kv_get(uint32_t key_format, capi_name code, const void* key, uint32_t key_size, void* value, uint32_t value_size);

/**
 * Erase a key-value pair. Aborts if key not found.
 *
 * @param key_format - encoding format: 0 = raw bytes, 1 = standard 24-byte layout
 * @param key - pointer to key bytes
 * @param key_size - length of key
 * @return negative RAM byte delta
 */
__attribute__((sysio_wasm_import))
int64_t kv_erase(uint32_t key_format, const void* key, uint32_t key_size);

/**
 * Check if a key exists.
 *
 * @param key_format - encoding format: 0 = raw bytes, 1 = standard 24-byte layout
 * @param code - contract account to check
 * @param key - pointer to key bytes
 * @param key_size - length of key
 * @return 1 if key exists, 0 otherwise
 */
__attribute__((sysio_wasm_import))
int32_t kv_contains(uint32_t key_format, capi_name code, const void* key, uint32_t key_size);

// --- Primary KV Iterators ---

/**
 * Create an iterator over keys matching a prefix for a given contract.
 * Positions at the first matching key, or end if none match.
 *
 * @param key_format - encoding format: 0 = raw bytes, 1 = standard 24-byte layout
 * @param code - contract account
 * @param prefix - key prefix bytes (empty = iterate all)
 * @param prefix_size - length of prefix
 * @return iterator handle (0..15)
 */
__attribute__((sysio_wasm_import))
uint32_t kv_it_create(uint32_t key_format, capi_name code, const void* prefix, uint32_t prefix_size);

/**
 * Destroy an iterator, freeing the slot.
 * @param handle - iterator handle
 */
__attribute__((sysio_wasm_import))
void kv_it_destroy(uint32_t handle);

/**
 * Get iterator status: 0=ok, 1=end, 2=erased.
 * @param handle - iterator handle
 * @return status code
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_status(uint32_t handle);

/**
 * Advance iterator to next key within prefix. Returns new status.
 * @param handle - iterator handle
 * @return status after advancing
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_next(uint32_t handle);

/**
 * Move iterator to previous key within prefix. Returns new status.
 * @param handle - iterator handle
 * @return status after retreating
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_prev(uint32_t handle);

/**
 * Seek iterator to lower bound of key within prefix. Returns new status.
 * @param handle - iterator handle
 * @param key - seek key bytes
 * @param key_size - length of seek key
 * @return status after seeking
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_lower_bound(uint32_t handle, const void* key, uint32_t key_size);

/**
 * Read current iterator's key with offset support.
 * @param handle - iterator handle
 * @param offset - byte offset into key
 * @param dest - output buffer
 * @param dest_size - output buffer size
 * @param actual_size - [out] actual total key size
 * @return status code
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);

/**
 * Read current iterator's value with offset support.
 * @param handle - iterator handle
 * @param offset - byte offset into value
 * @param dest - output buffer
 * @param dest_size - output buffer size
 * @param actual_size - [out] actual total value size
 * @return status code
 */
__attribute__((sysio_wasm_import))
int32_t kv_it_value(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);

// --- Secondary KV Index ---

/**
 * Store a secondary index entry.
 * @param payer - account paying for RAM (0 = contract itself)
 * @param table - logical table name
 * @param index_id - index identifier (0-255)
 * @param pri_key - primary key bytes
 * @param pri_key_size - primary key size (max 256)
 * @param sec_key - secondary key bytes
 * @param sec_key_size - secondary key size (max 256)
 */
__attribute__((sysio_wasm_import))
void kv_idx_store(uint64_t payer, capi_name table, uint32_t index_id,
                  const void* pri_key, uint32_t pri_key_size,
                  const void* sec_key, uint32_t sec_key_size);

/**
 * Remove a secondary index entry.
 */
__attribute__((sysio_wasm_import))
void kv_idx_remove(capi_name table, uint32_t index_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* sec_key, uint32_t sec_key_size);

/**
 * Update a secondary index entry (change secondary key, keep primary key).
 */
__attribute__((sysio_wasm_import))
void kv_idx_update(uint64_t payer, capi_name table, uint32_t index_id,
                   const void* pri_key, uint32_t pri_key_size,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size);

/**
 * Find exact secondary key match.
 *
 * @return iterator handle (>= 0) on match, or -1 if not found.
 *   When -1 is returned no handle is allocated and kv_idx_destroy
 *   must NOT be called.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_find_secondary(capi_name code, capi_name table, uint32_t index_id,
                              const void* sec_key, uint32_t sec_key_size);

/**
 * Find lower bound on secondary key.
 *
 * @return handle >= 0 positioned at the first entry with sec_key >= bound,
 *   or handle >= 0 in iterator_end state when bound is past all entries but
 *   the table is non-empty (enables kv_idx_prev to reach the last entry).
 *   Returns -1 only when the table has no entries for this (code, table, index_id).
 *   When -1 is returned no handle is allocated and kv_idx_destroy must NOT be called.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_lower_bound(capi_name code, capi_name table, uint32_t index_id,
                           const void* sec_key, uint32_t sec_key_size);

/**
 * Advance secondary iterator. Returns status.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_next(uint32_t handle);

/**
 * Retreat secondary iterator. Returns status.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_prev(uint32_t handle);

/**
 * Read current secondary key from iterator.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);

/**
 * Read current primary key from secondary iterator.
 */
__attribute__((sysio_wasm_import))
int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);

/**
 * Destroy a secondary iterator.
 */
__attribute__((sysio_wasm_import))
void kv_idx_destroy(uint32_t handle);

/** @} */

#ifdef __cplusplus
}
#endif
