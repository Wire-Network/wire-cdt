#pragma once
/**
 * kv::table -- All-purpose KV table with user-defined BE keys and optional
 * secondary indexes.
 *
 * Each table gets a unique table_id (DJB2 hash of the template parameter),
 * providing automatic namespace isolation. No key collisions between tables.
 *
 * Usage:
 *   struct my_key {
 *      uint64_t id;
 *      SYSLIB_SERIALIZE(my_key, (id))
 *   };
 *   struct my_val {
 *      uint64_t balance;
 *      name     owner;
 *      uint64_t get_balance() const { return balance; }
 *      SYSLIB_SERIALIZE(my_val, (balance)(owner))
 *   };
 *
 *   using my_table = kv::table<"mytbl"_n, my_key, my_val,
 *      kv::index<"byowner"_n, kv::member_data<my_val, name, &my_val::owner>>,
 *      kv::index<"bybal"_n,   sysio::const_mem_fun<my_val, uint64_t, &my_val::get_balance>>
 *   >;
 *
 *   my_table tbl;
 *   tbl.emplace({1}, {1000, "alice"_n});
 *   auto idx = tbl.get_index<"byowner"_n>();
 *   auto it  = idx.find("alice"_n);
 */

#include <sysio/kv_utils.hpp>
#include <sysio/check.hpp>

#include <cstring>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

// Secondary index intrinsic declarations (not in kv_utils.hpp).
// Duplicate extern "C" declarations are harmless if kv_multi_index.hpp
// is also included.
extern "C" {
   __attribute__((sysio_wasm_import))
   void kv_idx_store(uint64_t payer, uint32_t table_id,
                     const void* pri_key, uint32_t pri_key_size,
                     const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_remove(uint32_t table_id,
                      const void* pri_key, uint32_t pri_key_size,
                      const void* sec_key, uint32_t sec_key_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_update(uint64_t payer, uint32_t table_id,
                      const void* pri_key, uint32_t pri_key_size,
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
   int32_t kv_idx_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   int32_t kv_idx_primary_key(uint32_t handle, uint32_t offset, void* dest, uint32_t dest_size, uint32_t* actual_size);
   __attribute__((sysio_wasm_import))
   void kv_idx_destroy(uint32_t handle);
}

namespace sysio { namespace kv {

// ---------------------------------------------------------------------------
// BE key decoder — reverse of be_key_stream.
// Works with SYSLIB_SERIALIZE operator>> to decode struct keys.
// ---------------------------------------------------------------------------
class be_key_reader {
   const char* _data;
   size_t      _size;
   size_t      _pos = 0;

   uint16_t read_be16() {
      sysio::check(_pos + 2 <= _size, "be_key_reader underflow");
      uint16_t v = (static_cast<uint16_t>(static_cast<uint8_t>(_data[_pos])) << 8)
                 |  static_cast<uint16_t>(static_cast<uint8_t>(_data[_pos + 1]));
      _pos += 2;
      return v;
   }

   uint32_t read_be32() {
      sysio::check(_pos + 4 <= _size, "be_key_reader underflow");
      uint32_t v = 0;
      for (int i = 0; i < 4; ++i)
         v = (v << 8) | static_cast<uint8_t>(_data[_pos++]);
      return v;
   }

   uint64_t read_be64() {
      sysio::check(_pos + 8 <= _size, "be_key_reader underflow");
      uint64_t v = 0;
      for (int i = 0; i < 8; ++i)
         v = (v << 8) | static_cast<uint8_t>(_data[_pos++]);
      return v;
   }

   void read_escaped(std::vector<char>& out) {
      out.clear();
      while (_pos < _size) {
         char c = _data[_pos++];
         if (c == '\0') {
            sysio::check(_pos < _size, "be_key_reader: truncated NUL-escape");
            char next = _data[_pos++];
            if (next == '\0') return;            // terminator
            sysio::check(next == '\x01', "be_key_reader: invalid NUL-escape byte");
            out.push_back('\0'); // escaped NUL
         } else {
            out.push_back(c);
         }
      }
      sysio::check(false, "be_key_reader: unterminated NUL-escape string");
   }

   void read_escaped(key_buf& out) {
      char tmp[be_key_stream::buf_cap];
      uint32_t n = 0;
      while (_pos < _size) {
         char c = _data[_pos++];
         if (c == '\0') {
            sysio::check(_pos < _size, "be_key_reader: truncated NUL-escape");
            char next = _data[_pos++];
            if (next == '\0') { out.assign(tmp, n); return; }
            sysio::check(next == '\x01', "be_key_reader: invalid NUL-escape byte");
            sysio::check(n < be_key_stream::buf_cap, "be_key_reader: string too large");
            tmp[n++] = '\0';
         } else {
            sysio::check(n < be_key_stream::buf_cap, "be_key_reader: string too large");
            tmp[n++] = c;
         }
      }
      sysio::check(false, "be_key_reader: unterminated NUL-escape string");
   }

public:
   be_key_reader(const char* data, size_t size) : _data(data), _size(size) {}

   be_key_reader& operator>>(uint8_t& v) {
      sysio::check(_pos + 1 <= _size, "be_key_reader underflow");
      v = static_cast<uint8_t>(_data[_pos++]);
      return *this;
   }

   be_key_reader& operator>>(int8_t& v) {
      uint8_t u; *this >> u;
      v = static_cast<int8_t>(u ^ 0x80u);
      return *this;
   }

   be_key_reader& operator>>(uint16_t& v) { v = read_be16(); return *this; }
   be_key_reader& operator>>(int16_t& v) {
      uint16_t u = read_be16();
      v = static_cast<int16_t>(u ^ 0x8000u);
      return *this;
   }

   be_key_reader& operator>>(uint32_t& v) { v = read_be32(); return *this; }
   be_key_reader& operator>>(int32_t& v) {
      uint32_t u = read_be32();
      v = static_cast<int32_t>(u ^ 0x80000000u);
      return *this;
   }

   be_key_reader& operator>>(uint64_t& v) { v = read_be64(); return *this; }
   be_key_reader& operator>>(int64_t& v) {
      uint64_t u = read_be64();
      v = static_cast<int64_t>(u ^ (uint64_t(1) << 63));
      return *this;
   }

   be_key_reader& operator>>(uint128_t& v) {
      uint64_t hi = read_be64(), lo = read_be64();
      v = (uint128_t(hi) << 64) | lo;
      return *this;
   }
   be_key_reader& operator>>(int128_t& v) {
      uint128_t u; *this >> u;
      v = static_cast<int128_t>(u ^ (uint128_t(1) << 127));
      return *this;
   }

   be_key_reader& operator>>(name& v) {
      uint64_t raw = read_be64();
      v = name(raw);
      return *this;
   }

   be_key_reader& operator>>(bool& v) {
      sysio::check(_pos + 1 <= _size, "be_key_reader underflow");
      v = (_data[_pos++] != 0);
      return *this;
   }

   be_key_reader& operator>>(float& v) {
      uint32_t bits = read_be32();
      // Reverse sign-magnitude transform
      if (bits & (uint32_t(1) << 31))
         bits ^= (uint32_t(1) << 31);  // was positive: flip sign bit
      else
         bits = ~bits;                  // was negative: flip all bits
      std::memcpy(&v, &bits, 4);
      return *this;
   }

   be_key_reader& operator>>(double& v) {
      uint64_t bits = read_be64();
      if (bits & (uint64_t(1) << 63))
         bits ^= (uint64_t(1) << 63);
      else
         bits = ~bits;
      std::memcpy(&v, &bits, 8);
      return *this;
   }

   be_key_reader& operator>>(std::string& v) {
      key_buf tmp;
      read_escaped(tmp);
      v.assign(tmp.data(), tmp.size());
      return *this;
   }

   be_key_reader& operator>>(std::vector<char>& v) {
      read_escaped(v);
      return *this;
   }
};

// ---------------------------------------------------------------------------
// Extractor helpers
// ---------------------------------------------------------------------------

/// Extract a data member value (analogous to const_mem_fun for member functions).
template<class Class, typename Type, Type Class::*Ptr>
struct member_data {
   typedef Type result_type;
   Type operator()(const Class& x) const { return x.*Ptr; }
};

/// Index declaration for table (analogous to sysio::indexed_by).
template<name::raw IndexName, typename Extractor>
struct index {
   static constexpr uint64_t index_name = static_cast<uint64_t>(IndexName);
   typedef Extractor secondary_extractor_type;
};

// ---------------------------------------------------------------------------
// table
// ---------------------------------------------------------------------------

template<name::raw TableName, typename K, typename V, typename... Indices>
class table {
   static_assert(sizeof...(Indices) <= 16, "table supports at most 16 secondary indices");

   static constexpr uint32_t _table_id = sysio::kv::compute_table_id(static_cast<uint64_t>(TableName));

   // --- Compile-time table_id collision detection ---
   template<uint32_t... ids>
   static constexpr bool all_unique() {
      uint32_t arr[] = { ids... };
      for (size_t i = 0; i < sizeof...(ids); ++i)
         for (size_t j = i + 1; j < sizeof...(ids); ++j)
            if (arr[i] == arr[j]) return false;
      return true;
   }
   // Check primary table_id doesn't collide with any secondary index table_id
   static_assert(
      sizeof...(Indices) == 0 ||
      all_unique<_table_id,
         sysio::kv::compute_sec_table_id(static_cast<uint64_t>(TableName), Indices::index_name)...>(),
      "table_id collision detected: primary table and a secondary index have the same table_id"
   );

   uint64_t _code = 0;
   uint64_t code() const { return _code ? _code : sysio::current_receiver().value; }

   // --- Key / value encoding helpers ---

   static be_key_stream make_key(const K& key) {
      be_key_stream bs;
      bs << key;
      return bs;
   }

   static K decode_key(const char* data, size_t size) {
      K key;
      be_key_reader rd(data, size);
      rd >> key;
      return key;
   }

   static ser_buf serialize_value(const V& value) {
      if constexpr (std::is_trivially_copyable<V>::value) {
         if (sizeof(V) == sysio::pack_size(value)) {
            ser_buf buf(sizeof(V));
            std::memcpy(buf.data(), &value, sizeof(V));
            return buf;
         }
      }
      uint32_t sz = sysio::pack_size(value);
      ser_buf buf(sz);
      sysio::datastream<char*> ds(buf.data(), buf.size());
      ds << value;
      return buf;
   }

   static V deserialize_value(const char* data, size_t size) {
      V value;
      if constexpr (std::is_trivially_copyable<V>::value) {
         if (size == sizeof(V)) {
            std::memcpy(&value, data, sizeof(V));
            return value;
         }
      }
      sysio::datastream<const char*> ds(data, size);
      ds >> value;
      return value;
   }

   template<typename SecKey>
   static be_key_stream encode_sec_key(const SecKey& key) {
      be_key_stream bs;
      bs << key;
      return bs;
   }

   template<typename SecKey>
   static SecKey decode_sec_key(const char* data, size_t size) {
      SecKey key;
      be_key_reader rd(data, size);
      rd >> key;
      return key;
   }

   // --- Secondary index management (recursive template) ---

   template<size_t N, typename Index, typename... Rest>
   struct secondary_ops {
      static constexpr uint32_t _sec_tid = sysio::kv::compute_sec_table_id(
         static_cast<uint64_t>(TableName), Index::index_name);

      static void store_all(uint64_t payer, const char* pri_data, uint32_t pri_size, const V& value) {
         using ext_t = typename Index::secondary_extractor_type;
         ext_t ext;
         auto sec = encode_sec_key(ext(value));
         ::kv_idx_store(payer, _sec_tid,
                        pri_data, pri_size, sec.data(), sec.size());
         if constexpr (sizeof...(Rest) > 0)
            secondary_ops<N+1, Rest...>::store_all(payer, pri_data, pri_size, value);
      }

      static void remove_all(const char* pri_data, uint32_t pri_size, const V& value) {
         using ext_t = typename Index::secondary_extractor_type;
         ext_t ext;
         auto sec = encode_sec_key(ext(value));
         ::kv_idx_remove(_sec_tid,
                         pri_data, pri_size, sec.data(), sec.size());
         if constexpr (sizeof...(Rest) > 0)
            secondary_ops<N+1, Rest...>::remove_all(pri_data, pri_size, value);
      }

      static void update_all(uint64_t payer, const char* pri_data, uint32_t pri_size,
                              const V& old_val, const V& new_val) {
         using ext_t = typename Index::secondary_extractor_type;
         ext_t ext;
         auto old_sec = encode_sec_key(ext(old_val));
         auto new_sec = encode_sec_key(ext(new_val));
         bool same = (old_sec.size() == new_sec.size()) &&
                     (old_sec.size() == 0 || std::memcmp(old_sec.data(), new_sec.data(), old_sec.size()) == 0);
         if (!same) {
            ::kv_idx_update(payer, _sec_tid,
                            pri_data, pri_size,
                            old_sec.data(), old_sec.size(),
                            new_sec.data(), new_sec.size());
         }
         if constexpr (sizeof...(Rest) > 0)
            secondary_ops<N+1, Rest...>::update_all(payer, pri_data, pri_size, old_val, new_val);
      }
   };

   struct no_secondary_ops {
      static void store_all(uint64_t, const char*, uint32_t, const V&) {}
      static void remove_all(const char*, uint32_t, const V&) {}
      static void update_all(uint64_t, const char*, uint32_t, const V&, const V&) {}
   };

   template<typename... Is>
   struct sec_ops_selector { using type = secondary_ops<0, Is...>; };
   template<>
   struct sec_ops_selector<> { using type = no_secondary_ops; };
   using sec_ops = typename sec_ops_selector<Indices...>::type;

   void store_secondaries(uint64_t payer, const K& key, const V& value) {
      auto pri = make_key(key);
      sec_ops::store_all(payer, pri.data(), pri.size(), value);
   }
   void remove_secondaries(const K& key, const V& value) {
      auto pri = make_key(key);
      sec_ops::remove_all(pri.data(), pri.size(), value);
   }
   void update_secondaries(uint64_t payer, const K& key, const V& old_val, const V& new_val) {
      auto pri = make_key(key);
      sec_ops::update_all(payer, pri.data(), pri.size(), old_val, new_val);
   }

   // Internal insert (no duplicate check — caller must verify)
   void do_insert(uint64_t payer, const be_key_stream& k, const K& key, const V& value) {
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &value, sizeof(V));
         ::kv_set(_table_id, payer, k.data(), k.size(), vbuf, sizeof(V));
      } else {
         auto v = serialize_value(value);
         ::kv_set(_table_id, payer, k.data(), k.size(), v.data(), v.size());
      }
      store_secondaries(payer, key, value);
   }

   // Internal erase used by both primary and secondary erase paths
   void do_erase(const K& key, const V& value) {
      remove_secondaries(key, value);
      auto pri = make_key(key);
      ::kv_erase(_table_id, pri.data(), pri.size());
   }

public:
   /// Construct an table. Reads/iterates against \p code's data (default: current contract).
   table(sysio::name code = sysio::name{}) : _code(code.value) {}

   name get_code() const { return name(_code ? _code : sysio::current_receiver().value); }

   // --- Row type ---
   struct row {
      K key;
      V value;
   };

   // --- Primary const_iterator ---
   struct const_iterator {
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type        = V;
      using difference_type   = std::ptrdiff_t;
      using pointer           = const V*;
      using reference         = const V&;

      const_iterator() = default;

      /// Dereference returns the value (like multi_index). Use key() for the key.
      const V& operator*()  const { sysio::check(_valid, "deref end iterator"); return _row.value; }
      const V* operator->() const { sysio::check(_valid, "deref end iterator"); return &_row.value; }

      /// Access the key at the current position.
      const K& key()  const { sysio::check(_valid, "key() on end iterator"); return _row.key; }

      const_iterator& operator++() {
         if (!_valid) return *this;
         if (::kv_it_next(_handle) != 0) _valid = false;
         else load();
         return *this;
      }

      const_iterator& operator--() {
         if (_handle < 0) {
            ensure_handle();
            char max_key[kv_key_max_bytes];
            memset(max_key, 0xFF, sizeof(max_key));
            ::kv_it_lower_bound(_handle, max_key, sizeof(max_key));
            if (::kv_it_prev(_handle) == 0) { _valid = true; load(); }
            else _valid = false;
         } else {
            if (::kv_it_prev(_handle) == 0) { _valid = true; load(); }
            else _valid = false;
         }
         return *this;
      }

      // Post-increment/decrement deleted: the underlying KV iterator handle is
      // a unique resource — returning a copy of the old position would require
      // cloning the handle, which is expensive. Use ++it / --it instead.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         return a._valid == b._valid && (!a._valid || a._raw_key == b._raw_key);
      }

      const_iterator(const_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid),
           _row(std::move(o._row)), _raw_key(std::move(o._raw_key))
      { o._valid = false; }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid;
            _row = std::move(o._row); _raw_key = std::move(o._raw_key);
            o._valid = false;
         }
         return *this;
      }

      /// Copy constructor: re-creates an iterator at the same position.
      /// Required for std::reverse_iterator (CopyConstructible).
      const_iterator(const const_iterator& o)
         : _tbl(o._tbl), _valid(o._valid), _row(o._row)
      {
         if (o._valid && !o._raw_key.empty()) {
            _raw_key.assign(o._raw_key.data(), o._raw_key.size());
            _handle.reset(::kv_it_create(_table_id, _tbl->code(), nullptr, 0));
            ::kv_it_lower_bound(_handle, _raw_key.data(), _raw_key.size());
         }
      }
      const_iterator& operator=(const const_iterator& o) {
         if (this != &o) {
            _tbl = o._tbl; _valid = o._valid; _row = o._row;
            if (o._valid && !o._raw_key.empty()) {
               _raw_key.assign(o._raw_key.data(), o._raw_key.size());
               _handle.reset(::kv_it_create(_table_id, _tbl->code(), nullptr, 0));
               ::kv_it_lower_bound(_handle, _raw_key.data(), _raw_key.size());
            }
         }
         return *this;
      }

   private:
      friend class table;
      const table* _tbl = nullptr;
      kv::detail::it_handle _handle;
      bool                 _valid = false;
      row                  _row;
      key_buf              _raw_key;

      const_iterator(const table* t, int32_t h, bool valid)
         : _tbl(t), _handle(h), _valid(valid) {
         if (_valid) load();
      }
      static const_iterator make_end(const table* t) {
         const_iterator it; it._tbl = t; return it;
      }

      void load() {
         char key_stack[key_buf::inline_cap];
         uint32_t key_size = 0;
         if (::kv_it_key(_handle, 0, key_stack, key_buf::inline_cap, &key_size) != 0) {
            _valid = false; return;
         }
         if (key_size <= key_buf::inline_cap) {
            _raw_key.assign(key_stack, key_size);
         } else {
            char* kh = new char[key_size];
            ::kv_it_key(_handle, 0, kh, key_size, &key_size);
            _raw_key.assign(kh, key_size);
            delete[] kh;
         }
         _row.key = decode_key(_raw_key.data(), _raw_key.size());

         if constexpr (is_fixed_serializable_v<V>) {
            char vbuf[sizeof(V)];
            uint32_t val_size = 0;
            ::kv_it_value(_handle, 0, vbuf, sizeof(V), &val_size);
            std::memcpy(&_row.value, vbuf, sizeof(V));
         } else {
            char val_stack[kv_value_stack_size];
            uint32_t val_size = 0;
            ::kv_it_value(_handle, 0, val_stack, kv_value_stack_size, &val_size);
            if (val_size <= kv_value_stack_size) {
               _row.value = deserialize_value(val_stack, val_size);
            } else {
               char* heap = new char[val_size];
               ::kv_it_value(_handle, 0, heap, val_size, &val_size);
               _row.value = deserialize_value(heap, val_size);
               delete[] heap;
            }
         }
      }

      void ensure_handle() {
         if (_handle >= 0) return;
         if (!_tbl) return;
         _handle.reset(::kv_it_create(_table_id, _tbl->code(), nullptr, 0));
         if (_valid && !_raw_key.empty())
            ::kv_it_lower_bound(_handle, _raw_key.data(), _raw_key.size());
      }
   };

   // --- Primary key operations ---

   const_iterator begin() const {
      uint32_t h = ::kv_it_create(_table_id, code(), nullptr, 0);
      return const_iterator(this, h, ::kv_it_status(h) == 0);
   }

   const_iterator end() const { return const_iterator::make_end(this); }

   const_iterator find(const K& key) const {
      auto k = make_key(key);
      if (!::kv_contains(_table_id, code(), k.data(), k.size()))
         return end();
      uint32_t h = ::kv_it_create(_table_id, code(), nullptr, 0);
      ::kv_it_lower_bound(h, k.data(), k.size());
      return const_iterator(this, h, true);
   }

   const_iterator require_find(const K& key, const char* msg = "unable to find key") const {
      auto it = find(key);
      sysio::check(it != end(), msg);
      return it;
   }

   /// Returns the value for key, or nullopt if not found.
   std::optional<V> try_get(const K& key) const {
      auto k = make_key(key);
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         int32_t sz = ::kv_get(_table_id, code(), k.data(), k.size(), vbuf, sizeof(V));
         if (sz < 0) return {};
         V val; std::memcpy(&val, vbuf, sizeof(V));
         return val;
      } else {
         char stack[kv_value_stack_size];
         int32_t sz = ::kv_get(_table_id, code(), k.data(), k.size(), stack, kv_value_stack_size);
         if (sz < 0) return {};
         if (sz <= static_cast<int32_t>(kv_value_stack_size))
            return deserialize_value(stack, sz);
         char* heap = new char[sz];
         ::kv_get(_table_id, code(), k.data(), k.size(), heap, sz);
         V result = deserialize_value(heap, sz);
         delete[] heap;
         return result;
      }
   }

   /// Returns the value for key. Asserts if not found.
   V get(const K& key, const char* msg = "key not found") const {
      auto val = try_get(key);
      sysio::check(val.has_value(), msg);
      return *val;
   }

   bool contains(const K& key) const {
      auto k = make_key(key);
      return ::kv_contains(_table_id, code(), k.data(), k.size()) != 0;
   }

   const_iterator lower_bound(const K& key) const {
      auto k = make_key(key);
      uint32_t h = ::kv_it_create(_table_id, code(), nullptr, 0);
      int32_t status = ::kv_it_lower_bound(h, k.data(), k.size());
      return const_iterator(this, h, status == 0);
   }

   const_iterator upper_bound(const K& key) const {
      auto it = lower_bound(key);
      auto encoded = make_key(key);
      if (it != end() && it._raw_key.equals(encoded.data(), encoded.size())) ++it;
      return it;
   }

   // --- Mutation ---

   /// Insert a new row. Asserts if the key already exists. Use upsert()/set()
   /// for insert-or-update semantics.
   void emplace(name payer, const K& key, const V& value) {
      auto k = make_key(key);
      sysio::check(!::kv_contains(_table_id, code(), k.data(), k.size()),
                   "emplace: key already exists (use upsert for insert-or-update)");
      do_insert(payer.value, k, key, value);
   }

   void emplace(const K& key, const V& value) {
      emplace(name{}, key, value);
   }

   /// Lambda emplace: construct the value in-place.
   template<typename Lambda, typename = std::enable_if_t<std::is_invocable_v<Lambda, V&>>>
   void emplace(name payer, const K& key, Lambda&& constructor) {
      V value{};
      constructor(value);
      emplace(payer, key, value);
   }

   template<typename Lambda, typename = std::enable_if_t<std::is_invocable_v<Lambda, V&>>>
   void emplace(const K& key, Lambda&& constructor) {
      emplace(name{}, key, std::forward<Lambda>(constructor));
   }

   /// Upsert convenience alias.
   void set(name payer, const K& key, const V& value) { upsert(payer, key, value); }
   void set(const K& key, const V& value) { upsert(name{}, key, value); }

   /// Insert or update a row. If the key already exists, the old value is read
   /// and secondary indexes are updated correctly (like modify). If the key is
   /// new, it is inserted (like emplace). Costs one extra kv_get on the insert
   /// path to check for existence.
   void upsert(name payer, const K& key, const V& value) {
      auto k = make_key(key);
      if constexpr (is_fixed_serializable_v<V>) {
         char old_vbuf[sizeof(V)];
         int32_t old_sz = ::kv_get(_table_id, code(), k.data(), k.size(), old_vbuf, sizeof(V));
         if (old_sz >= 0) {
            V old_value; std::memcpy(&old_value, old_vbuf, sizeof(V));
            update_secondaries(payer.value, key, old_value, value);
         } else {
            store_secondaries(payer.value, key, value);
         }
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &value, sizeof(V));
         ::kv_set(_table_id, payer.value, k.data(), k.size(), vbuf, sizeof(V));
      } else {
         char stack[kv_value_stack_size];
         int32_t old_sz = ::kv_get(_table_id, code(), k.data(), k.size(), stack, kv_value_stack_size);
         if (old_sz >= 0) {
            const char* old_data = stack;
            char* heap = nullptr;
            if (old_sz > static_cast<int32_t>(kv_value_stack_size)) {
               heap = new char[old_sz];
               ::kv_get(_table_id, code(), k.data(), k.size(), heap, old_sz);
               old_data = heap;
            }
            V old_value = deserialize_value(old_data, old_sz);
            delete[] heap;
            update_secondaries(payer.value, key, old_value, value);
         } else {
            store_secondaries(payer.value, key, value);
         }
         auto v = serialize_value(value);
         ::kv_set(_table_id, payer.value, k.data(), k.size(), v.data(), v.size());
      }
   }

   void upsert(const K& key, const V& value) {
      upsert(name{}, key, value);
   }

   void modify(name payer, const const_iterator& it, const V& new_value) {
      sysio::check(it._valid, "cannot modify end iterator");
      auto k = make_key(it._row.key);
      update_secondaries(payer.value, it._row.key, it._row.value, new_value);
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &new_value, sizeof(V));
         ::kv_set(_table_id, payer.value, k.data(), k.size(), vbuf, sizeof(V));
      } else {
         auto v = serialize_value(new_value);
         ::kv_set(_table_id, payer.value, k.data(), k.size(), v.data(), v.size());
      }
   }

   void modify(const const_iterator& it, const V& new_value) {
      modify(name{}, it, new_value);
   }

   /// Modify by key + lambda (like multi_index).
   template<typename Lambda>
   void modify(name payer, const K& key, Lambda&& updater) {
      V old_val = get(key, "modify: key not found");
      V new_val = old_val;
      updater(new_val);
      auto k = make_key(key);
      update_secondaries(payer.value, key, old_val, new_val);
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &new_val, sizeof(V));
         ::kv_set(_table_id, payer.value, k.data(), k.size(), vbuf, sizeof(V));
      } else {
         auto v = serialize_value(new_val);
         ::kv_set(_table_id, payer.value, k.data(), k.size(), v.data(), v.size());
      }
   }

   template<typename Lambda>
   void modify(const K& key, Lambda&& updater) {
      modify(name{}, key, std::forward<Lambda>(updater));
   }

   const_iterator erase(const_iterator it) {
      sysio::check(it._valid, "cannot erase end iterator");
      K saved_key = it._row.key;
      V saved_value = it._row.value;
      ++it;
      do_erase(saved_key, saved_value);
      return it;
   }

   /// Erase by key. Asserts if key not found.
   void erase(const K& key) {
      V val = get(key, "erase: key not found");
      do_erase(key, val);
   }

   // --- Iterator aliases ---

   const_iterator cbegin() const { return begin(); }
   const_iterator cend() const { return end(); }

   using const_reverse_iterator = std::reverse_iterator<const_iterator>;
   const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
   const_reverse_iterator rend() const { return const_reverse_iterator(begin()); }
   const_reverse_iterator crbegin() const { return rbegin(); }
   const_reverse_iterator crend() const { return rend(); }

   // --- Auto-increment primary key ---
   // Available only when K has a primary_key() method returning an integral type.

private:
   template<typename KK, typename = void>
   struct has_primary_key : std::false_type {};
   template<typename KK>
   struct has_primary_key<KK, std::void_t<decltype(std::declval<KK>().primary_key())>> : std::true_type {};

public:
   /// Returns the next available primary key (max existing + 1, or 0 if empty).
   /// Only available when K has a primary_key() method.
   template<typename KK = K, std::enable_if_t<has_primary_key<KK>::value, int> = 0>
   uint64_t available_primary_key() const {
      auto it = end();
      if (begin() == it) return 0;
      --it;
      auto pk = it.key().primary_key();
      sysio::check(pk < std::numeric_limits<uint64_t>::max(),
                   "next primary key in table is at autoincrement limit");
      return pk + 1;
   }

   // --- Secondary index access ---

   template<name::raw IndexName>
   struct secondary_index_view {
      // Compile-time index number lookup
      template<size_t N, typename First, typename... Rest>
      struct find_index_number {
         static constexpr size_t value =
            (First::index_name == static_cast<uint64_t>(IndexName))
            ? N : find_index_number<N+1, Rest...>::value;
      };
      template<size_t N, typename Last>
      struct find_index_number<N, Last> {
         static constexpr size_t value =
            (Last::index_name == static_cast<uint64_t>(IndexName))
            ? N : N + 1;
      };

      static constexpr size_t index_number = find_index_number<0, Indices...>::value;
      static_assert(index_number < sizeof...(Indices), "invalid secondary index name");
      using index_type             = typename std::tuple_element<index_number, std::tuple<Indices...>>::type;
      static constexpr uint32_t _sec_table_id = sysio::kv::compute_sec_table_id(
         static_cast<uint64_t>(TableName), index_type::index_name);
      using secondary_extractor_type = typename index_type::secondary_extractor_type;
      using secondary_key_type     = std::decay_t<typename secondary_extractor_type::result_type>;

      table* _tbl;
      secondary_index_view(table& tbl) : _tbl(&tbl) {}

      // --- key_row for key-only iteration ---
      struct key_row {
         K              key;
         secondary_key_type sec_key;
      };

      // --- Secondary const_iterator ---
      struct const_iterator {
         using iterator_category = std::bidirectional_iterator_tag;
         using value_type        = row;
         using difference_type   = std::ptrdiff_t;
         using pointer           = const row*;
         using reference         = const row&;

         const_iterator() = default;

         const V& operator*()  const { sysio::check(_valid, "deref invalid sec iter"); return _row.value; }
         const V* operator->() const { sysio::check(_valid, "deref invalid sec iter"); return &_row.value; }
         const K& key()  const { sysio::check(_valid, "key() on invalid sec iter"); return _row.key; }

         const_iterator& operator++() {
            if (!_valid || _handle < 0) return *this;
            if (::kv_idx_next(_handle) == 0) load_current();
            else _valid = false;
            return *this;
         }

         const_iterator& operator--() {
            if (_handle < 0) {
               char max_sec[kv_key_max_bytes];
               memset(max_sec, 0xFF, sizeof(max_sec));
               _handle.reset(::kv_idx_lower_bound(
                  _tbl->code(), _sec_table_id,
                  max_sec, sizeof(max_sec)));
               if (_handle < 0) { _valid = false; }
               else if (::kv_idx_prev(_handle) == 0) { _valid = true; load_current(); }
               else { _valid = false; }
            } else {
               if (::kv_idx_prev(_handle) == 0) { _valid = true; load_current(); }
               else { _valid = false; }
            }
            return *this;
         }

         // Post-increment/decrement deleted: KV iterator handle is a unique resource.
         const_iterator operator++(int) = delete;
         const_iterator operator--(int) = delete;

         friend bool operator==(const const_iterator& a, const const_iterator& b) {
            if (!a._valid && !b._valid) return true;
            if (!a._valid || !b._valid) return false;
            return a._pri_bytes == b._pri_bytes;
         }

         const_iterator(const_iterator&& o) noexcept
            : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid),
              _row(std::move(o._row)), _pri_bytes(std::move(o._pri_bytes))
         { o._valid = false; }
         const_iterator& operator=(const_iterator&& o) noexcept {
            if (this != &o) {
               _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid;
               _row = std::move(o._row); _pri_bytes = std::move(o._pri_bytes);
               o._valid = false;
            }
            return *this;
         }
         const_iterator(const const_iterator&) = delete;
         const_iterator& operator=(const const_iterator&) = delete;

      private:
         friend struct secondary_index_view;
         table* _tbl = nullptr;
         kv::detail::idx_handle _handle;
         bool           _valid = false;
         row            _row;
         key_buf        _pri_bytes;

         const_iterator(table* tbl, int32_t handle, bool valid)
            : _tbl(tbl), _handle(handle), _valid(valid) {
            if (_valid) load_current();
         }
         static const_iterator make_end(table* tbl) {
            const_iterator it; it._tbl = tbl; return it;
         }

         void load_current() {
            if (_handle < 0) { _valid = false; return; }
            char pri_stack[key_buf::inline_cap];
            uint32_t pri_size = 0;
            if (::kv_idx_primary_key(_handle, 0, pri_stack, key_buf::inline_cap, &pri_size) != 0) {
               _valid = false; return;
            }
            if (pri_size <= key_buf::inline_cap) {
               _pri_bytes.assign(pri_stack, pri_size);
            } else {
               char* ph = new char[pri_size];
               ::kv_idx_primary_key(_handle, 0, ph, pri_size, &pri_size);
               _pri_bytes.assign(ph, pri_size);
               delete[] ph;
            }
            _row.key = decode_key(_pri_bytes.data(), _pri_bytes.size());

            if constexpr (is_fixed_serializable_v<V>) {
               char vbuf[sizeof(V)];
               int32_t val_sz = ::kv_get(_table_id, _tbl->code(),
                                         _pri_bytes.data(), _pri_bytes.size(),
                                         vbuf, sizeof(V));
               if (val_sz < 0) { _valid = false; return; }
               std::memcpy(&_row.value, vbuf, sizeof(V));
            } else {
               char val_stack[kv_value_stack_size];
               int32_t val_sz = ::kv_get(_table_id, _tbl->code(),
                                         _pri_bytes.data(), _pri_bytes.size(),
                                         val_stack, kv_value_stack_size);
               if (val_sz < 0) { _valid = false; return; }
               if (val_sz <= static_cast<int32_t>(kv_value_stack_size)) {
                  _row.value = deserialize_value(val_stack, val_sz);
               } else {
                  char* heap = new char[val_sz];
                  ::kv_get(_table_id, _tbl->code(),
                           _pri_bytes.data(), _pri_bytes.size(), heap, val_sz);
                  _row.value = deserialize_value(heap, val_sz);
                  delete[] heap;
               }
            }
         }
      };

      // --- Key-only iterator (no kv_get, no value deserialization) ---
      struct key_iterator {
         using iterator_category = std::bidirectional_iterator_tag;
         using value_type        = key_row;
         using difference_type   = std::ptrdiff_t;
         using pointer           = const key_row*;
         using reference         = const key_row&;

         key_iterator() = default;

         const key_row& operator*()  const { sysio::check(_valid, "deref invalid key_iterator"); return _kr; }
         const key_row* operator->() const { sysio::check(_valid, "deref invalid key_iterator"); return &_kr; }

         key_iterator& operator++() {
            if (!_valid || _handle < 0) return *this;
            if (::kv_idx_next(_handle) == 0) load_keys();
            else _valid = false;
            return *this;
         }

         key_iterator& operator--() {
            if (_handle < 0) {
               char max_sec[kv_key_max_bytes];
               memset(max_sec, 0xFF, sizeof(max_sec));
               _handle.reset(::kv_idx_lower_bound(
                  _tbl->code(), _sec_table_id,
                  max_sec, sizeof(max_sec)));
               if (_handle < 0) { _valid = false; }
               else if (::kv_idx_prev(_handle) == 0) { _valid = true; load_keys(); }
               else { _valid = false; }
            } else {
               if (::kv_idx_prev(_handle) == 0) { _valid = true; load_keys(); }
               else { _valid = false; }
            }
            return *this;
         }

         // Post-increment/decrement deleted: KV iterator handle is a unique resource.
         key_iterator operator++(int) = delete;
         key_iterator operator--(int) = delete;

         friend bool operator==(const key_iterator& a, const key_iterator& b) {
            if (!a._valid && !b._valid) return true;
            if (!a._valid || !b._valid) return false;
            return a._pri_bytes == b._pri_bytes;
         }

         key_iterator(key_iterator&& o) noexcept
            : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid),
              _kr(std::move(o._kr)), _pri_bytes(std::move(o._pri_bytes))
         { o._valid = false; }
         key_iterator& operator=(key_iterator&& o) noexcept {
            if (this != &o) {
               _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid;
               _kr = std::move(o._kr); _pri_bytes = std::move(o._pri_bytes);
               o._valid = false;
            }
            return *this;
         }
         key_iterator(const key_iterator&) = delete;
         key_iterator& operator=(const key_iterator&) = delete;

      private:
         friend struct secondary_index_view;
         table* _tbl = nullptr;
         kv::detail::idx_handle _handle;
         bool           _valid = false;
         key_row        _kr;
         key_buf        _pri_bytes;

         key_iterator(table* tbl, int32_t handle, bool valid)
            : _tbl(tbl), _handle(handle), _valid(valid) {
            if (_valid) load_keys();
         }
         static key_iterator make_end(table* tbl) {
            key_iterator it; it._tbl = tbl; return it;
         }

         void load_keys() {
            if (_handle < 0) { _valid = false; return; }
            char pri_stack[key_buf::inline_cap];
            uint32_t pri_size = 0;
            if (::kv_idx_primary_key(_handle, 0, pri_stack, key_buf::inline_cap, &pri_size) != 0) {
               _valid = false; return;
            }
            if (pri_size <= key_buf::inline_cap) {
               _pri_bytes.assign(pri_stack, pri_size);
            } else {
               char* ph = new char[pri_size];
               ::kv_idx_primary_key(_handle, 0, ph, pri_size, &pri_size);
               _pri_bytes.assign(ph, pri_size);
               delete[] ph;
            }
            _kr.key = decode_key(_pri_bytes.data(), _pri_bytes.size());

            char sec_stack[64];
            uint32_t sec_size = 0;
            ::kv_idx_key(_handle, 0, sec_stack, 64, &sec_size);
            if (sec_size <= 64) {
               _kr.sec_key = decode_sec_key<secondary_key_type>(sec_stack, sec_size);
            } else {
               char* heap = new char[sec_size];
               ::kv_idx_key(_handle, 0, heap, sec_size, &sec_size);
               _kr.sec_key = decode_sec_key<secondary_key_type>(heap, sec_size);
               delete[] heap;
            }
         }
      };

      // --- Secondary index view API ---

      const_iterator begin() const {
         int32_t handle = ::kv_idx_lower_bound(
            _tbl->code(), _sec_table_id, nullptr, 0);
         if (handle < 0) return end();
         return const_iterator(_tbl, handle, true);
      }

      const_iterator end() const { return const_iterator::make_end(_tbl); }

      template<typename SecKey>
      const_iterator find(const SecKey& sec_key) const {
         auto sec = encode_sec_key(secondary_key_type(sec_key));
         int32_t handle = ::kv_idx_find_secondary(
            _tbl->code(), _sec_table_id,
            sec.data(), sec.size());
         if (handle < 0) return end();
         return const_iterator(_tbl, handle, true);
      }

      template<typename SecKey>
      const_iterator lower_bound(const SecKey& sec_key) const {
         auto sec = encode_sec_key(secondary_key_type(sec_key));
         int32_t handle = ::kv_idx_lower_bound(
            _tbl->code(), _sec_table_id,
            sec.data(), sec.size());
         if (handle < 0) return end();
         return const_iterator(_tbl, handle, true);
      }

      template<typename SecKey>
      const_iterator upper_bound(const SecKey& sec_key) const {
         auto sec = encode_sec_key(secondary_key_type(sec_key));
         // Append a NUL byte to make the query strictly greater than the encoded
         // key under byte comparison, so kv_idx_lower_bound returns the first
         // entry past the target.  Works for both fixed-size BE keys (extra byte
         // extends length) and NUL-escape encoded strings (0x00 is the escape
         // sentinel, so the extended key cannot collide with a valid encoding).
         char nul = '\0';
         sec.write(&nul, 1);
         int32_t handle = ::kv_idx_lower_bound(
            _tbl->code(), _sec_table_id,
            sec.data(), sec.size());
         if (handle < 0) return end();
         return const_iterator(_tbl, handle, true);
      }

      template<typename SecKey>
      const_iterator require_find(const SecKey& sec_key,
                                  const char* msg = "unable to find secondary key") const {
         auto itr = find(sec_key);
         sysio::check(itr != end(), msg);
         return itr;
      }

      /// Modify the primary row referenced by a secondary iterator.
      /// The iterator is invalidated after this call — both the cached value
      /// and the iterator's position in the secondary index may be stale
      /// (if the secondary key changed, the underlying index entry was moved).
      /// Do not dereference or advance the iterator after modify; re-find instead.
      void modify(name payer, const const_iterator& itr, const V& new_value) {
         sysio::check(itr._valid, "cannot modify end iterator");
         auto k = make_key(itr._row.key);
         _tbl->update_secondaries(
            payer.value, itr._row.key, itr._row.value, new_value);
         if constexpr (is_fixed_serializable_v<V>) {
            char vbuf[sizeof(V)];
            std::memcpy(vbuf, &new_value, sizeof(V));
            ::kv_set(_table_id, payer.value, k.data(), k.size(), vbuf, sizeof(V));
         } else {
            auto v = serialize_value(new_value);
            ::kv_set(_table_id, payer.value, k.data(), k.size(), v.data(), v.size());
         }
      }

      void modify(const const_iterator& itr, const V& new_value) {
         modify(name{}, itr, new_value);
      }

      // Erase the primary row referenced by a secondary iterator. Returns next.
      const_iterator erase(const_iterator itr) {
         sysio::check(itr._valid, "cannot erase end iterator");
         K saved_key = itr._row.key;
         V saved_value = itr._row.value;
         ++itr;
         _tbl->do_erase(saved_key, saved_value);
         return itr;
      }

      // Key-only iteration
      key_iterator key_begin() const {
         int32_t handle = ::kv_idx_lower_bound(
            _tbl->code(), _sec_table_id, nullptr, 0);
         if (handle < 0) return key_end();
         return key_iterator(_tbl, handle, true);
      }

      key_iterator key_end() const { return key_iterator::make_end(_tbl); }

      name get_code() const { return _tbl->get_code(); }
   };

   template<name::raw IndexName>
   secondary_index_view<IndexName> get_index() {
      return secondary_index_view<IndexName>(*this);
   }
};

} } // namespace sysio::kv
