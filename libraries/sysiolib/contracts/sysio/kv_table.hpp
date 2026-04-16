#pragma once
/**
 * sysio::kv::table — High-performance KV-backed table with multi_index-like API.
 *
 * Automatically selects zero-copy storage for trivially_copyable structs (memcpy,
 * no pack/unpack) or falls back to datastream serialization for complex types.
 *
 * Key encoding: [table:8B BE][scope:8B BE][pk:8B BE] = 24 bytes.
 * SHiP compatible: keys can be reverse-mapped to legacy contract_row format.
 */

#include <cstdint>

// KV intrinsic declarations (primary only — kv::table does not use secondary indices)
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
}

#include <sysio/kv_constants.hpp>
#include <sysio/kv_it_handle.hpp>
#include <sysio/kv_raw_table.hpp>  // for is_fixed_serializable_v, ser_buf

#include <sysio/name.hpp>
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/check.hpp>
#include <sysio/action.hpp>

#include <cstring>
#include <limits>
#include <type_traits>

#ifndef SYSIO_SAME_PAYER_DEFINED
#define SYSIO_SAME_PAYER_DEFINED
namespace sysio {
   inline constexpr name same_payer{};
}
#endif

namespace sysio { namespace kv {

namespace detail {
   inline void encode_be64(char* buf, uint64_t v) {
      for (int i = 7; i >= 0; --i) { buf[i] = static_cast<char>(v & 0xFF); v >>= 8; }
   }
   inline uint64_t decode_be64(const char* buf) {
      uint64_t v = 0;
      for (int i = 0; i < 8; ++i) v = (v << 8) | static_cast<uint8_t>(buf[i]);
      return v;
   }

   // Zero-copy for trivially_copyable types WITHOUT struct padding.
   // Padding check: sizeof(T) == pack_size(T). If they differ, fall back to pack/unpack.
   template<typename T>
   inline uint32_t store_value(const T& obj, char* buf, uint32_t buf_size) {
      if constexpr (std::is_trivially_copyable<T>::value) {
         if (sizeof(T) == sysio::pack_size(obj)) {
            memcpy(buf, &obj, sizeof(T));
            return sizeof(T);
         }
      }
      sysio::datastream<char*> ds(buf, buf_size);
      ds << obj;
      return static_cast<uint32_t>(ds.tellp());
   }

   template<typename T>
   inline void load_value(T& obj, const char* data, uint32_t size) {
      if constexpr (std::is_trivially_copyable<T>::value) {
         if (size == sizeof(T)) {
            memcpy(&obj, data, sizeof(T));
            return;
         }
      }
      sysio::datastream<const char*> ds(data, size);
      ds >> obj;
   }

   template<typename T>
   inline uint32_t value_size(const T& obj) {
      if constexpr (std::is_trivially_copyable<T>::value) {
         if (sizeof(T) == sysio::pack_size(obj))
            return sizeof(T);
      }
      return sysio::pack_size(obj);
   }
}

template<sysio::name::raw TableName, typename T>
class table {
   static_assert(std::is_default_constructible<T>::value, "Row type must be default constructible");

   // Key layout: [table:8B BE][scope:8B BE][pk:8B BE]
   static constexpr uint32_t scope_offset    = 8;
   static constexpr uint32_t pk_offset       = 16;
   static constexpr uint32_t key_size        = 24;
   static constexpr uint32_t prefix_size     = 16;
   static constexpr uint32_t tbl_prefix_size = 8;

   uint64_t _code;
   uint64_t _scope;

   struct pk_buf { char data[key_size]; };
   struct prefix_buf { char data[prefix_size]; };
   struct table_prefix_buf { char data[tbl_prefix_size]; };

   pk_buf make_pk(uint64_t pk) const {
      pk_buf key;
      detail::encode_be64(key.data,                static_cast<uint64_t>(TableName));
      detail::encode_be64(key.data + scope_offset, _scope);
      detail::encode_be64(key.data + pk_offset,    pk);
      return key;
   }

   prefix_buf _prefix;
   prefix_buf make_prefix() const {
      prefix_buf p;
      detail::encode_be64(p.data,                static_cast<uint64_t>(TableName));
      detail::encode_be64(p.data + scope_offset, _scope);
      return p;
   }

   // Common kv_get + deserialize.
   bool get_value(const pk_buf& key, T& out) const {
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         int32_t sz = ::kv_get(kv_format_standard, _code, key.data, key_size, vbuf, sizeof(T));
         if (sz < 0) return false;
         std::memcpy(&out, vbuf, sizeof(T));
      } else {
         char buf[kv_value_stack_size];
         int32_t sz = ::kv_get(kv_format_standard, _code, key.data, key_size, buf, kv_value_stack_size);
         if (sz < 0) return false;
         if (static_cast<uint32_t>(sz) <= kv_value_stack_size) {
            detail::load_value(out, buf, sz);
         } else {
            char* heap = new char[sz];
            ::kv_get(kv_format_standard, _code, key.data, key_size, heap, sz);
            detail::load_value(out, heap, sz);
            delete[] heap;
         }
      }
      return true;
   }

   bool load_row(uint64_t pk, T& out) const {
      return get_value(make_pk(pk), out);
   }

   bool load_row_at(uint64_t scope, uint64_t pk, T& out) const {
      pk_buf key;
      detail::encode_be64(key.data,                static_cast<uint64_t>(TableName));
      detail::encode_be64(key.data + scope_offset, scope);
      detail::encode_be64(key.data + pk_offset,    pk);
      return get_value(key, out);
   }

   void store_row(uint64_t payer, uint64_t pk, const T& obj) const {
      auto key = make_pk(pk);
      if constexpr (is_fixed_serializable_v<T>) {
         char vbuf[sizeof(T)];
         std::memcpy(vbuf, &obj, sizeof(T));
         ::kv_set(kv_format_standard, payer, key.data, key_size, vbuf, sizeof(T));
      } else {
         uint32_t sz = detail::value_size(obj);
         if (sz <= kv_value_stack_size) {
            char buf[kv_value_stack_size];
            detail::store_value(obj, buf, kv_value_stack_size);
            ::kv_set(kv_format_standard, payer, key.data, key_size, buf, sz);
         } else {
            char* heap = new char[sz];
            detail::store_value(obj, heap, sz);
            ::kv_set(kv_format_standard, payer, key.data, key_size, heap, sz);
            delete[] heap;
         }
      }
   }

public:
   table(sysio::name code, uint64_t scope)
      : _code(code.value), _scope(scope), _prefix(make_prefix()) {}

   // --- const_iterator ---
   struct const_iterator {
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = const T;
      using difference_type = std::ptrdiff_t;
      using pointer = const T*;
      using reference = const T&;

      const_iterator() = default;

      const T& operator*() const { sysio::check(_has_obj, "deref invalid"); return _obj; }
      const T* operator->() const { sysio::check(_has_obj, "deref invalid"); return &_obj; }

      const_iterator& operator++() {
         if (!_has_obj) return *this;
         ensure_handle();
         if (::kv_it_next(_handle) != 0) { _has_obj = false; }
         else { load_from_handle(); }
         return *this;
      }
      // Post-increment/decrement deleted: copy requires host function calls. Use ++it / --it.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      const_iterator& operator--() {
         ensure_handle();
         if (::kv_it_prev(_handle) == 0) { _has_obj = true; load_from_handle(); }
         else { _has_obj = false; }
         return *this;
      }

      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         if (!a._has_obj && !b._has_obj) return true;
         if (!a._has_obj || !b._has_obj) return false;
         return a._obj.primary_key() == b._obj.primary_key();
      }
      friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); }

      const_iterator(const_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(std::move(o._handle)), _has_obj(o._has_obj), _obj(std::move(o._obj))
         { o._has_obj = false; }
      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            _tbl = o._tbl; _handle = std::move(o._handle); _has_obj = o._has_obj; _obj = std::move(o._obj);
            o._has_obj = false;
         }
         return *this;
      }
      const_iterator(const const_iterator&) = delete;
      const_iterator& operator=(const const_iterator&) = delete;

   private:
      friend class table;
      const table* _tbl = nullptr;
      detail::it_handle _handle;
      bool _has_obj = false;
      T _obj;

      const_iterator(const table* t, const T& obj) : _tbl(t), _has_obj(true), _obj(obj) {}
      const_iterator(const table* t, int32_t h, bool valid) : _tbl(t), _handle(h), _has_obj(valid) {
         if (_has_obj) load_from_handle();
      }
      static const_iterator make_end(const table* t) { const_iterator it; it._tbl = t; return it; }

      void load_from_handle() {
         char key_buf[key_size]; uint32_t actual = 0;
         if (::kv_it_key(_handle, 0, key_buf, key_size, &actual) != 0 || actual != key_size) { _has_obj = false; return; }
         uint64_t pk = detail::decode_be64(key_buf + pk_offset);
         _has_obj = _tbl->load_row(pk, _obj);
      }

      void ensure_handle() {
         if (_handle >= 0) return;
         if (!_tbl) return;
         _handle.reset(::kv_it_create(kv_format_standard, _tbl->_code, _tbl->_prefix.data, prefix_size));
         if (_has_obj) {
            auto key = _tbl->make_pk(_obj.primary_key());
            ::kv_it_lower_bound(_handle, key.data, key_size);
         }
      }
   };

   const_iterator end() const { return const_iterator::make_end(this); }
   const_iterator cend() const { return end(); }
   const_iterator begin() const {
      uint32_t h = ::kv_it_create(kv_format_standard, _code, _prefix.data, prefix_size);
      return const_iterator(this, h, ::kv_it_status(h) == 0);
   }
   const_iterator cbegin() const { return begin(); }

   // --- Cross-scope iteration (NEW: not possible with legacy multi_index) ---
   // Uses 8-byte prefix (table only) to iterate ALL scopes for this table.

   struct scoped_row {
      uint64_t scope;
      uint64_t primary_key;
      T        obj;
   };

   struct scoped_iterator {
      using iterator_category = std::forward_iterator_tag;
      using value_type = const scoped_row;
      using difference_type = std::ptrdiff_t;
      using pointer = const scoped_row*;
      using reference = const scoped_row&;

      scoped_iterator() = default;

      const scoped_row& operator*() const { sysio::check(_has_obj, "deref invalid"); return _row; }
      const scoped_row* operator->() const { sysio::check(_has_obj, "deref invalid"); return &_row; }

      scoped_iterator& operator++() {
         if (!_has_obj || _handle < 0) return *this;
         if (::kv_it_next(_handle) != 0) { _has_obj = false; }
         else { load_current(); }
         return *this;
      }
      // Post-increment deleted: copy requires host function calls. Use ++it.
      scoped_iterator operator++(int) = delete;

      friend bool operator==(const scoped_iterator& a, const scoped_iterator& b) {
         if (!a._has_obj && !b._has_obj) return true;
         if (!a._has_obj || !b._has_obj) return false;
         return a._row.scope == b._row.scope && a._row.primary_key == b._row.primary_key;
      }
      friend bool operator!=(const scoped_iterator& a, const scoped_iterator& b) { return !(a == b); }

      scoped_iterator(scoped_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(std::move(o._handle)), _has_obj(o._has_obj), _row(std::move(o._row))
         { o._has_obj = false; }
      scoped_iterator& operator=(scoped_iterator&& o) noexcept {
         if (this != &o) {
            _tbl = o._tbl; _handle = std::move(o._handle); _has_obj = o._has_obj; _row = std::move(o._row);
            o._has_obj = false;
         }
         return *this;
      }
      scoped_iterator(const scoped_iterator&) = delete;
      scoped_iterator& operator=(const scoped_iterator&) = delete;

   private:
      friend class table;
      const table* _tbl = nullptr;
      detail::it_handle _handle;
      bool _has_obj = false;
      scoped_row _row;

      scoped_iterator(const table* t, int32_t h, bool valid) : _tbl(t), _handle(h), _has_obj(valid) {
         if (_has_obj) load_current();
      }
      static scoped_iterator make_end(const table* t) { scoped_iterator it; it._tbl = t; return it; }

      void load_current() {
         char key_buf[key_size]; uint32_t actual = 0;
         if (::kv_it_key(_handle, 0, key_buf, key_size, &actual) != 0 || actual != key_size) { _has_obj = false; return; }
         _row.scope = detail::decode_be64(key_buf + scope_offset);
         _row.primary_key = detail::decode_be64(key_buf + pk_offset);
         _has_obj = _tbl->load_row_at(_row.scope, _row.primary_key, _row.obj);
      }
   };

   /// Iterate ALL rows across ALL scopes for this table.
   /// Each entry provides scope, primary_key, and the deserialized row.
   scoped_iterator begin_all_scopes() const {
      table_prefix_buf tp;
      detail::encode_be64(tp.data, static_cast<uint64_t>(TableName));
      uint32_t h = ::kv_it_create(kv_format_standard, _code, tp.data, tbl_prefix_size);
      return scoped_iterator(this, h, ::kv_it_status(h) == 0);
   }

   scoped_iterator end_all_scopes() const { return scoped_iterator::make_end(this); }

   // Point lookup: 1 kv_get call, zero-copy for trivial types
   const_iterator find(uint64_t pk) const {
      T obj;
      if (load_row(pk, obj)) return const_iterator(this, obj);
      return end();
   }

   const_iterator require_find(uint64_t pk, const char* msg = "unable to find key") const {
      auto itr = find(pk); sysio::check(itr != end(), msg); return itr;
   }

   T get(uint64_t pk, const char* msg = "unable to find key") const {
      T obj;
      sysio::check(load_row(pk, obj), msg);
      return obj;
   }

   bool contains(uint64_t pk) const {
      auto key = make_pk(pk);
      return ::kv_contains(kv_format_standard, _code, key.data, key_size) != 0;
   }

   const_iterator lower_bound(uint64_t pk) const {
      auto key = make_pk(pk);
      uint32_t h = ::kv_it_create(kv_format_standard, _code, _prefix.data, prefix_size);
      return const_iterator(this, h, ::kv_it_lower_bound(h, key.data, key_size) == 0);
   }

   const_iterator upper_bound(uint64_t pk) const {
      if (pk == std::numeric_limits<uint64_t>::max()) return end();
      return lower_bound(pk + 1);
   }

   // --- Mutation ---

   template<typename Lambda>
   const_iterator emplace(sysio::name payer, Lambda&& constructor) {
      T obj;
      constructor(obj);
      store_row(payer.value, obj.primary_key(), obj);
      return const_iterator(this, obj);
   }

   // Set directly by primary key (convenience for raw structs)
   void set(uint64_t pk, const T& obj) {
      store_row(0, pk, obj);
   }

   template<typename Lambda>
   void modify(const const_iterator& itr, sysio::name payer, Lambda&& updater) {
      sysio::check(itr._has_obj, "cannot modify end iterator");
      T& mobj = const_cast<T&>(itr._obj);
      auto old_pk = mobj.primary_key();
      updater(mobj);
      sysio::check(mobj.primary_key() == old_pk, "cannot modify primary key");
      store_row(payer.value, old_pk, mobj);
   }

   template<typename Lambda>
   void modify(const T& obj, sysio::name payer, Lambda&& updater) {
      T mobj = obj;
      auto old_pk = mobj.primary_key();
      updater(mobj);
      sysio::check(mobj.primary_key() == old_pk, "cannot modify primary key");
      store_row(payer.value, old_pk, mobj);
   }

   void erase(const T& obj) {
      erase(obj.primary_key());
   }

   void erase(const const_iterator& itr) {
      sysio::check(itr._has_obj, "cannot erase end iterator");
      erase(itr._obj.primary_key());
   }

   void erase(uint64_t pk) {
      auto key = make_pk(pk);
      ::kv_erase(kv_format_standard, key.data, key_size);
   }

   uint64_t available_primary_key() const {
      detail::it_handle h(::kv_it_create(kv_format_standard, _code, _prefix.data, prefix_size));
      if (::kv_it_status(h) != 0) return 0;
      char max_key[key_size];
      detail::encode_be64(max_key,                static_cast<uint64_t>(TableName));
      detail::encode_be64(max_key + scope_offset, _scope);
      detail::encode_be64(max_key + pk_offset,    std::numeric_limits<uint64_t>::max());
      ::kv_it_lower_bound(h, max_key, key_size);
      if (::kv_it_prev(h) != 0) return 0;
      char key_buf[key_size]; uint32_t actual = 0;
      ::kv_it_key(h, 0, key_buf, key_size, &actual);
      if (actual != key_size) return 0;
      uint64_t last_pk = detail::decode_be64(key_buf + pk_offset);
      sysio::check(last_pk < std::numeric_limits<uint64_t>::max(),
                   "next primary key in table is at autoincrement limit");
      return last_pk + 1;
   }
};

}} // namespace sysio::kv
