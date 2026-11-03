#pragma once
/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  In-memory stand-in for the chain's KV host functions, for native unit tests.
 *
 *  It follows wire-sysio's apply_context and kv_database rule for rule, including the parts a wrapper
 *  can get wrong: reads take a `code` and writes always land on the receiver, an iterator's position
 *  is a key rather than a row, a move returns only ok or end, and only a read reports a row that was
 *  erased under it. The checks the host makes first are here too, with the host's order and text:
 *  read-only transactions, empty keys, size limits, the table_id and mode ranges, the iterator limit
 *  and the handle layout. A row and an entry each keep their payer.
 *
 *  Left out, and pinned by wire-sysio's own tests: RAM billing and payer authorization, so a payer is
 *  kept but never charged or checked. The limits are the chain's defaults, in fields a case can change.
 *
 *  Every entry point counts its calls, because a wrapper's host-call cost is part of its contract
 *  and a result alone cannot show it. calls() names one intrinsic; total() sums all of them. Both
 *  exclude current_receiver, which is counted apart in receiver_calls: it is not a KV call, and
 *  the tests pin it separately so a wrapper cannot hide one behind a KV count.
 */

#include <sysio/tester.hpp>
#include <sysio/kv_constants.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace kv_mock {

// The chain's values, from config.hpp, kv_context.hpp and kv_database.cpp.
inline constexpr uint32_t default_max_key_size           = 256;
inline constexpr uint32_t default_max_value_size         = 256 * 1024;
inline constexpr uint32_t default_max_secondary_key_size = 256;
inline constexpr uint32_t max_prefix_size                = sysio::kv::kv_key_max_bytes;
inline constexpr uint32_t max_iterators                  = 1024;
inline constexpr uint32_t max_table_id                   = 65535;
inline constexpr uint32_t secondary_handle_tag           = 0x00010000u;
inline constexpr uint32_t handle_slot_mask               = 0x000003FFu;
inline constexpr uint32_t handle_reserved_mask           = ~(handle_slot_mask | secondary_handle_tag);

inline void check_table_id(uint32_t table_id) {
   sysio::check(table_id <= max_table_id, "table_id " + std::to_string(table_id) + " exceeds maximum " +
                                             std::to_string(max_table_id));
}

/// Abort unless \p size fits \p limit, in the words of the host's message about \p what.
inline void check_size(const std::string& what, uint32_t size, uint32_t limit) {
   sysio::check(size <= limit, "KV " + what + " size " + std::to_string(size) + " exceeds maximum " +
                                  std::to_string(limit));
}

inline void check_prefix_size(uint32_t size) {
   sysio::check(size <= max_prefix_size, "KV iterator prefix size " + std::to_string(size) + " exceeds limit " +
                                            std::to_string(max_prefix_size));
}

/// Abort on a handle with a reserved bit set, or one from the other pool.
/// @return the handle's slot number
inline uint32_t validate_handle(int32_t handle, bool secondary, const char* op) {
   const auto bits = static_cast<uint32_t>(handle);
   sysio::check((bits & handle_reserved_mask) == 0,
                "KV iterator handle has reserved bits set: " + std::to_string(bits));
   sysio::check(((bits & secondary_handle_tag) != 0) == secondary,
                std::string(op) + " called on " + (secondary ? "primary" : "secondary") + " iterator handle");
   return bits & handle_slot_mask;
}

/// The first key above every key starting with \p prefix, or nullopt when none exists.
inline std::optional<std::string> byte_successor(std::string prefix) {
   while (!prefix.empty()) {
      if (static_cast<uint8_t>(prefix.back()) != 0xFF) {
         prefix.back() = static_cast<char>(static_cast<uint8_t>(prefix.back()) + 1);
         return prefix;
      }
      prefix.pop_back();
   }
   return std::nullopt;
}

inline std::string as_string(const void* data, uint32_t size) {
   return size ? std::string(static_cast<const char*>(data), size) : std::string();
}

/// Copy from \p src at \p offset. A zero-size buffer skips the copy, a short one truncates, and an
/// offset at or past the end copies nothing.
inline void serve(const std::string& src, uint32_t offset, void* dest, uint32_t dest_size) {
   if (dest_size > 0 && offset < src.size())
      std::memcpy(dest, src.data() + offset, std::min<size_t>(dest_size, src.size() - offset));
}

struct store {
   using row_key = std::tuple<uint64_t, uint32_t, std::string>;                // code, table_id, key
   using idx_key = std::tuple<uint64_t, uint32_t, std::string, std::string>;   // code, table_id, sec, pri

   struct primary_slot {
      bool        open = false;
      uint64_t    code = 0;
      uint32_t    table_id = 0;
      std::string prefix;
      std::string key;
      int32_t     status = kv_status_end;
   };

   struct secondary_slot {
      bool        open = false;
      uint64_t    code = 0;
      uint32_t    table_id = 0;
      std::string prefix;
      std::string sec;
      std::string pri;
      int32_t     status = kv_status_end;
   };

   std::map<row_key, std::string> rows;
   std::map<row_key, uint64_t>    row_payers;
   std::map<idx_key, uint64_t>    entries;   // value is the payer
   std::vector<primary_slot>      primary;
   std::vector<secondary_slot>    secondary;
   std::map<std::string, uint32_t> counts;
   uint32_t receiver_calls = 0;
   uint64_t receiver = 0;
   uint64_t last_payer = 0;
   bool     read_only = false;   ///< run as a read-only transaction: every write aborts
   uint32_t max_key_size = default_max_key_size;
   uint32_t max_value_size = default_max_value_size;
   uint32_t max_secondary_key_size = default_max_secondary_key_size;

   /// Calls made to one intrinsic since the last reset() or clear_counts().
   uint32_t calls(const std::string& name) const {
      auto it = counts.find(name);
      return it == counts.end() ? 0 : it->second;
   }

   /// KV calls made since the last reset() or clear_counts(), whichever intrinsic took them.
   uint32_t total() const {
      uint32_t sum = 0;
      for (const auto& entry : counts) sum += entry.second;
      return sum;
   }

   /// KV handles still open. Non-zero after every iterator is gone means a wrapper leaked one.
   uint32_t open_handles() const {
      uint32_t n = 0;
      for (const auto& slot : primary)   n += slot.open;
      for (const auto& slot : secondary) n += slot.open;
      return n;
   }

   void clear_counts() { counts.clear(); receiver_calls = 0; }

   void reset(uint64_t who) {
      rows.clear();
      row_payers.clear();
      entries.clear();
      primary.clear();
      secondary.clear();
      clear_counts();
      receiver = who;
      last_payer = 0;
      read_only = false;
      max_key_size = default_max_key_size;
      max_value_size = default_max_value_size;
      max_secondary_key_size = default_max_secondary_key_size;
   }

   /// Seed a row directly, past the write path and its counters.
   void seed(uint64_t code, uint32_t table_id, const std::string& key, const std::string& value) {
      const row_key at{code, table_id, key};
      rows[at] = value;
      row_payers[at] = code;
   }

   /// Seed an index entry directly, past the write path and its counters.
   void seed_entry(uint64_t code, uint32_t table_id, const std::string& sec, const std::string& pri) {
      entries[idx_key{code, table_id, sec, pri}] = code;
   }

   primary_slot& primary_at(int32_t handle, const char* op) {
      const uint32_t slot = validate_handle(handle, false, op);
      sysio::check(slot < primary.size() && primary[slot].open,
                   "invalid KV primary iterator slot " + std::to_string(slot));
      return primary[slot];
   }

   secondary_slot& secondary_at(int32_t handle, const char* op) {
      const uint32_t slot = validate_handle(handle, true, op);
      sysio::check(slot < secondary.size() && secondary[slot].open,
                   "invalid KV secondary iterator slot " + std::to_string(slot));
      return secondary[slot];
   }

   /// Land a primary slot on \p at when it is inside the slot's table and prefix, else on end.
   void settle_primary(primary_slot& slot, std::map<row_key, std::string>::const_iterator at) {
      if (at != rows.end() && std::get<0>(at->first) == slot.code && std::get<1>(at->first) == slot.table_id &&
          std::get<2>(at->first).compare(0, slot.prefix.size(), slot.prefix) == 0) {
         slot.status = kv_status_ok;
         slot.key = std::get<2>(at->first);
      } else {
         slot.status = kv_status_end;
         slot.key.clear();
      }
   }

   /// Land a secondary slot on \p at when it is inside the slot's index and prefix, else on end.
   void settle_secondary(secondary_slot& slot, std::map<idx_key, uint64_t>::const_iterator at) {
      if (at != entries.end() && std::get<0>(at->first) == slot.code && std::get<1>(at->first) == slot.table_id &&
          std::get<2>(at->first).compare(0, slot.prefix.size(), slot.prefix) == 0) {
         slot.status = kv_status_ok;
         slot.sec = std::get<2>(at->first);
         slot.pri = std::get<3>(at->first);
      } else {
         slot.status = kv_status_end;
         slot.sec.clear();
         slot.pri.clear();
      }
   }

   /// Move a primary slot to the first row at or after \p seek, clamped up to its prefix.
   void seek_primary(primary_slot& slot, const std::string& seek) {
      settle_primary(slot, rows.lower_bound(row_key{slot.code, slot.table_id, std::max(seek, slot.prefix)}));
   }

   /// Move a secondary slot to the first entry at or after \p seek, clamped up to its prefix.
   void seek_secondary(secondary_slot& slot, const std::string& seek) {
      settle_secondary(slot, entries.lower_bound(
         idx_key{slot.code, slot.table_id, std::max(seek, slot.prefix), std::string()}));
   }

   void step_primary(primary_slot& slot) {
      if (slot.status == kv_status_end) return;
      // Strictly past the current key, whether or not the row is still there.
      settle_primary(slot, rows.upper_bound(row_key{slot.code, slot.table_id, slot.key}));
   }

   void step_back_primary(primary_slot& slot) {
      std::map<row_key, std::string>::const_iterator at;
      if (slot.status == kv_status_end) {
         // From end, to the last row of the prefix range.
         const auto succ = byte_successor(slot.prefix);
         at = succ ? rows.lower_bound(row_key{slot.code, slot.table_id, *succ})
                   : rows.lower_bound(row_key{slot.code, slot.table_id + 1, std::string()});
      } else {
         at = rows.lower_bound(row_key{slot.code, slot.table_id, slot.key});
      }
      settle_primary(slot, at == rows.begin() ? rows.end() : std::prev(at));
   }

   void step_secondary(secondary_slot& slot) {
      if (slot.status == kv_status_end) return;
      settle_secondary(slot, entries.upper_bound(idx_key{slot.code, slot.table_id, slot.sec, slot.pri}));
   }

   void step_back_secondary(secondary_slot& slot) {
      std::map<idx_key, uint64_t>::const_iterator at;
      if (slot.status == kv_status_end) {
         const auto succ = byte_successor(slot.prefix);
         at = succ ? entries.lower_bound(idx_key{slot.code, slot.table_id, *succ, std::string()})
                   : entries.lower_bound(idx_key{slot.code, slot.table_id + 1, std::string(), std::string()});
      } else {
         at = entries.lower_bound(idx_key{slot.code, slot.table_id, slot.sec, slot.pri});
      }
      settle_secondary(slot, at == entries.begin() ? entries.end() : std::prev(at));
   }

   /// What a read of a primary slot reports. Writes nothing on a miss but the sizes, which are 0.
   int32_t read_primary(primary_slot& slot, uint32_t offset, void* key, uint32_t key_capacity, void* value,
                        uint32_t value_capacity, uint32_t* key_size, uint32_t* value_size) {
      *key_size = 0;
      *value_size = 0;
      if (slot.status != kv_status_ok) return slot.status;
      const auto at = rows.find(row_key{slot.code, slot.table_id, slot.key});
      if (at == rows.end()) return kv_status_no_row;
      serve(slot.key, 0, key, key_capacity);
      serve(at->second, offset, value, value_capacity);
      *key_size = static_cast<uint32_t>(slot.key.size());
      *value_size = static_cast<uint32_t>(at->second.size());
      return kv_status_ok;
   }

   /// What a read of a secondary slot reports, on the same rules.
   int32_t read_secondary(secondary_slot& slot, void* sec, uint32_t sec_capacity, void* pri, uint32_t pri_capacity,
                          uint32_t* sec_size, uint32_t* pri_size) {
      *sec_size = 0;
      *pri_size = 0;
      if (slot.status != kv_status_ok) return slot.status;
      if (!entries.count(idx_key{slot.code, slot.table_id, slot.sec, slot.pri})) return kv_status_no_row;
      serve(slot.sec, 0, sec, sec_capacity);
      serve(slot.pri, 0, pri, pri_capacity);
      *sec_size = static_cast<uint32_t>(slot.sec.size());
      *pri_size = static_cast<uint32_t>(slot.pri.size());
      return kv_status_ok;
   }

   /// The lowest free slot of a pool, growing it up to the limit each pool has of its own.
   template<typename Slot>
   static uint32_t allocate(std::vector<Slot>& pool, const char* pool_name) {
      for (uint32_t i = 0; i < pool.size(); ++i)
         if (!pool[i].open) return i;
      sysio::check(pool.size() < max_iterators, std::string("exceeded maximum number of KV ") + pool_name +
                                                   " iterators (" + std::to_string(max_iterators) + ")");
      pool.emplace_back();
      return static_cast<uint32_t>(pool.size() - 1);
   }

   uint32_t allocate_primary() { return allocate(primary, "primary"); }
   uint32_t allocate_secondary() { return allocate(secondary, "secondary"); }
};

inline store& host() {
   static store inst;
   return inst;
}

/// Install the mocked host functions. Call once per case, after host().reset().
inline void install() {
   using namespace sysio::native;

   intrinsics::set_intrinsic<intrinsics::current_receiver>([]() -> capi_name {
      ++host().receiver_calls;
      return host().receiver;
   });

   intrinsics::set_intrinsic<intrinsics::kv_set>(
      [](uint32_t table_id, uint64_t payer, const void* key, uint32_t key_size, const void* value,
         uint32_t value_size, uint32_t mode) -> int32_t {
         auto& m = host();
         ++m.counts["kv_set"];
         check_table_id(table_id);
         sysio::check(mode <= kv_set_update, "kv_set mode " + std::to_string(mode) +
                                                " is not upsert, insert or update");
         sysio::check(!m.read_only, "cannot store a KV record when executing a readonly transaction");
         sysio::check(key_size > 0, "KV key must not be empty");
         check_size("key", key_size, m.max_key_size);
         check_size("value", value_size, m.max_value_size);
         m.last_payer = payer;
         const store::row_key at{m.receiver, table_id, as_string(key, key_size)};
         const bool found = m.rows.find(at) != m.rows.end();
         if (mode == kv_set_insert && found) return kv_status_exists;
         if (mode == kv_set_update && !found) return kv_status_no_row;
         if (!found) sysio::check(payer != 0, "must specify a valid account to pay for new record");
         if (payer != 0) m.row_payers[at] = payer;
         m.rows[at] = as_string(value, value_size);
         return kv_status_ok;
      });

   intrinsics::set_intrinsic<intrinsics::kv_erase>(
      [](uint32_t table_id, const void* key, uint32_t key_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_erase"];
         check_table_id(table_id);
         sysio::check(!m.read_only, "cannot erase a KV record when executing a readonly transaction");
         sysio::check(key_size > 0, "KV key must not be empty");
         const store::row_key key_at{m.receiver, table_id, as_string(key, key_size)};
         const auto at = m.rows.find(key_at);
         if (at == m.rows.end()) return kv_status_no_row;
         m.rows.erase(at);
         m.row_payers.erase(key_at);
         return kv_status_ok;
      });

   intrinsics::set_intrinsic<intrinsics::kv_get>(
      [](uint32_t table_id, capi_name code, const void* key, uint32_t key_size, uint32_t offset, void* value,
         uint32_t value_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_get"];
         check_table_id(table_id);
         sysio::check(key_size > 0, "KV key must not be empty");
         const auto at = m.rows.find(store::row_key{code, table_id, as_string(key, key_size)});
         if (at == m.rows.end()) return -1;
         serve(at->second, offset, value, value_size);
         return static_cast<int32_t>(at->second.size());
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_store>(
      [](uint32_t table_id, uint64_t payer, const void* pri, uint32_t pri_size, const void* sec,
         uint32_t sec_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_store"];
         check_table_id(table_id);
         sysio::check(!m.read_only, "cannot store a KV index when executing a readonly transaction");
         sysio::check(sec_size > 0, "KV secondary key must not be empty");
         check_size("secondary key", sec_size, m.max_secondary_key_size);
         sysio::check(pri_size > 0, "KV primary key must not be empty");
         check_size("primary key", pri_size, m.max_key_size);
         const store::idx_key at{m.receiver, table_id, as_string(sec, sec_size), as_string(pri, pri_size)};
         // Payer 0 fails only when it would create an entry.
         if (m.entries.count(at)) return kv_status_exists;
         sysio::check(payer != 0, "must specify a valid account to pay for new record");
         m.entries[at] = payer;
         return kv_status_ok;
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_erase>(
      [](uint32_t table_id, const void* pri, uint32_t pri_size, const void* sec, uint32_t sec_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_erase"];
         check_table_id(table_id);
         sysio::check(!m.read_only, "cannot remove a KV index when executing a readonly transaction");
         sysio::check(pri_size > 0, "KV primary key must not be empty");
         sysio::check(sec_size > 0, "KV secondary key must not be empty");
         const auto at = m.entries.find(
            store::idx_key{m.receiver, table_id, as_string(sec, sec_size), as_string(pri, pri_size)});
         if (at == m.entries.end()) return kv_status_no_row;
         m.entries.erase(at);
         return kv_status_ok;
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_update>(
      [](uint32_t table_id, uint64_t payer, const void* pri, uint32_t pri_size, const void* old_sec,
         uint32_t old_sec_size, const void* new_sec, uint32_t new_sec_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_update"];
         check_table_id(table_id);
         sysio::check(!m.read_only, "cannot update a KV index when executing a readonly transaction");
         sysio::check(pri_size > 0, "KV primary key must not be empty");
         sysio::check(old_sec_size > 0, "KV secondary key must not be empty");
         sysio::check(new_sec_size > 0, "KV secondary key must not be empty");
         check_size("secondary key", new_sec_size, m.max_secondary_key_size);
         const auto pri_key = as_string(pri, pri_size);
         const auto at = m.entries.find(
            store::idx_key{m.receiver, table_id, as_string(old_sec, old_sec_size), pri_key});
         if (at == m.entries.end()) return kv_status_no_row;
         const store::idx_key moved{m.receiver, table_id, as_string(new_sec, new_sec_size), pri_key};
         if (m.entries.count(moved) && moved != at->first) return kv_status_exists;
         const uint64_t kept = payer ? payer : at->second;
         m.entries.erase(at);
         m.entries[moved] = kept;
         return kv_status_ok;
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_create>(
      [](uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size, const void* seek,
         uint32_t seek_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_it_create"];
         check_table_id(table_id);
         check_prefix_size(prefix_size);
         const uint32_t handle = m.allocate_primary();
         auto& slot = m.primary[handle];
         slot = store::primary_slot{true, code, table_id, as_string(prefix, prefix_size), {}, kv_status_end};
         m.seek_primary(slot, as_string(seek, seek_size));
         return static_cast<int32_t>(handle);
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_next>(
      [](int32_t handle, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity, uint32_t* key_size,
         uint32_t* value_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_it_next"];
         auto& slot = m.primary_at(handle, "kv_it_next");
         m.step_primary(slot);
         return m.read_primary(slot, 0, key, key_capacity, value, value_capacity, key_size, value_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_prev>(
      [](int32_t handle, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity, uint32_t* key_size,
         uint32_t* value_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_it_prev"];
         auto& slot = m.primary_at(handle, "kv_it_prev");
         m.step_back_primary(slot);
         return m.read_primary(slot, 0, key, key_capacity, value, value_capacity, key_size, value_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_lower_bound>(
      [](int32_t handle, const void* seek, uint32_t seek_size, void* key, uint32_t key_capacity, void* value,
         uint32_t value_capacity, uint32_t* key_size, uint32_t* value_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_it_lower_bound"];
         auto& slot = m.primary_at(handle, "kv_it_lower_bound");
         m.seek_primary(slot, as_string(seek, seek_size));
         return m.read_primary(slot, 0, key, key_capacity, value, value_capacity, key_size, value_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_read>(
      [](int32_t handle, uint32_t offset, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity,
         uint32_t* key_size, uint32_t* value_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_it_read"];
         return m.read_primary(m.primary_at(handle, "kv_it_read"), offset, key, key_capacity, value, value_capacity,
                               key_size, value_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_it_destroy>([](int32_t handle) {
      auto& m = host();
      ++m.counts["kv_it_destroy"];
      m.primary_at(handle, "kv_it_destroy").open = false;
   });

   intrinsics::set_intrinsic<intrinsics::kv_idx_create>(
      [](uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size, const void* seek,
         uint32_t seek_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_create"];
         check_table_id(table_id);
         check_prefix_size(prefix_size);
         const uint32_t handle = m.allocate_secondary();
         auto& slot = m.secondary[handle];
         slot = store::secondary_slot{true, code, table_id, as_string(prefix, prefix_size), {}, {}, kv_status_end};
         m.seek_secondary(slot, as_string(seek, seek_size));
         return static_cast<int32_t>(handle | secondary_handle_tag);
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_next>(
      [](int32_t handle, void* sec, uint32_t sec_capacity, void* pri, uint32_t pri_capacity, uint32_t* sec_size,
         uint32_t* pri_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_next"];
         auto& slot = m.secondary_at(handle, "kv_idx_next");
         m.step_secondary(slot);
         return m.read_secondary(slot, sec, sec_capacity, pri, pri_capacity, sec_size, pri_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_prev>(
      [](int32_t handle, void* sec, uint32_t sec_capacity, void* pri, uint32_t pri_capacity, uint32_t* sec_size,
         uint32_t* pri_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_prev"];
         auto& slot = m.secondary_at(handle, "kv_idx_prev");
         m.step_back_secondary(slot);
         return m.read_secondary(slot, sec, sec_capacity, pri, pri_capacity, sec_size, pri_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_lower_bound>(
      [](int32_t handle, const void* seek, uint32_t seek_size, void* sec, uint32_t sec_capacity, void* pri,
         uint32_t pri_capacity, uint32_t* sec_size, uint32_t* pri_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_lower_bound"];
         auto& slot = m.secondary_at(handle, "kv_idx_lower_bound");
         m.seek_secondary(slot, as_string(seek, seek_size));
         return m.read_secondary(slot, sec, sec_capacity, pri, pri_capacity, sec_size, pri_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_read>(
      [](int32_t handle, void* sec, uint32_t sec_capacity, void* pri, uint32_t pri_capacity, uint32_t* sec_size,
         uint32_t* pri_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_read"];
         return m.read_secondary(m.secondary_at(handle, "kv_idx_read"), sec, sec_capacity, pri, pri_capacity,
                                 sec_size, pri_size);
      });

   intrinsics::set_intrinsic<intrinsics::kv_idx_destroy>([](int32_t handle) {
      auto& m = host();
      ++m.counts["kv_idx_destroy"];
      m.secondary_at(handle, "kv_idx_destroy").open = false;
   });

   intrinsics::set_intrinsic<intrinsics::kv_idx_find>(
      [](uint32_t table_id, capi_name code, const void* prefix, uint32_t prefix_size, const void* sec,
         uint32_t sec_size) -> int32_t {
         auto& m = host();
         ++m.counts["kv_idx_find"];
         check_table_id(table_id);
         check_prefix_size(prefix_size);
         sysio::check(sec_size > 0, "KV secondary key must not be empty");
         const auto wanted = as_string(sec, sec_size);
         const auto bound = as_string(prefix, prefix_size);
         sysio::check(wanted.compare(0, bound.size(), bound) == 0, "KV secondary key does not start with the prefix");
         const auto at = m.entries.lower_bound(store::idx_key{code, table_id, wanted, std::string()});
         if (at == m.entries.end() || std::get<0>(at->first) != code || std::get<1>(at->first) != table_id ||
             std::get<2>(at->first) != wanted)
            return -1;
         const uint32_t handle = m.allocate_secondary();
         auto& slot = m.secondary[handle];
         slot = store::secondary_slot{true, code, table_id, bound, {}, {}, kv_status_end};
         m.settle_secondary(slot, at);
         return static_cast<int32_t>(handle | secondary_handle_tag);
      });
}

} // namespace kv_mock
