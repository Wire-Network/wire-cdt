#pragma once

// Secondary-index host intrinsics shared by kv_multi_index.hpp and
// kv_table.hpp. Consolidated here so signature changes only need to
// touch one place; both wrappers transitively reach these through
// their own includes.
//
// Primary KV and primary-iterator intrinsics (kv_set/kv_get/kv_erase/
// kv_contains, kv_it_*) are declared in <sysio/kv_utils.hpp>.

#include <cstddef>
#include <cstdint>

extern "C" {

__attribute__((sysio_wasm_import))
void kv_idx_store(uint64_t payer, uint32_t table_id, int64_t primary_id,
                  const void* sec_key, uint32_t sec_key_size);

__attribute__((sysio_wasm_import))
void kv_idx_remove(uint32_t table_id, int64_t primary_id,
                   const void* sec_key, uint32_t sec_key_size);

__attribute__((sysio_wasm_import))
void kv_idx_update(uint64_t payer, uint32_t table_id, int64_t primary_id,
                   const void* old_sec_key, uint32_t old_sec_key_size,
                   const void* new_sec_key, uint32_t new_sec_key_size);

__attribute__((sysio_wasm_import))
int32_t kv_idx_find_secondary(uint64_t code, uint32_t table_id,
                              const void* sec_key, uint32_t sec_key_size);

__attribute__((sysio_wasm_import))
int32_t kv_idx_lower_bound(uint64_t code, uint32_t table_id,
                           const void* sec_key, uint32_t sec_key_size);

__attribute__((sysio_wasm_import))
int32_t kv_idx_next(uint32_t handle);

__attribute__((sysio_wasm_import))
int32_t kv_idx_prev(uint32_t handle);

__attribute__((sysio_wasm_import))
int32_t kv_idx_key(uint32_t handle, uint32_t offset,
                   void* dest, uint32_t dest_size, uint32_t* actual_size);

__attribute__((sysio_wasm_import))
int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset,
                           void* dest, uint32_t dest_size, uint32_t* actual_size);

__attribute__((sysio_wasm_import))
void kv_idx_destroy(uint32_t handle);

}
