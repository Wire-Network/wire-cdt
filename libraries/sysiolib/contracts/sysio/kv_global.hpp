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

   uint64_t code() const { return _code ? _code : sysio::current_receiver().value; }

   // Fixed key: big-endian encoding of the Name.
   // 8 bytes, deterministic, no scope.
   static constexpr uint32_t key_size = sizeof(uint64_t);
   struct key_t {
      char data[key_size];
   };

   static key_t make_key() {
      key_t k;
      uint64_t v = static_cast<uint64_t>(Name);
      for (int i = key_size - 1; i >= 0; --i) { k.data[i] = static_cast<char>(v & 0xFF); v >>= 8; }
      return k;
   }

public:
   /// Construct a global. Reads/writes against \p code's KV data (default: current contract).
   global(sysio::name code = sysio::name{}) : _code(code.value) {}

   /// Returns true if a value has been stored.
   bool exists() const {
      auto k = make_key();
      return ::kv_contains(_table_id, code(), k.data, key_size) != 0;
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
   /// instead of paying kv_contains followed by kv_get.
   bool try_get(T& out) const {
      auto k = make_key();
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         int32_t sz = ::kv_get(_table_id, code(), k.data, key_size, vbuf, sizeof(T));
         if (sz < 0) return false;
         std::memcpy(&out, vbuf, sizeof(T));
      } else {
         char stack[kv_value_stack_size];
         int32_t sz = ::kv_get(_table_id, code(), k.data, key_size, stack, kv_value_stack_size);
         if (sz < 0) return false;
         if (sz <= static_cast<int32_t>(kv_value_stack_size)) {
            sysio::datastream<const char*> ds(stack, sz);
            ds >> out;
         } else {
            char* heap = new char[sz];
            ::kv_get(_table_id, code(), k.data, key_size, heap, sz);
            sysio::datastream<const char*> ds(heap, sz);
            ds >> out;
            delete[] heap;
         }
      }
      return true;
   }

   /// Stores or overwrites the value.
   void set(const T& val, sysio::name payer) {
      auto k = make_key();
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         std::memcpy(vbuf, &val, sizeof(T));
         ::kv_set(_table_id, payer.value, k.data, key_size, vbuf, sizeof(T));
      } else {
         uint32_t sz = sysio::pack_size(val);
         ser_buf buf(sz);
         sysio::datastream<char*> ds(buf.data(), sz);
         ds << val;
         ::kv_set(_table_id, payer.value, k.data, key_size, buf.data(), sz);
      }
   }

   /// Removes the stored value. Safe to call even if not set.
   void remove() {
      auto k = make_key();
      // kv_erase always operates on current_receiver, so check receiver's data
      if (!::kv_contains(_table_id, sysio::current_receiver().value, k.data, key_size)) return;
      ::kv_erase(_table_id, k.data, key_size);
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
template<name::raw Name, typename T>
using cached_global = cached_value<global<Name, T>>;

}} // namespace sysio::kv
