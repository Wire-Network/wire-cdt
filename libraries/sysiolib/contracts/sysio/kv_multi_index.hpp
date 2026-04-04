#pragma once
/**
 * KV-backed multi_index emulation layer.
 *
 * Drop-in replacement for sysio::multi_index that uses KV intrinsics instead
 * of legacy db_*_i64 intrinsics. Same template API, different backend.
 *
 * Key encoding: [table_name: 8B BE][scope: 8B BE][primary_key: 8B BE] = 24 bytes
 * This enables the integer fast-path comparator in kv_object.
 *
 * The payer parameter is honored — RAM is charged to the specified payer,
 * matching the behavior of the legacy sysio::multi_index.
 */

#include <cstdint>

// KV intrinsic declarations (primary + secondary — multi_index uses both)
extern "C" {
   __attribute__((sysio_wasm_import))
   int64_t kv_set(uint32_t key_format, uint64_t payer, const void* key, uint32_t key_size, const void* value, uint32_t value_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_get(uint32_t key_format, uint64_t code, const void* key, uint32_t key_size, void* value, uint32_t value_size);
   __attribute__((sysio_wasm_import))
   int64_t kv_erase(uint32_t key_format, const void* key, uint32_t key_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_contains(uint32_t key_format, uint64_t code, const void* key, uint32_t key_size);
   __attribute__((sysio_wasm_import))
   uint32_t kv_it_create(uint32_t key_format, uint64_t code, const void* prefix, uint32_t prefix_size);
   __attribute__((sysio_wasm_import))
   void kv_it_destroy(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_status(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_next(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_prev(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_lower_bound(uint32_t handle, const void* key, uint32_t key_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_it_value(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_store(uint64_t payer, uint64_t table, uint32_t index_id,
                     const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_remove(uint64_t table, uint32_t index_id,
                      const void* pri_key, uint32_t pri_key_size,
                      const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_update(uint64_t payer, uint64_t table, uint32_t index_id,
                      const void* pri_key, uint32_t pri_key_size,
                      const void* old_sec_key, uint32_t old_sec_key_size,
                      const void* new_sec_key, uint32_t new_sec_key_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_find_secondary(uint64_t code, uint64_t table, uint32_t index_id,
                                 const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_lower_bound(uint64_t code, uint64_t table, uint32_t index_id,
                              const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_next(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_prev(uint32_t handle);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_destroy(uint32_t handle);
}

#include <sysio/name.hpp>
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/check.hpp>
#include <sysio/action.hpp>

#include <vector>
#include <memory>
#include <map>
#include <cstring>
#include <type_traits>
#include <limits>
#include <iterator>

#include <sysio/kv_constants.hpp>

namespace sysio {

using sysio::kv::kv_format_raw;
using sysio::kv::kv_format_standard;

// Type definitions needed by contracts using secondary indices.
// These are pure type templates with no legacy db_* dependencies.
template<name::raw IndexName, typename Extractor>
struct indexed_by {
   enum constants { index_name = static_cast<uint64_t>(IndexName) };
   typedef Extractor secondary_extractor_type;
};

template<class Class, typename Type, Type (Class::*PtrToMemberFunction)()const>
struct const_mem_fun {
   typedef typename std::remove_reference<Type>::type result_type;
   Type operator()(const Class& x) const { return (x.*PtrToMemberFunction)(); }
   Type operator()(const Class* x) const { return (x->*PtrToMemberFunction)(); }
};

#ifndef SYSIO_SAME_PAYER_DEFINED
#define SYSIO_SAME_PAYER_DEFINED
inline constexpr name same_payer{};
#endif

namespace _kv_multi_index_detail {

   inline void encode_be64(char* buf, uint64_t v) {
      for (int i = 7; i >= 0; --i) {
         buf[i] = static_cast<char>(v & 0xFF);
         v >>= 8;
      }
   }

   inline uint64_t decode_be64(const char* buf) {
      uint64_t v = 0;
      for (int i = 0; i < 8; ++i) {
         v = (v << 8) | static_cast<uint8_t>(buf[i]);
      }
      return v;
   }

   // Encode a secondary key to bytes for kv_idx_store
   template<typename T>
   inline std::vector<char> encode_secondary(const T& key) {
      auto sz = pack_size(key);
      std::vector<char> buf(sz);
      datastream<char*> ds(buf.data(), buf.size());
      ds << key;
      return buf;
   }

   // Encode uint64_t as 8-byte big-endian (for secondary index)
   template<>
   inline std::vector<char> encode_secondary(const uint64_t& key) {
      std::vector<char> buf(8);
      encode_be64(buf.data(), key);
      return buf;
   }

   // Encode uint128_t as 16-byte big-endian
   template<>
   inline std::vector<char> encode_secondary(const uint128_t& key) {
      std::vector<char> buf(16);
      encode_be64(buf.data(), static_cast<uint64_t>(key >> 64));
      encode_be64(buf.data() + 8, static_cast<uint64_t>(key));
      return buf;
   }

   // Encode double as sort-preserving 8 bytes
   template<>
   inline std::vector<char> encode_secondary(const double& key) {
      uint64_t bits;
      memcpy(&bits, &key, 8);
      // IEEE 754 sign-magnitude → unsigned sortable
      if (bits & (uint64_t(1) << 63))
         bits = ~bits; // negative: flip all bits
      else
         bits ^= (uint64_t(1) << 63); // positive: flip sign bit only
      std::vector<char> buf(8);
      encode_be64(buf.data(), bits);
      return buf;
   }

   // Encode long double (128-bit softfloat in WASM) as sort-preserving 16 bytes.
   // WASM stores long double as IEEE 754 binary128 in little-endian.
   // Convert to big-endian and apply sign-magnitude → unsigned sortable transform.
   template<>
   inline std::vector<char> encode_secondary(const long double& key) {
      char raw[16];
      memcpy(raw, &key, 16);
      // Convert little-endian to big-endian
      std::vector<char> buf(16);
      for (int i = 0; i < 16; ++i)
         buf[i] = raw[15 - i];
      // Sign-magnitude to unsigned sortable (sign bit is now buf[0] bit 7)
      if (static_cast<uint8_t>(buf[0]) & 0x80u)
         for (int i = 0; i < 16; ++i) buf[i] = ~buf[i]; // negative: flip all
      else
         buf[0] = static_cast<char>(static_cast<uint8_t>(buf[0]) ^ 0x80u); // positive: flip sign
      return buf;
   }

} // namespace _kv_multi_index_detail

// Uses sysio::indexed_by and sysio::const_mem_fun from the standard CDT headers.
// This class is a drop-in replacement: just change multi_index -> kv_multi_index.

template<name::raw TableName, typename T, typename... Indices>
class kv_multi_index {
   static_assert(sizeof...(Indices) <= 16, "multi_index supports at most 16 secondary indices");

   // Helper: convert primary_key() result to uint64_t regardless of return type (uint64_t or name)
   static uint64_t to_pk_uint64(uint64_t pk) { return pk; }
   static uint64_t to_pk_uint64(name pk) { return pk.value; }

   name     _code;
   uint64_t _scope;
   mutable uint64_t _next_primary_key = 0;
   mutable bool     _next_primary_key_cached = false;

   // Cached items (deserialized rows)
   mutable std::map<uint64_t, std::unique_ptr<T>> _items;

   // --- Key encoding ---
   struct primary_key_buf {
      char data[24];
   };

   primary_key_buf make_pk(uint64_t pk) const {
      primary_key_buf key;
      _kv_multi_index_detail::encode_be64(key.data,      static_cast<uint64_t>(TableName));
      _kv_multi_index_detail::encode_be64(key.data + 8,  _scope);
      _kv_multi_index_detail::encode_be64(key.data + 16, pk);
      return key;
   }

   // 16-byte prefix for iteration (table + scope)
   struct prefix_buf {
      char data[16];
   };

   prefix_buf make_prefix() const {
      prefix_buf p;
      _kv_multi_index_detail::encode_be64(p.data,     static_cast<uint64_t>(TableName));
      _kv_multi_index_detail::encode_be64(p.data + 8, _scope);
      return p;
   }

   // 8-byte primary key for secondary index: [pk:8B]
   // Scope is encoded in the sec_key prefix instead (see store_all).
   struct pk_bytes_buf {
      char data[8];
   };

   pk_bytes_buf pk_to_bytes(uint64_t pk) const {
      pk_bytes_buf b;
      _kv_multi_index_detail::encode_be64(b.data, pk);
      return b;
   }

   // --- Row serialization (zero-copy for trivially_copyable types) ---
   static std::vector<char> serialize_row(const T& obj) {
      if constexpr (std::is_trivially_copyable<T>::value) {
         if (sizeof(T) == pack_size(obj)) {
            std::vector<char> buf(sizeof(T));
            memcpy(buf.data(), &obj, sizeof(T));
            return buf;
         }
      }
      auto sz = pack_size(obj);
      std::vector<char> buf(sz);
      datastream<char*> ds(buf.data(), buf.size());
      ds << obj;
      return buf;
   }

   static T deserialize_row(const char* data, size_t size) {
      T obj;
      if constexpr (std::is_trivially_copyable<T>::value) {
         if (size == sizeof(T)) {
            memcpy(&obj, data, sizeof(T));
            return obj;
         }
      }
      datastream<const char*> ds(data, size);
      ds >> obj;
      return obj;
   }

   T* load_object(uint64_t pk) const {
      auto it = _items.find(pk);
      if (it != _items.end()) return it->second.get();

      auto key = make_pk(pk);
      int32_t sz = ::kv_get(kv_format_standard, _code.value, key.data, 24, nullptr, 0);
      if (sz < 0) return nullptr;

      std::vector<char> buf(sz);
      ::kv_get(kv_format_standard, _code.value, key.data, 24, buf.data(), buf.size());

      auto ptr = std::make_unique<T>(deserialize_row(buf.data(), buf.size()));
      auto* raw = ptr.get();
      _items[pk] = std::move(ptr);
      return raw;
   }

   // Encode [scope:8B BE][secondary_value] using the correct sort-preserving
   // encoder for each type.  The chain's kv_idx_* intrinsics have no scope
   // parameter; encoding scope into sec_key provides equivalent isolation to
   // the legacy db_idx*_find_secondary(code, scope, ...) API.
   //
   // Generic version: delegates to _kv_multi_index_detail::encode_secondary()
   // which has specializations for uint64_t, uint128_t, double, and long double.
   template<typename SecVal>
   std::vector<char> encode_scoped_secondary(const SecVal& val) const {
      auto raw = _kv_multi_index_detail::encode_secondary(val);
      std::vector<char> buf(8 + raw.size());
      _kv_multi_index_detail::encode_be64(buf.data(), _scope);
      memcpy(buf.data() + 8, raw.data(), raw.size());
      return buf;
   }

   // Single-allocation fast path for uint64_t.
   std::vector<char> encode_scoped_secondary(const uint64_t& val) const {
      std::vector<char> buf(16);
      _kv_multi_index_detail::encode_be64(buf.data(), _scope);
      _kv_multi_index_detail::encode_be64(buf.data() + 8, val);
      return buf;
   }

   // Single-allocation fast path for uint128_t.
   std::vector<char> encode_scoped_secondary(const uint128_t& val) const {
      std::vector<char> buf(24);
      _kv_multi_index_detail::encode_be64(buf.data(), _scope);
      _kv_multi_index_detail::encode_be64(buf.data() + 8,  static_cast<uint64_t>(val >> 64));
      _kv_multi_index_detail::encode_be64(buf.data() + 16, static_cast<uint64_t>(val));
      return buf;
   }

   // Single-allocation fast path for double (sort-preserving transform + scope).
   std::vector<char> encode_scoped_secondary(const double& val) const {
      uint64_t bits;
      memcpy(&bits, &val, 8);
      if (bits & (uint64_t(1) << 63))
         bits = ~bits;
      else
         bits ^= (uint64_t(1) << 63);
      std::vector<char> buf(16);
      _kv_multi_index_detail::encode_be64(buf.data(), _scope);
      _kv_multi_index_detail::encode_be64(buf.data() + 8, bits);
      return buf;
   }

   // --- Secondary index helpers ---
   template<size_t N, typename Index, typename... Rest>
   struct secondary_ops {
      static void store_all(uint64_t payer, const kv_multi_index& idx, const T& obj) {
         using extractor_t = typename Index::secondary_extractor_type;
         extractor_t ext;
         auto sec_key = idx.encode_scoped_secondary(ext(obj));
         auto pri_key = idx.pk_to_bytes(obj.primary_key());
         ::kv_idx_store(payer, static_cast<uint64_t>(TableName), N,
                        pri_key.data, 8,
                        sec_key.data(), sec_key.size());
         if constexpr (sizeof...(Rest) > 0) {
            secondary_ops<N+1, Rest...>::store_all(payer, idx, obj);
         }
      }

      static void remove_all(const kv_multi_index& idx, const T& obj) {
         using extractor_t = typename Index::secondary_extractor_type;
         extractor_t ext;
         auto sec_key = idx.encode_scoped_secondary(ext(obj));
         auto pri_key = idx.pk_to_bytes(obj.primary_key());
         ::kv_idx_remove(static_cast<uint64_t>(TableName), N,
                         pri_key.data, 8,
                         sec_key.data(), sec_key.size());
         if constexpr (sizeof...(Rest) > 0) {
            secondary_ops<N+1, Rest...>::remove_all(idx, obj);
         }
      }

      static void update_all(uint64_t payer, const kv_multi_index& idx, const T& old_obj, const T& new_obj) {
         using extractor_t = typename Index::secondary_extractor_type;
         extractor_t ext;
         auto old_sec = idx.encode_scoped_secondary(ext(old_obj));
         auto new_sec = idx.encode_scoped_secondary(ext(new_obj));
         auto pri_key = idx.pk_to_bytes(old_obj.primary_key());
         if (old_sec != new_sec) {
            ::kv_idx_update(payer, static_cast<uint64_t>(TableName), N,
                            pri_key.data, 8,
                            old_sec.data(), old_sec.size(),
                            new_sec.data(), new_sec.size());
         }
         if constexpr (sizeof...(Rest) > 0) {
            secondary_ops<N+1, Rest...>::update_all(payer, idx, old_obj, new_obj);
         }
      }
   };

   // Base case — no indices
   struct no_secondary_ops {
      static void store_all(uint64_t, const kv_multi_index&, const T&) {}
      static void remove_all(const kv_multi_index&, const T&) {}
      static void update_all(uint64_t, const kv_multi_index&, const T&, const T&) {}
   };

   template<typename... Is>
   struct sec_ops_selector { using type = secondary_ops<0, Is...>; };
   template<>
   struct sec_ops_selector<> { using type = no_secondary_ops; };
   using sec_ops = typename sec_ops_selector<Indices...>::type;

   void store_secondaries(uint64_t payer, const T& obj) const {
      sec_ops::store_all(payer, *this, obj);
   }

   void remove_secondaries(const T& obj) const {
      sec_ops::remove_all(*this, obj);
   }

   void update_secondaries(uint64_t payer, const T& old_obj, const T& new_obj) const {
      sec_ops::update_all(payer, *this, old_obj, new_obj);
   }

public:
   kv_multi_index(name code, uint64_t scope) : _code(code), _scope(scope) {}

   name get_code() const { return _code; }
   uint64_t get_scope() const { return _scope; }

   // --- const_iterator ---
   struct const_iterator {
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = const T;
      using difference_type = std::ptrdiff_t;
      using pointer = const T*;
      using reference = const T&;

      const_iterator() : _idx(nullptr), _handle(-1), _valid(false) {}

      const T& operator*() const {
         check(_valid && _obj, "dereferencing invalid iterator");
         return *_obj;
      }

      const T* operator->() const {
         check(_valid && _obj, "dereferencing invalid iterator");
         return _obj;
      }

      const_iterator& operator++() {
         check(_idx, "incrementing invalid iterator");
         check(_valid, "cannot increment end iterator");
         if (!_valid) return *this;
         int32_t status = ::kv_it_next(_handle);
         if (status != 0) {
            _valid = false;
            _obj = nullptr;
         } else {
            load_current();
         }
         return *this;
      }

      // Post-increment/decrement deleted: copy requires kv_it_create + kv_it_lower_bound
      // (O(log n) per call). Use ++it / --it instead.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      const_iterator& operator--() {
         check(_idx, "decrementing invalid iterator");
         if (_handle == -1) {
            // End iterator: create a real iterator and seek to last element
            auto prefix = _idx->make_prefix();
            _handle = ::kv_it_create(kv_format_standard, _idx->_code.value, prefix.data, 16);
            // Seek past the end of this table's prefix range by using a max key
            char max_key[24];
            memcpy(max_key, prefix.data, 16);
            memset(max_key + 16, 0xFF, 8);
            ::kv_it_lower_bound(_handle, max_key, 24);
            // Now prev to get the last element
            int32_t status = ::kv_it_prev(_handle);
            if (status == 0) {
               _valid = true;
               load_current();
            } else {
               _valid = false;
               _obj = nullptr;
            }
         } else {
            int32_t status = ::kv_it_prev(_handle);
            if (status == 0) {
               _valid = true;
               load_current();
            } else {
               check(false, "cannot decrement iterator at beginning of table");
            }
         }
         return *this;
      }

      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         if (!a._valid && !b._valid) return true; // both end
         if (!a._valid || !b._valid) return false;
         return a._obj && b._obj && a._obj->primary_key() == b._obj->primary_key();
      }

      friend bool operator!=(const const_iterator& a, const const_iterator& b) {
         return !(a == b);
      }

      ~const_iterator() {
         if (_handle != -1) ::kv_it_destroy(_handle);
      }

      const_iterator(const_iterator&& o) noexcept
         : _idx(o._idx), _handle(o._handle), _valid(o._valid), _obj(o._obj) {
         o._handle = -1;
         o._valid = false;
         o._obj = nullptr;
      }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            if (_handle != -1) ::kv_it_destroy(_handle);
            _idx = o._idx;
            _handle = o._handle;
            _valid = o._valid;
            _obj = o._obj;
            o._handle = -1;
            o._valid = false;
            o._obj = nullptr;
         }
         return *this;
      }

      // Copy (creates new iterator handle)
      const_iterator(const const_iterator& o) : _idx(o._idx), _valid(o._valid), _obj(o._obj) {
         if (o._handle != -1 && _idx) {
            auto prefix = _idx->make_prefix();
            _handle = ::kv_it_create(kv_format_standard, _idx->_code.value, prefix.data, 16);
            if (_valid && _obj) {
               auto key = _idx->make_pk(to_pk_uint64(_obj->primary_key()));
               ::kv_it_lower_bound(_handle, key.data, 24);
            }
         } else {
            _handle = -1;
         }
      }

      const_iterator& operator=(const const_iterator& o) {
         if (this != &o) {
            if (_handle != -1) ::kv_it_destroy(_handle);
            _idx = o._idx;
            _valid = o._valid;
            _obj = o._obj;
            if (o._handle != -1 && _idx) {
               auto prefix = _idx->make_prefix();
               _handle = ::kv_it_create(kv_format_standard, _idx->_code.value, prefix.data, 16);
               if (_valid && _obj) {
                  auto key = _idx->make_pk(to_pk_uint64(_obj->primary_key()));
                  ::kv_it_lower_bound(_handle, key.data, 24);
               }
            } else {
               _handle = -1;
            }
         }
         return *this;
      }

   private:
      friend class kv_multi_index;
      const kv_multi_index* _idx;
      int32_t _handle;
      bool _valid;
      const T* _obj;

      const_iterator(const kv_multi_index* idx, int32_t handle, bool valid)
         : _idx(idx), _handle(handle), _valid(valid), _obj(nullptr) {
         if (_valid) load_current();
      }

      void load_current() {
         if (!_valid || _handle < 0) { _obj = nullptr; return; }
         // Read the primary key from the KV key (last 8 bytes of 24-byte key)
         char key_buf[24];
         uint32_t actual_size = 0;
         int32_t status = ::kv_it_key(_handle, 0, key_buf, 24, &actual_size);
         if (status != 0 || actual_size != 24) {
            _valid = false;
            _obj = nullptr;
            return;
         }
         uint64_t pk = _kv_multi_index_detail::decode_be64(key_buf + 16);
         _obj = _idx->load_object(pk);
         if (!_obj) {
            _valid = false;
         }
      }
   };

   // --- Primary key operations ---

   const_iterator begin() const {
      auto prefix = make_prefix();
      uint32_t handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      int32_t status = ::kv_it_status(handle);
      return const_iterator(this, handle, status == 0);
   }

   const_iterator end() const {
      return const_iterator(this, -1, false);
   }

   const_iterator cbegin() const { return begin(); }
   const_iterator cend() const { return end(); }

   using const_reverse_iterator = std::reverse_iterator<const_iterator>;
   const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
   const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
   const_reverse_iterator crbegin() const { return rbegin(); }
   const_reverse_iterator crend() const { return rend(); }

   const_iterator find(name primary) const { return find(primary.value); }
   const_iterator find(uint64_t primary) const {
      auto key = make_pk(primary);
      int32_t sz = ::kv_get(kv_format_standard, _code.value, key.data, 24, nullptr, 0);
      if (sz < 0) return end();

      // Create iterator positioned at this key
      auto prefix = make_prefix();
      uint32_t handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      ::kv_it_lower_bound(handle, key.data, 24);
      return const_iterator(this, handle, true);
   }

   const_iterator require_find(name primary, const char* error_msg = "unable to find key") const { return require_find(primary.value, error_msg); }
   const_iterator require_find(uint64_t primary, const char* error_msg = "unable to find key") const {
      auto itr = find(primary);
      check(itr != end(), error_msg);
      return itr;
   }

   const T& get(name primary, const char* error_msg = "unable to find key") const { return get(primary.value, error_msg); }
   const T& get(uint64_t primary, const char* error_msg = "unable to find key") const {
      auto* obj = load_object(primary);
      check(obj != nullptr, error_msg);
      return *obj;
   }

   const_iterator lower_bound(uint64_t primary) const {
      auto key = make_pk(primary);
      auto prefix = make_prefix();
      uint32_t handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      int32_t status = ::kv_it_lower_bound(handle, key.data, 24);
      return const_iterator(this, handle, status == 0);
   }

   const_iterator upper_bound(uint64_t primary) const {
      if (primary == std::numeric_limits<uint64_t>::max()) return end();
      return lower_bound(primary + 1);
   }

   const_iterator iterator_to(const T& obj) const {
      uint64_t pk = to_pk_uint64(obj.primary_key());
      check(_items.find(pk) != _items.end(),
            "object passed to iterator_to is not in multi_index");
      auto key = make_pk(pk);
      auto prefix = make_prefix();
      uint32_t handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      ::kv_it_lower_bound(handle, key.data, 24);
      return const_iterator(this, handle, true);
   }

   // --- Mutation ---

   template<typename Lambda>
   const_iterator emplace(name payer, Lambda&& constructor) {
      T obj;
      constructor(obj);

      uint64_t pk = to_pk_uint64(obj.primary_key());
      auto key = make_pk(pk);
      auto value = serialize_row(obj);

      ::kv_set(1, payer.value, key.data, 24, value.data(), value.size());
      store_secondaries(payer.value, obj);

      // Cache the object
      auto ptr = std::make_unique<T>(std::move(obj));
      _items[pk] = std::move(ptr);

      // Update auto-increment
      if (_next_primary_key_cached && pk >= _next_primary_key) {
         check(pk < std::numeric_limits<uint64_t>::max(),
               "next primary key in table is at autoincrement limit");
         _next_primary_key = pk + 1;
      }

      return find(pk);
   }

   template<typename Lambda>
   void modify(const const_iterator& itr, name payer, Lambda&& updater) {
      check(itr != end(), "cannot pass end iterator to modify");
      modify(*itr, payer, std::forward<Lambda>(updater));
   }

   template<typename Lambda>
   void modify(const T& obj, name payer, Lambda&& updater) {
      T old_obj = obj;
      // Cast away const for modification (same pattern as legacy multi_index)
      auto& mutable_obj = const_cast<T&>(obj);
      updater(mutable_obj);
      check(mutable_obj.primary_key() == old_obj.primary_key(), "updater cannot change primary key when modifying an object");

      uint64_t pk = to_pk_uint64(mutable_obj.primary_key());
      auto key = make_pk(pk);
      auto value = serialize_row(mutable_obj);
      ::kv_set(1, payer.value, key.data, 24, value.data(), value.size());

      update_secondaries(payer.value, old_obj, mutable_obj);

      // Update cache
      _items[pk] = std::make_unique<T>(mutable_obj);
   }

   const_iterator erase(const_iterator itr) {
      check(itr != end(), "cannot pass end iterator to erase");
      auto next = itr;
      ++next;

      erase(*itr);
      return next;
   }

   void erase(const T& obj) {
      uint64_t pk = to_pk_uint64(obj.primary_key());
      auto key = make_pk(pk);

      remove_secondaries(obj);
      ::kv_erase(kv_format_standard, key.data, 24);
      _items.erase(pk);
   }

   uint64_t available_primary_key() const {
      if (_next_primary_key_cached) return _next_primary_key;

      _next_primary_key_cached = true;

      // Find the last key in the table
      auto prefix = make_prefix();
      uint32_t handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      int32_t status = ::kv_it_status(handle);

      if (status != 0) {
         // Empty table
         ::kv_it_destroy(handle);
         _next_primary_key = 0;
         return 0;
      }

      ::kv_it_destroy(handle);

      // Create iterator and seek past all possible primary keys
      handle = ::kv_it_create(kv_format_standard, _code.value, prefix.data, 16);
      char max_key[24];
      _kv_multi_index_detail::encode_be64(max_key,      static_cast<uint64_t>(TableName));
      _kv_multi_index_detail::encode_be64(max_key + 8,  _scope);
      _kv_multi_index_detail::encode_be64(max_key + 16, std::numeric_limits<uint64_t>::max());
      ::kv_it_lower_bound(handle, max_key, 24);

      // Check if we found max key exactly
      status = ::kv_it_status(handle);
      if (status == 0) {
         // The max key exists — can't auto-increment past max
         ::kv_it_destroy(handle);
         _next_primary_key = 0;
         _next_primary_key_cached = false;
         check(false, "next primary key in table is at autoincrement limit");
         return 0;
      }

      // Go back to find the actual last key
      status = ::kv_it_prev(handle);
      if (status != 0) {
         // Empty table
         ::kv_it_destroy(handle);
         _next_primary_key = 0;
         return 0;
      }

      char key_buf[24];
      uint32_t actual_size = 0;
      ::kv_it_key(handle, 0, key_buf, 24, &actual_size);
      ::kv_it_destroy(handle);

      if (actual_size == 24) {
         uint64_t last_pk = _kv_multi_index_detail::decode_be64(key_buf + 16);
         check(last_pk < std::numeric_limits<uint64_t>::max(),
               "next primary key in table is at autoincrement limit");
         _next_primary_key = last_pk + 1;
      } else {
         _next_primary_key = 0;
      }

      return _next_primary_key;
   }

   // --- Secondary index access ---

   template<name::raw IndexName>
   struct secondary_index_view {
      template<size_t N, typename First, typename... Rest>
      struct find_index_number {
         static constexpr size_t value =
            (static_cast<uint64_t>(First::index_name) == static_cast<uint64_t>(IndexName))
            ? N : find_index_number<N+1, Rest...>::value;
      };
      template<size_t N, typename Last>
      struct find_index_number<N, Last> {
         static constexpr size_t value =
            (static_cast<uint64_t>(Last::index_name) == static_cast<uint64_t>(IndexName))
            ? N : N + 1;
      };

      static constexpr size_t index_number = find_index_number<0, Indices...>::value;
      static_assert(index_number < sizeof...(Indices), "invalid secondary index name");

      using index_type = typename std::tuple_element<index_number, std::tuple<Indices...>>::type;
      using secondary_extractor_type = typename index_type::secondary_extractor_type;
      using secondary_key_type = std::decay_t<typename secondary_extractor_type::result_type>;
      static_assert(sizeof(secondary_key_type) == pack_size(secondary_key_type{}),
                    "encoded secondary size must equal sizeof for max_sec buffer sizing");

      const kv_multi_index* _mi;
      secondary_index_view(const kv_multi_index& mi) : _mi(&mi) {}

      // Helper: read primary key from secondary iterator handle.
      // The stored pri_key is [pk:8B].
      static bool read_primary_key(uint32_t handle, uint64_t& pk) {
         char pri_buf[8];
         uint32_t actual = 0;
         int32_t status = ::kv_idx_primary_key(handle, 0, pri_buf, 8, &actual);
         if (status != 0 || actual != 8) return false;
         pk = _kv_multi_index_detail::decode_be64(pri_buf);
         return true;
      }

      // Secondary const_iterator
      struct const_iterator {
         using iterator_category = std::bidirectional_iterator_tag;
         using value_type = const T;
         using difference_type = std::ptrdiff_t;
         using pointer = const T*;
         using reference = const T&;

         const_iterator() = default;

         const T& operator*() const { check(_has_obj, "deref invalid sec iter"); return _obj; }
         const T* operator->() const { check(_has_obj, "deref invalid sec iter"); return &_obj; }

         const_iterator& operator++() {
            check(_has_obj, "cannot increment end iterator");
            if (!_has_obj || _handle < 0) return *this;
            if (::kv_idx_next(_handle) == 0 && check_scope()) { load_current(); }
            else { _has_obj = false; }
            return *this;
         }
         // Post-increment/decrement deleted: copy requires host function calls. Use ++it / --it.
         const_iterator operator++(int) = delete;
         const_iterator operator--(int) = delete;

         const_iterator& operator--() {
            if (_handle < 0) {
               // End sentinel: create real iterator at last entry in THIS scope.
               // Build a maximal sec_key for this scope: [scope:8B][0xFF...]
               // so lower_bound positions past this scope's entries, then prev
               // lands on the last entry in the scope.
               constexpr size_t sec_val_size = sizeof(secondary_key_type);
               char max_sec[8 + sec_val_size];
               _kv_multi_index_detail::encode_be64(max_sec, _mi->_scope);
               memset(max_sec + 8, 0xFF, sec_val_size);
               _handle = ::kv_idx_lower_bound(
                  _mi->_code.value, static_cast<uint64_t>(TableName), index_number,
                  max_sec, sizeof(max_sec));
               if (_handle < 0) { _has_obj = false; }
               else if (::kv_idx_prev(_handle) == 0 && check_scope()) { _has_obj = true; load_current(); }
               else { _has_obj = false; }
            } else {
               if (::kv_idx_prev(_handle) == 0 && check_scope()) { _has_obj = true; load_current(); }
               else { check(false, "cannot decrement iterator at beginning of index"); }
            }
            return *this;
         }
         friend bool operator==(const const_iterator& a, const const_iterator& b) {
            if (!a._has_obj && !b._has_obj) return true;
            if (!a._has_obj || !b._has_obj) return false;
            return a._pk == b._pk;
         }
         friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); }

         ~const_iterator() { if (_handle >= 0) ::kv_idx_destroy(_handle); }
         const_iterator(const_iterator&& o) noexcept
            : _mi(o._mi), _handle(o._handle), _has_obj(o._has_obj), _pk(o._pk), _obj(std::move(o._obj))
            { o._handle = -1; o._has_obj = false; }
         const_iterator& operator=(const_iterator&& o) noexcept {
            if (this != &o) {
               if (_handle >= 0) ::kv_idx_destroy(_handle);
               _mi = o._mi; _handle = o._handle; _has_obj = o._has_obj; _pk = o._pk; _obj = std::move(o._obj);
               o._handle = -1; o._has_obj = false;
            }
            return *this;
         }
         const_iterator(const const_iterator& o) : _mi(o._mi), _has_obj(o._has_obj), _pk(o._pk), _obj(o._obj) {
            _clone_handle(o);
         }
         const_iterator& operator=(const const_iterator& o) {
            if (this != &o) {
               if (_handle >= 0) ::kv_idx_destroy(_handle);
               _mi = o._mi; _has_obj = o._has_obj; _pk = o._pk; _obj = o._obj;
               _clone_handle(o);
            }
            return *this;
         }

      private:
         void _clone_handle(const const_iterator& o) {
            if (o._handle >= 0 && _mi && _has_obj) {
               using extractor_t = typename std::tuple_element<index_number, std::tuple<typename Indices::secondary_extractor_type...>>::type;
               extractor_t ext;
               auto sec_bytes = _mi->encode_scoped_secondary(ext(_obj));
               _handle = ::kv_idx_find_secondary(
                  _mi->_code.value, static_cast<uint64_t>(TableName), index_number,
                  sec_bytes.data(), sec_bytes.size());
               if (_handle < 0) return;
               // kv_idx_find_secondary lands on the first row with this
               // secondary key, but the source iterator may point to a later
               // duplicate. Advance until we find the matching primary key.
               uint64_t found_pk = 0;
               if (read_primary_key(_handle, found_pk) && found_pk == _pk)
                  return;
               while (::kv_idx_next(_handle) == 0) {
                  if (read_primary_key(_handle, found_pk) && found_pk == _pk)
                     return;
               }
            } else {
               _handle = -1;
            }
         }

         friend struct secondary_index_view;
         const kv_multi_index* _mi = nullptr;
         int32_t _handle = -1;
         bool _has_obj = false;
         uint64_t _pk = 0;
         T _obj;

         const_iterator(const kv_multi_index* mi, int32_t handle, bool valid)
            : _mi(mi), _handle(handle), _has_obj(valid) {
            if (_has_obj) load_current();
         }
         static const_iterator make_end(const kv_multi_index* mi) {
            const_iterator it; it._mi = mi; return it;
         }

         void load_current() {
            if (_handle < 0) { _has_obj = false; return; }
            if (!read_primary_key(_handle, _pk)) { _has_obj = false; return; }
            auto* ptr = _mi->load_object(_pk);
            if (ptr) { _obj = *ptr; _has_obj = true; }
            else { _has_obj = false; }
         }

         // Verify the current sec_key still starts with [scope:8B].
         // After kv_idx_next/prev, the iterator may have crossed into
         // another scope — treat that as end-of-range.
         bool check_scope() const {
            if (_handle < 0 || !_mi) return false;
            char scope_buf[8];
            uint32_t actual = 0;
            int32_t status = ::kv_idx_key(_handle, 0, scope_buf, 8, &actual);
            if (status != 0 || actual < 8) return false;
            char expected[8];
            _kv_multi_index_detail::encode_be64(expected, _mi->_scope);
            return memcmp(scope_buf, expected, 8) == 0;
         }
      };

      const_iterator end() const { return const_iterator::make_end(_mi); }
      const_iterator cend() const { return end(); }
      const_iterator cbegin() const { return begin(); }

      using const_reverse_iterator = std::reverse_iterator<const_iterator>;
      const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
      const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
      const_reverse_iterator crbegin() const { return rbegin(); }
      const_reverse_iterator crend() const { return rend(); }

      const_iterator begin() const {
         // Lower bound with scope prefix = first entry in this scope
         char scope_prefix[8];
         _kv_multi_index_detail::encode_be64(scope_prefix, _mi->_scope);
         int32_t handle = ::kv_idx_lower_bound(
            _mi->_code.value, static_cast<uint64_t>(TableName), index_number,
            scope_prefix, 8);
         if (handle < 0) return end();
         // Verify we landed in the right scope (may be past it if scope is empty)
         const_iterator it(_mi, handle, true);
         if (it._has_obj && !it.check_scope()) return end();
         return it;
      }

      template<typename SecKey>
      const_iterator find(const SecKey& sec_key) const {
         auto sec_bytes = _mi->encode_scoped_secondary(secondary_key_type(sec_key));
         int32_t handle = ::kv_idx_find_secondary(
            _mi->_code.value, static_cast<uint64_t>(TableName), index_number,
            sec_bytes.data(), sec_bytes.size());
         if (handle < 0) return end();
         return const_iterator(_mi, handle, true);
      }

      template<typename SecKey>
      const_iterator lower_bound(const SecKey& sec_key) const {
         auto sec_bytes = _mi->encode_scoped_secondary(secondary_key_type(sec_key));
         int32_t handle = ::kv_idx_lower_bound(
            _mi->_code.value, static_cast<uint64_t>(TableName), index_number,
            sec_bytes.data(), sec_bytes.size());
         if (handle < 0) return end();
         const_iterator it(_mi, handle, true);
         if (it._has_obj && !it.check_scope()) return end();
         return it;
      }

      template<typename SecKey>
      const_iterator require_find(const SecKey& sec_key, const char* msg = "unable to find secondary key") const {
         auto itr = find(sec_key);
         check(itr != end(), msg);
         return itr;
      }

      template<typename SecKey>
      const_iterator upper_bound(const SecKey& sec_key) const {
         // upper_bound = lower_bound then skip entries matching the key
         using extractor_t = typename std::tuple_element<index_number, std::tuple<typename Indices::secondary_extractor_type...>>::type;
         extractor_t ext;
         auto target_sec = _kv_multi_index_detail::encode_secondary(secondary_key_type(sec_key));
         auto itr = lower_bound(sec_key);
         while (itr != end()) {
            auto itr_sec = _kv_multi_index_detail::encode_secondary(ext(*itr));
            if (itr_sec != target_sec) break;
            ++itr;
         }
         return itr;
      }

      template<typename SecKey>
      const T& get(const SecKey& sec_key, const char* msg = "unable to find secondary key") const {
         auto itr = find(sec_key);
         check(itr != end(), msg);
         return *itr;
      }

      // Modify the primary row referenced by a secondary iterator
      template<typename Lambda>
      void modify(const const_iterator& itr, name payer, Lambda&& updater) {
         check(itr._has_obj, "cannot pass end iterator to modify");
         const_cast<kv_multi_index*>(_mi)->modify(itr._obj, payer, std::forward<Lambda>(updater));
      }

      // Find the secondary iterator for a given row object.
      // When multiple rows share the same secondary key, advance
      // to the entry matching this object's primary key.
      const_iterator iterator_to(const T& obj) {
         using extractor_t = typename std::tuple_element<index_number, std::tuple<typename Indices::secondary_extractor_type...>>::type;
         extractor_t ext;
         auto target_pk = to_pk_uint64(obj.primary_key());
         auto it = find(ext(obj));
         while (it != end() && it._pk != target_pk) {
            ++it;
         }
         return it;
      }

      // Erase the primary row referenced by a secondary iterator, returns next iterator
      const_iterator erase(const const_iterator& itr) {
         check(itr._has_obj, "cannot pass end iterator to erase");
         auto next = itr;
         ++next;
         const_cast<kv_multi_index*>(_mi)->erase(itr._obj);
         return next;
      }

      name get_code() const { return name(_mi->_code.value); }
      uint64_t get_scope() const { return _mi->_scope; }
   };

   template<name::raw IndexName>
   secondary_index_view<IndexName> get_index() const {
      return secondary_index_view<IndexName>(*this);
   }
};

} // namespace sysio
