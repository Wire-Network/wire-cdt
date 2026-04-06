#pragma once

#include <cstdint>

namespace sysio::kv {

/// Key format constants for KV intrinsics.
/// Format determines how the host indexes and partitions key data.
/// Format 0 and format 1 entries are stored in separate index partitions,
/// so there is no collision between raw_table and multi_index/kv::table data.

/// Raw bytes -- variable-length keys, used by kv::raw_table.
inline constexpr uint32_t kv_format_raw      = 0;

/// Standard 24-byte layout: [table:8B BE][scope:8B BE][pk:8B BE].
/// Enables integer fast-path comparison and SHiP translation to legacy
/// contract_row format.
inline constexpr uint32_t kv_format_standard = 1;

/// Chain-enforced maximum KV key size in bytes.
inline constexpr size_t kv_key_max_bytes = 1024;

/// Stack buffer size for value reads/writes. Values up to this size avoid heap allocation.
/// Only used for non-trivially-copyable types; trivially-copyable types use sizeof(T) directly.
inline constexpr uint32_t kv_value_stack_size = 256;

} // namespace sysio::kv
