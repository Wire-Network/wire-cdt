#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace sysio::kv {

/// Chain-enforced maximum KV key size in bytes.
inline constexpr size_t kv_key_max_bytes = 1024;

/// Stack buffer size for value reads/writes. Values up to this size avoid heap allocation.
/// Only used for non-trivially-copyable types; trivially-copyable types use sizeof(T) directly.
inline constexpr uint32_t kv_value_stack_size = 256;

// ── table_id computation ─────────────────────────────────────────────────────
// Each table and each secondary index gets a unique uint16_t table_id.
// Computed by DJB2-hashing the big-endian bytes of the raw uint64_t template
// parameter (works for both name::raw and hash_id::raw), then truncating to 16 bits.
// Must match chain-side compute_table_id in sysio/chain/types.hpp.

// String-based djbh_hash is in hash_id.hpp (sysio::hash_id::djbh_hash).

/// DJB2-hash the 8 big-endian bytes of a uint64_t.
/// Gives good distribution regardless of input bit patterns (unlike raw % 65536
/// which maps most name::raw values to 0 due to MSB-packed encoding).
inline constexpr uint64_t djbh_hash_raw(uint64_t raw) {
   uint64_t hash = 5381;
   for (int i = 0; i < 8; ++i)
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(raw >> (56 - i * 8));
   return hash;
}

/// Convert any NTTP to uint64_t (works for name, name::raw, hash_id::raw, uint64_t).
inline constexpr uint64_t to_uint64(uint64_t v) { return v; }

template<typename T, std::enable_if_t<std::is_enum_v<T>, int> = 0>
inline constexpr uint64_t to_uint64(T v) {
   return static_cast<uint64_t>(static_cast<std::underlying_type_t<T>>(v));
}

// Overload for struct types with a .value member (sysio::name, sysio::hash_id)
template<typename T, std::enable_if_t<!std::is_enum_v<T> && !std::is_integral_v<T>, int> = 0>
inline constexpr uint64_t to_uint64(T v) {
   return static_cast<uint64_t>(v.value);
}

/// Compute primary table_id from a template parameter (name::raw or hash_id::raw).
inline constexpr uint16_t compute_table_id(uint64_t raw) {
   return static_cast<uint16_t>(djbh_hash_raw(raw) % 65536);
}

/// Compute secondary index table_id from table + index template parameters.
/// Both are raw uint64_t values (name::raw or hash_id::raw).
inline constexpr uint16_t compute_sec_table_id(uint64_t table_raw, uint64_t index_raw) {
   // Continue hashing index bytes after table bytes (single DJB2 chain)
   uint64_t hash = djbh_hash_raw(table_raw);
   for (int i = 0; i < 8; ++i)
      hash = ((hash << 5) + hash) + static_cast<uint8_t>(index_raw >> (56 - i * 8));
   return static_cast<uint16_t>(hash % 65536);
}

/// Compute secondary index table_id for multi_index (positional indices).
/// Uses index_pos+1 as a synthetic raw value (0 would collide with all-zero names).
inline constexpr uint16_t compute_mi_sec_table_id(uint64_t table_raw, uint8_t index_pos) {
   return compute_sec_table_id(table_raw, static_cast<uint64_t>(index_pos) + 1);
}

} // namespace sysio::kv
