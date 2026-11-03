#pragma once
/**
 * sysio::kv::global — Non-scoped singleton backed by a single KV entry under a dedicated table_id.
 *
 * Stores exactly one value per contract, keyed by the table name alone.
 * No scope parameter — the entry is global to the contract account.
 *
 * Uses is_fixed_serializable_v<T> for compile-time zero-copy when T is POD.
 *
 * Usage:
 *   struct config {
 *      uint64_t rate;
 *      uint32_t flags;
 *      SYSLIB_SERIALIZE(config, (rate)(flags))
 *   };
 *
 *   kv::global<"config"_n, config> cfg(get_self());
 *   cfg.set({42, 0xFF}, get_self());
 *   auto val = cfg.get();
 */

#include <sysio/kv_utils.hpp>
#include <sysio/check.hpp>
#include <sysio/name.hpp>
#include <sysio/action.hpp>
#include <sysio/kv_cached.hpp>

#include <cstring>
#include <optional>

namespace sysio { namespace kv {

template<name::raw Name, typename T>
class global {
   static_assert(std::is_default_constructible_v<T>, "global value type must be default constructible");

public:
   /// Payload type. Lets generic wrappers (kv::cached_value) deduce it without repetition.
   using value_type = T;

private:
   static constexpr uint32_t _table_id = sysio::kv::compute_table_id(static_cast<uint64_t>(Name));
   uint64_t _code = 0;

   uint64_t code() const { return _code ? _code : kv::detail::receiving_account().value; }

   // Fixed key: big-endian encoding of the Name.
   // 8 bytes, deterministic, no scope.
   static constexpr uint32_t key_size = sizeof(uint64_t);
   struct key_t {
      char data[key_size];
   };

   static key_t make_key() {
      key_t k;
      encode_be64(k.data, static_cast<uint64_t>(Name));
      return k;
   }

public:
   /// Construct a global. Reads/writes against \p code's KV data (default: current contract).
   global(sysio::name code = sysio::name{}) : _code(code.value) {}

   /// Returns true if a value has been stored. One kv_get with an empty buffer, which reads
   /// nothing and reports whether the row exists.
   bool exists() const {
      auto k = make_key();
      return ::kv_get(_table_id, code(), k.data, key_size, 0, nullptr, 0) >= 0;
   }

   /// Returns the stored value. Asserts if not set.
   T get(const char* msg = "global does not exist") const {
      T val;
      sysio::check(try_get(val), msg);
      return val;
   }

   /// Returns the stored value, or \p def if not set.
   T get_or_default(const T& def) const {
      T val;
      if (try_get(val)) return val;
      return def;
   }

   /// Returns the stored value if it exists. Otherwise stores \p def and returns it.
   T get_or_create(sysio::name payer, const T& def) {
      T val;
      if (try_get(val)) return val;
      set(def, payer);
      return def;
   }

   /// Single kv_get call — returns true if found, populates \p out.
   /// Public so generic wrappers (kv::cached_value) load through this single-kv_get path
   /// instead of paying exists() followed by a read.
   bool try_get(T& out) const {
      auto k = make_key();
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         int32_t sz = ::kv_get(_table_id, code(), k.data, key_size, 0, vbuf, sizeof(T));
         if (sz < 0) return false;
         // kv_get fills min(buffer, stored) bytes but returns the FULL stored size, so a row that is
         // not exactly sizeof(T) must be rejected rather than memcpy'd. Copying sizeof(T) out of a
         // partially filled buffer splices whatever this contract's linear memory happened to hold
         // into the payload, so the caller silently gets stale bytes where it expects stored data.
         // This is not a divergence risk -- linear memory is deterministic, so every node builds the
         // same wrong value -- but a wrong value that reads as authoritative is worse than a trap. A
         // size mismatch means the row was written by an incompatible version of this type, so trap
         // rather than report "absent": silently treating a real row as missing invites the caller
         // to overwrite it.
         sysio::check(sz == static_cast<int32_t>(sizeof(T)),
                      "kv::global: stored value size does not match the fixed-serializable payload");
         std::memcpy(&out, vbuf, sizeof(T));
         return true;
      } else {
         return detail::read_row<kv_value_stack_size>(_table_id, code(), k.data, key_size,
                                                      [&](const char* data, uint32_t size) {
                                                         sysio::datastream<const char*> ds(data, size);
                                                         ds >> out;
                                                      });
      }
   }

   /**
    * Stores or overwrites the value.
    *
    * WRITES IGNORE code(). kv_get takes a code parameter, so reads honour whatever
    * account this handle was constructed with; kv_set and kv_erase have no such parameter and
    * always land on the current receiver. A handle opened on a FOREIGN account is therefore
    * read-only in practice -- calling this on one reads their row and writes your own. Nothing
    * detects that at compile time, and no write checks it at run time, so it is left to the
    * caller: construct foreign-code handles for reading only.
    */
   void set(const T& val, sysio::name payer) {
      auto k = make_key();
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         std::memcpy(vbuf, &val, sizeof(T));
         sysio::check(::kv_set(_table_id, payer.value, k.data, key_size, vbuf, sizeof(T),
                               kv_set_upsert) == kv_status_ok, msg::write_refused);
      } else {
         uint32_t sz = sysio::pack_size(val);
         ser_buf buf(sz);
         sysio::datastream<char*> ds(buf.data(), sz);
         ds << val;
         sysio::check(::kv_set(_table_id, payer.value, k.data, key_size, buf.data(), sz,
                               kv_set_upsert) == kv_status_ok, msg::write_refused);
      }
   }

   /// Removes the stored value, in one kv_erase. A silent no-op if not set: a missing row is the
   /// one state this call accepts, so it is the one status that passes. kv_erase is a write whether
   /// or not it finds a row, so in a read-only transaction this aborts even when nothing is set.
   void remove() {
      auto k = make_key();
      const int32_t status = ::kv_erase(_table_id, k.data, key_size);
      sysio::check(status == kv_status_ok || status == kv_status_no_row, msg::write_refused);
   }
};

/**
 * Write-deferring kv::global.
 *
 * Reads never issue a kv_set, so an action that only reads the singleton stays legal inside
 * a read-only transaction. Use this in place of caching the value in a contract member and
 * writing it back from the contract destructor. See kv_cached.hpp for the rationale and for
 * the visibility rules that come with deferred writes.
 */
template<name::raw Name, typename T, T (*MakeDefault)() = nullptr>
using cached_global = cached_value<global<Name, T>, MakeDefault>;

}} // namespace sysio::kv
