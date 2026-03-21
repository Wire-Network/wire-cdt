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
/// Enables SSO fast-path (inline storage + integer comparison) and SHiP
/// translation to legacy contract_row format.
inline constexpr uint32_t kv_format_standard = 1;

} // namespace sysio::kv
