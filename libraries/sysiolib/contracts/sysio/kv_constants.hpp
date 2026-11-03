#pragma once

#include <cstdint>
#include <type_traits>

namespace sysio::kv {

/// Chain-enforced absolute maximum KV key size in bytes. The governance limit starts lower, at 256.
inline constexpr size_t kv_key_max_bytes = 512;

/// Stack buffer size for value reads/writes. Values up to this size avoid heap allocation.
/// kv::table and kv::global do not use it when is_fixed_serializable_v<T> holds: a read then offers
/// exactly sizeof(T). multi_index uses it for every row.
inline constexpr uint32_t kv_value_stack_size = 256;

/// Size in bytes of a scope encoded as a big-endian uint64_t (used as a key prefix
/// in scoped tables). Equivalent to sizeof(uint64_t).
inline constexpr uint32_t kv_scope_size = sizeof(uint64_t);

/// Text raised when the host reports a state a wrapper has no contract-supplied message for. A
/// contract can reach these only through a stale iterator or by writing around the wrappers.
namespace msg {
   inline constexpr const char* row_not_found    = "KV key not found for erase";
   inline constexpr const char* entry_exists     = "KV secondary index entry already exists";
   inline constexpr const char* entry_not_erased = "KV secondary index entry not found for remove";
   inline constexpr const char* entry_not_moved  = "KV secondary index entry not found for update";
   inline constexpr const char* write_refused    = "KV write refused";
   inline constexpr const char* row_not_modified = "cannot modify a row that no longer exists";
   inline constexpr const char* row_not_held     = "cannot modify a row the receiver does not hold";
   inline constexpr const char* row_lost         = "KV row changed during a read";
   inline constexpr const char* no_table         = "cannot position a default-constructed iterator";
   inline constexpr const char* iterator_erased  = "cannot use an iterator after erasing its row";
   inline constexpr const char* past_rend        = "cannot increment reverse iterator at rend";
   inline constexpr const char* before_rbegin    = "cannot decrement reverse iterator at rbegin";
   inline constexpr const char* modify_end       = "cannot modify end iterator";
   inline constexpr const char* erase_end        = "cannot erase end iterator";
   inline constexpr const char* entry_moved      = "cannot copy an iterator whose index entry has moved";
   inline constexpr const char* pk_limit         = "next primary key in table is at autoincrement limit";
   inline constexpr const char* table_start      = "cannot decrement iterator at beginning of table";
   inline constexpr const char* index_start      = "cannot decrement iterator at beginning of index";
   inline constexpr const char* table_is_empty   = "cannot decrement end iterator when the table is empty";
   inline constexpr const char* index_is_empty   = "cannot decrement end iterator when the index is empty";
   inline constexpr const char* reader_underflow = "be_key_reader underflow";
   inline constexpr const char* reader_truncated = "be_key_reader: truncated NUL-escape";
   inline constexpr const char* reader_bad_escape   = "be_key_reader: invalid NUL-escape byte";
   inline constexpr const char* reader_unterminated = "be_key_reader: unterminated NUL-escape string";
   inline constexpr const char* reader_too_large    = "be_key_reader: string too large";
}

// ── table_id computation ─────────────────────────────────────────────────────
// Each table and each secondary index gets a unique uint16_t table_id.
// Computed by DJB2-hashing the big-endian bytes of the raw uint64_t template
// parameter (works for both name::raw and hash_id::raw), then truncating to 16 bits.
// Must match chain-side compute_table_id in sysio/chain/types.hpp.

// String-based djbh_hash is in hash_id.hpp (sysio::hash_id::djbh_hash).

/// DJB2 initial hash seed (canonical value from Daniel J. Bernstein's hash function).
inline constexpr uint64_t djbh_seed = 5381;

/// DJB2-hash the 8 big-endian bytes of a uint64_t, optionally continuing from an existing hash.
/// Gives good distribution regardless of input bit patterns (unlike raw % 65536
/// which maps most name::raw values to 0 due to MSB-packed encoding).
inline constexpr uint64_t djbh_hash_raw(uint64_t raw, uint64_t hash = djbh_seed) {
   for (int i = 0; i < 8; ++i)
      // hash * 33 (2^5 + 1), then add byte
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
/// Narrowing cast to uint16_t truncates to the low 16 bits (well-defined for unsigned).
inline constexpr uint16_t compute_table_id(uint64_t raw) {
   return static_cast<uint16_t>(djbh_hash_raw(raw));
}

/// Compute secondary index table_id from table + index template parameters.
/// Both are raw uint64_t values (name::raw or hash_id::raw).
/// Chains two 8-byte DJB2 passes: first the table bytes, then the index bytes.
inline constexpr uint16_t compute_sec_table_id(uint64_t table_raw, uint64_t index_raw) {
   return static_cast<uint16_t>(djbh_hash_raw(index_raw, djbh_hash_raw(table_raw)));
}

/// Compute secondary index table_id for multi_index (positional indices).
/// Uses index_pos+1 as a synthetic raw value (0 would collide with all-zero names).
inline constexpr uint16_t compute_mi_sec_table_id(uint64_t table_raw, uint8_t index_pos) {
   return compute_sec_table_id(table_raw, static_cast<uint64_t>(index_pos) + 1);
}

} // namespace sysio::kv
