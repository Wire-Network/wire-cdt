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
 *   sysio::const_mem_fun comes from <sysio/multi_index.hpp>, which this header does not include.
 *
 *   my_table tbl;
 *   tbl.emplace(get_self(), {1}, {1000, "alice"_n});
 *   auto idx = tbl.get_index<"byowner"_n>();
 *   auto it  = idx.find("alice"_n);
 */

#include <sysio/kv_utils.hpp>   // the KV host functions, through <sysio/kv.h>
#include <sysio/check.hpp>

#include <cstring>
#include <limits>
#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace sysio { namespace kv {

// ---------------------------------------------------------------------------
// BE key decoder — reverse of be_key_stream.
// Works with SYSLIB_SERIALIZE operator>> to decode struct keys.
// ---------------------------------------------------------------------------
class be_key_reader {
   const char* _data;
   size_t      _size;
   size_t      _pos = 0;

   void require(size_t bytes) const { sysio::check(_pos + bytes <= _size, msg::reader_underflow); }

   uint16_t read_be16() {
      require(sizeof(uint16_t));
      uint16_t v = (static_cast<uint16_t>(static_cast<uint8_t>(_data[_pos])) << 8)
                 |  static_cast<uint16_t>(static_cast<uint8_t>(_data[_pos + 1]));
      _pos += sizeof(uint16_t);
      return v;
   }

   uint32_t read_be32() {
      require(sizeof(uint32_t));
      uint32_t v = 0;
      for (size_t i = 0; i < sizeof(uint32_t); ++i)
         v = (v << 8) | static_cast<uint8_t>(_data[_pos++]);
      return v;
   }

   uint64_t read_be64() {
      require(sizeof(uint64_t));
      const uint64_t v = decode_be64(_data + _pos);
      _pos += sizeof(uint64_t);
      return v;
   }

   /// Decode a NUL-escaped field through its terminator, handing each byte to \p push.
   template<typename Push>
   void read_escaped(Push&& push) {
      while (_pos < _size) {
         const char c = _data[_pos++];
         if (c != '\0') {
            push(c);
            continue;
         }
         sysio::check(_pos < _size, msg::reader_truncated);
         const char next = _data[_pos++];
         if (next == '\0') return;            // terminator
         sysio::check(next == '\x01', msg::reader_bad_escape);
         push('\0'); // escaped NUL
      }
      sysio::check(false, msg::reader_unterminated);
   }

public:
   be_key_reader(const char* data, size_t size) : _data(data), _size(size) {}

   /// Raw bytes, which fixed_bytes keys read through. The reverse of be_key_stream::write.
   void read(char* out, size_t size) {
      require(size);
      std::memcpy(out, _data + _pos, size);
      _pos += size;
   }

   be_key_reader& operator>>(uint8_t& v) {
      require(sizeof(uint8_t));
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
      v = (static_cast<uint128_t>(hi) << 64) | lo;
      return *this;
   }
   be_key_reader& operator>>(int128_t& v) {
      uint128_t u; *this >> u;
      v = static_cast<int128_t>(u ^ (static_cast<uint128_t>(1) << 127));
      return *this;
   }

   be_key_reader& operator>>(name& v) {
      uint64_t raw = read_be64();
      v = name(raw);
      return *this;
   }

   be_key_reader& operator>>(bool& v) {
      static_assert(sizeof(bool) == 1);
      require(sizeof(bool));
      v = (_data[_pos++] != 0);
      return *this;
   }

   be_key_reader& operator>>(float& v) {
      static_assert(sizeof(float) == sizeof(uint32_t));
      static_assert(std::numeric_limits<float>::is_iec559);
      const uint32_t bits = detail::float_bits_from_sortable(read_be32());
      std::memcpy(&v, &bits, sizeof(float));
      return *this;
   }

   be_key_reader& operator>>(double& v) {
      static_assert(sizeof(double) == sizeof(uint64_t));
      static_assert(std::numeric_limits<double>::is_iec559);
      const uint64_t bits = detail::float_bits_from_sortable(read_be64());
      std::memcpy(&v, &bits, sizeof(double));
      return *this;
   }

   be_key_reader& operator>>(std::string& v) {
      char     tmp[be_key_stream::buf_cap];
      uint32_t n = 0;
      read_escaped([&](char c) {
         sysio::check(n < be_key_stream::buf_cap, msg::reader_too_large);
         tmp[n++] = c;
      });
      v.assign(tmp, n);
      return *this;
   }

   be_key_reader& operator>>(std::vector<char>& v) {
      v.clear();
      read_escaped([&](char c) { v.push_back(c); });
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
// table_impl — internal implementation; use table<> or scoped_table<> below.
// ---------------------------------------------------------------------------

template<name::raw TableName, typename K, typename V, bool Scoped, typename... Indices>
class table_impl {
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
   uint64_t code() const { return _code ? _code : kv::detail::receiving_account().value; }

   /// Stack bytes a read offers a value before falling back to the heap.
   static constexpr uint32_t value_stack_capacity = is_fixed_serializable_v<V> ? sizeof(V) : kv_value_stack_size;

   /// Read the value stored at an encoded key. Costs one kv_get, or two when the value overflows
   /// the stack buffer.
   /// @return false, leaving \p out untouched, when no row exists
   bool read_value(const char* key, uint32_t key_size, V& out) const {
      return detail::read_row<value_stack_capacity>(_table_id, code(), key, key_size,
                                                    [&](const char* data, uint32_t size) {
                                                       out = deserialize_value(data, size);
                                                    });
   }

   /// Write a value at an encoded key. One kv_set.
   /// @return the status kv_set returned, which the caller turns into its own message
   int32_t write_value(uint64_t payer, const char* key, uint32_t key_size, const V& value, kv_set_mode mode) const {
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &value, sizeof(V));
         return ::kv_set(_table_id, payer, key, key_size, vbuf, sizeof(V), mode);
      } else {
         auto v = serialize_value(value);
         return ::kv_set(_table_id, payer, key, key_size, v.data(), v.size(), mode);
      }
   }

protected:
   uint64_t _scope = 0;
   table_impl(sysio::name code, uint64_t scope) : _code(code.value), _scope(scope) {}

public:

   // --- Key / value encoding helpers ---

   be_key_stream make_key(const K& key) const {
      be_key_stream bs;
      if constexpr (Scoped) bs << _scope;
      bs << key;
      return bs;
   }

   /// Encode key without scope prefix — used for pri_key in secondary index storage.
   static be_key_stream make_unscoped_key(const K& key) {
      be_key_stream bs;
      bs << key;
      return bs;
   }

   K decode_key(const char* data, size_t size) const {
      be_key_reader rd(data, size);
      if constexpr (Scoped) { uint64_t dummy; rd >> dummy; }
      K key;
      rd >> key;
      return key;
   }

   /// Decode key from unscoped bytes (secondary index pri_key).
   static K decode_unscoped_key(const char* data, size_t size) {
      be_key_reader rd(data, size);
      K key;
      rd >> key;
      return key;
   }

   static ser_buf serialize_value(const V& value) { return kv::serialize_value(value); }
   static V deserialize_value(const char* data, size_t size) { return kv::deserialize_value<V>(data, size); }

   template<typename SecKey>
   be_key_stream encode_sec_key(const SecKey& key) const {
      be_key_stream bs;
      if constexpr (Scoped) bs << _scope;
      bs << key;
      return bs;
   }

   template<typename SecKey>
   SecKey decode_sec_key(const char* data, size_t size) const {
      be_key_reader rd(data, size);
      if constexpr (Scoped) { uint64_t dummy; rd >> dummy; }
      SecKey key;
      rd >> key;
      return key;
   }

   // --- Scoped iterator creation ---

   /// Open a primary handle at the first row at or after \p seek, bounded by this table's scope.
   int32_t create_primary_it(const char* seek = nullptr, uint32_t seek_size = 0) const {
      const auto prefix = make_prefix();
      return ::kv_it_create(_table_id, code(), prefix.data, prefix.size, seek, seek_size);
   }

   /// Build the full scoped key from unscoped pri_key bytes (for secondary index lookups).
   be_key_stream make_full_key_from_pri(const char* pri_data, uint32_t pri_size) const {
      be_key_stream bs;
      if constexpr (Scoped) bs << _scope;
      bs.write(pri_data, pri_size);
      return bs;
   }

   // --- Secondary index management ---
   //
   // One step per index, folded over the pack, so that each index's key streams are gone before the
   // next one starts. Nested calls would hold them all at once, and 16 indices would outgrow the
   // default stack.

   template<typename Index>
   static constexpr uint32_t sec_table_id = sysio::kv::compute_sec_table_id(
      static_cast<uint64_t>(TableName), Index::index_name);

   template<typename Index>
   void store_secondary(uint64_t payer, const be_key_stream& pri, const V& value) const {
      typename Index::secondary_extractor_type ext;
      auto sec = encode_sec_key(ext(value));
      detail::idx_store(sec_table_id<Index>, payer, pri.data(), pri.size(), sec.data(), sec.size());
   }

   template<typename Index>
   void remove_secondary(const be_key_stream& pri, const V& value) const {
      typename Index::secondary_extractor_type ext;
      auto sec = encode_sec_key(ext(value));
      detail::idx_erase(sec_table_id<Index>, pri.data(), pri.size(), sec.data(), sec.size());
   }

   template<typename Index>
   void update_secondary(uint64_t payer, const be_key_stream& pri, const V& old_val, const V& new_val) const {
      typename Index::secondary_extractor_type ext;
      auto old_sec = encode_sec_key(ext(old_val));
      auto new_sec = encode_sec_key(ext(new_val));
      bool same = (old_sec.size() == new_sec.size()) &&
                  (old_sec.size() == 0 || std::memcmp(old_sec.data(), new_sec.data(), old_sec.size()) == 0);
      if (!same) {
         detail::idx_update(sec_table_id<Index>, payer, pri.data(), pri.size(),
                            old_sec.data(), old_sec.size(), new_sec.data(), new_sec.size());
      }
   }

   /// Secondary ops use unscoped pri_key — saves 8B/row vs storing scoped key.
   void store_secondaries(uint64_t payer, const K& key, const V& value) {
      auto pri = make_unscoped_key(key);
      (store_secondary<Indices>(payer, pri, value), ...);
   }
   void remove_secondaries(const K& key, const V& value) {
      auto pri = make_unscoped_key(key);
      (remove_secondary<Indices>(pri, value), ...);
   }
   void update_secondaries(uint64_t payer, const K& key, const V& old_val, const V& new_val) {
      auto pri = make_unscoped_key(key);
      (update_secondary<Indices>(payer, pri, old_val, new_val), ...);
   }

   // store_secondaries, remove_secondaries, update_secondaries and do_erase are public but internal:
   // calling store_secondaries directly strands a (sec_key -> pri_key) mapping, because nothing ties
   // it to a primary row.

private:
   /// The scope as a key prefix. Empty on an unscoped table.
   struct key_prefix {
      char     data[kv_scope_size];
      uint32_t size = 0;
   };

   /// This table's scope as a key_prefix.
   key_prefix make_prefix() const {
      key_prefix prefix;
      if constexpr (Scoped) {
         encode_be64(prefix.data, _scope);
         prefix.size = kv_scope_size;
      }
      return prefix;
   }

   /// Open a secondary handle at the first entry at or after \p seek. The scope leads every secondary
   /// key of a scoped table, so passing it as the prefix bounds each step without the wrapper reading
   /// a key to check.
   int32_t create_index_it(uint32_t sec_table_id, const char* seek = nullptr, uint32_t seek_size = 0) const {
      const auto prefix = make_prefix();
      return ::kv_idx_create(sec_table_id, code(), prefix.data, prefix.size, seek, seek_size);
   }

   /// Replace a row's value and move its index entries.
   /// @param not_found_msg raised when the row is gone
   void update_row(uint64_t payer, const K& key, const V& old_value, const V& new_value,
                   const char* not_found_msg) {
      auto k = make_key(key);
      update_secondaries(payer, key, old_value, new_value);
      sysio::check(write_value(payer, k.data(), k.size(), new_value, kv_set_update) == kv_status_ok, not_found_msg);
   }

   /// Replace a row an iterator holds. A table with secondary indices moves their entries from the
   /// stored row, not from \p held: writing back a key an index no longer holds would otherwise look
   /// like no move at all, and leave the row and its index apart for good.
   ///
   /// The write goes to the receiver, whatever account the iterator read from. It misses when the row
   /// has gone, or when a handle on another account read a row the receiver never held, and
   /// msg::row_not_held has to be true of both.
   void modify_held_row(uint64_t payer, const K& key, const V& held, const V& new_value) {
      if constexpr (sizeof...(Indices) > 0) {
         auto k = make_key(key);
         V stored;
         sysio::check(read_value(k.data(), k.size(), stored), msg::row_not_modified);
         update_row(payer, key, stored, new_value, msg::row_not_held);
      } else {
         update_row(payer, key, held, new_value, msg::row_not_held);
      }
   }

public:
   // Internal erase used by both primary and secondary erase paths
   void do_erase(const K& key, const V& value) {
      remove_secondaries(key, value);
      auto pri = make_key(key);
      detail::erase_row(_table_id, pri.data(), pri.size());
   }

public:
   /// Construct a table. Reads/iterates against \p code's data (default: current contract).
   table_impl(sysio::name code = sysio::name{}) : _code(code.value) {}

   name get_code() const { return name(code()); }

   // --- Row type ---
   struct row {
      K key;
      V value;
   };

   // --- Primary const_iterator ---
   //
   // An iterator holds a copy of its row, so reading it never reaches the host. It takes a host
   // handle only once it has to step, because a handle costs a create, a seek and a destroy that a
   // point lookup never needs:
   //
   //   - find() and a copy start with no handle. The first ++ or -- opens one, by key.
   //   - erase() returns an iterator that still owes its step. Nothing reaches the host until that
   //     iterator is first used, so a caller that ignores it pays nothing.
   //
   // Being a copy, the row does not follow the table. modify() through an iterator updates that one
   // iterator. Any other iterator on the row keeps what it read, so find the row again to write
   // through it.
   //
   // The table must outlive its iterators, as with multi_index: opening a handle reads the table's
   // code and scope.
   struct const_iterator {
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type        = V;
      using difference_type   = std::ptrdiff_t;
      using pointer           = const V*;
      using reference         = const V&;

      const_iterator() = default;

      /// Dereference returns the value (like multi_index). Use key() for the key.
      const V& operator*()  const { sysio::check(valid(), "deref end iterator"); return _row.value; }
      const V* operator->() const { sysio::check(valid(), "deref end iterator"); return &_row.value; }

      /// Access the key at the current position.
      const K& key()  const { sysio::check(valid(), "key() on end iterator"); return _row.key; }

      const_iterator& operator++() {
         if (valid()) step_next();
         return *this;
      }

      const_iterator& operator--() {
         settle();
         if (_handle < 0) {
            if (_valid) {
               // Lands on this row, or on the one after it if this row is gone, and prev is the
               // row before this position either way.
               open_handle(_raw_key.data(), _raw_key.size());
            } else {
               detail::end_seek seek;
               open_handle(seek.data, seek.size);
            }
         }
         take_row<::kv_it_prev>();
         return *this;
      }

      // Post-increment/decrement deleted: returning a copy of the old position
      // would make every step in a loop re-open a host handle by key.
      // Use ++it / --it instead.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         a.settle();
         b.settle();
         return a._valid == b._valid && (!a._valid || a._raw_key == b._raw_key);
      }

      const_iterator(const_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid), _next_owed(o._next_owed),
           _row(std::move(o._row)), _raw_key(std::move(o._raw_key))
      { o._valid = false; o._next_owed = false; }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid; _next_owed = o._next_owed;
            _row = std::move(o._row); _raw_key = std::move(o._raw_key);
            o._valid = false; o._next_owed = false;
         }
         return *this;
      }

      /// Copy: the same position with no handle of its own, so copying makes no host call. The
      /// copy opens a handle by key on its first ++ or --.
      const_iterator(const const_iterator& o)
         : _tbl(o._tbl), _valid(o._valid), _next_owed(o._next_owed), _row(o._row), _raw_key(o._raw_key) {}

      const_iterator& operator=(const const_iterator& o) {
         if (this != &o) {
            _tbl = o._tbl; _handle.reset(); _valid = o._valid; _next_owed = o._next_owed;
            _row = o._row; _raw_key = o._raw_key;
         }
         return *this;
      }

   private:
      friend class table_impl;
      friend class detail::reverse_cursor<const_iterator>;
      const table_impl* _tbl = nullptr;
      // Mutable because a const read settles an owed step, the one thing a read can trigger.
      mutable kv::detail::it_handle _handle;
      mutable bool         _valid = false;
      mutable bool         _next_owed = false;   ///< erase() deferred the step to the next row
      mutable row          _row;
      mutable key_buf      _raw_key;

      /// Take over a handle and read the row it was opened on.
      const_iterator(const table_impl* t, int32_t h) : _tbl(t), _handle(h) { take_row<read_here>(); }

      static const_iterator make_end(const table_impl* t) {
         const_iterator it; it._tbl = t; return it;
      }

      /// Take the step erase() deferred, if there is one.
      void settle() const {
         if (!_next_owed) return;
         _next_owed = false;
         step_next();
      }

      /// True when positioned on a row, after settling.
      bool valid() const {
         settle();
         return _valid;
      }

      /// reverse_cursor's test for a row here: false at end.
      bool positioned() const { return valid(); }

      /// reverse_cursor's move to the previous row, landing on end past the first one.
      void step_back() { --*this; }

      /// reverse_cursor's move to the first row, or to end on an empty table. Aborts on a
      /// default-constructed iterator.
      void to_first() {
         sysio::check(_tbl != nullptr, msg::no_table);
         *this = _tbl->begin();
      }

      /// Move to the next row. By key when there is no handle yet: seeking the key itself and
      /// stepping would skip a row after an erase, and would land back on a row erased and written
      /// again at the same key.
      void step_next() const {
         if (_handle < 0) {
            detail::successor_key seek(_raw_key.data(), _raw_key.size());
            open_handle(seek.data(), seek.size());
            take_row<read_here>();
         } else {
            take_row<::kv_it_next>();
         }
      }

      /// Open a handle at the first row at or after a seek key.
      void open_handle(const char* seek, uint32_t seek_size) const {
         sysio::check(_tbl != nullptr, msg::no_table);
         _handle.reset(_tbl->create_primary_it(seek, seek_size));
      }

      /// kv_it_read in the shape of the moves, for take_row.
      static int32_t read_here(int32_t handle, void* key, uint32_t key_capacity, void* value, uint32_t value_capacity,
                               uint32_t* key_size, uint32_t* value_size) {
         return ::kv_it_read(handle, 0, key, key_capacity, value, value_capacity, key_size, value_size);
      }

      /// Run one host call that lands the handle on a row, and hold that row. The call fills stack
      /// buffers, so a scan is one host call per row. A row larger than those buffers is read
      /// again at its full size.
      template<auto HostCall>
      void take_row() const {
         char     key_stack[key_buf::inline_cap];
         char     value_stack[value_stack_capacity];
         uint32_t key_size = 0;
         uint32_t value_size = 0;
         _valid = HostCall(_handle, key_stack, key_buf::inline_cap, value_stack, value_stack_capacity,
                           &key_size, &value_size) == kv_status_ok;
         if (!_valid) return;
         if (key_size <= key_buf::inline_cap && value_size <= value_stack_capacity) {
            hold_row(key_stack, key_size, value_stack, value_size);
         } else {
            ser_buf key_heap(key_size);
            ser_buf value_heap(value_size);
            sysio::check(read_here(_handle, key_heap.data(), key_size, value_heap.data(), value_size,
                                   &key_size, &value_size) == kv_status_ok, msg::row_lost);
            hold_row(key_heap.data(), key_size, value_heap.data(), value_size);
         }
      }

      void hold_row(const char* key, uint32_t key_size, const char* value, uint32_t value_size) const {
         _raw_key.assign(key, key_size);
         _row.key = _tbl->decode_key(key, key_size);
         _row.value = deserialize_value(value, value_size);
      }
   };

   // --- Primary key operations ---

   /// Two host calls: kv_it_create, then kv_it_read.
   const_iterator begin() const { return const_iterator(this, create_primary_it()); }

   const_iterator end() const { return const_iterator::make_end(this); }

   /// Find the row at \p key. One kv_get, or two when the value overflows the stack buffer. The
   /// iterator holds the row and opens no host handle until its first ++ or --.
   const_iterator find(const K& key) const {
      auto k = make_key(key);
      auto it = end();
      if (read_value(k.data(), k.size(), it._row.value)) {
         it._row.key = key;
         it._raw_key.assign(k.data(), k.size());
         it._valid = true;
      }
      return it;
   }

   const_iterator require_find(const K& key, const char* msg = "unable to find key") const {
      auto it = find(key);
      sysio::check(it != end(), msg);
      return it;
   }

   /// Returns the value for key, or nullopt if not found.
   std::optional<V> try_get(const K& key) const {
      auto k = make_key(key);
      V value;
      if (!read_value(k.data(), k.size(), value)) return {};
      return value;
   }

   /// Returns the value for key. Asserts if not found.
   V get(const K& key, const char* msg = "key not found") const {
      auto val = try_get(key);
      sysio::check(val.has_value(), msg);
      return *std::move(val);
   }

   /// One kv_get with an empty buffer, which reads nothing and reports whether the row exists.
   bool contains(const K& key) const {
      auto k = make_key(key);
      return ::kv_get(_table_id, code(), k.data(), k.size(), 0, nullptr, 0) >= 0;
   }

   /// Two host calls: kv_it_create at the key, then kv_it_read.
   const_iterator lower_bound(const K& key) const {
      auto k = make_key(key);
      return const_iterator(this, create_primary_it(k.data(), k.size()));
   }

   /// Two host calls, like lower_bound.
   const_iterator upper_bound(const K& key) const {
      auto k = make_key(key);
      detail::successor_key seek(k.data(), k.size());
      return const_iterator(this, create_primary_it(seek.data(), seek.size()));
   }

   // --- Mutation ---
   // Every row-creating operation (emplace/set/upsert) requires an explicit payer: the host rejects
   // payer 0 (same_payer) on insert, since a new row has no existing payer to keep. same_payer stays
   // valid for modify (update), where it keeps the row's current payer.

   /// Insert a new row, in one kv_set when the table has no secondary index. Asserts \p exists_msg
   /// if the key already exists. Use upsert()/set() for insert-or-update semantics.
   ///
   /// WRITES IGNORE code(), as they do in kv::global. kv_get takes a code argument, so reads honour
   /// whatever account this handle was constructed with; kv_set, kv_erase and kv_idx_store have no
   /// such parameter and always land on the current receiver. A handle opened on a FOREIGN account
   /// is therefore read-only in practice -- because table_id is derived from the table name alone,
   /// mutating through one lands on your row of the same name. Nothing detects it at compile time.
   /// Construct foreign-code handles for reading only.
   ///
   /// (sysio::multi_index does guard this, because upstream does and ported contracts rely
   /// on the abort; these wrappers have no such compatibility obligation.)
   void emplace(name payer, const K& key, const V& value, const char* exists_msg = "key already exists") {
      auto k = make_key(key);
      sysio::check(write_value(payer.value, k.data(), k.size(), value, kv_set_insert) == kv_status_ok, exists_msg);
      store_secondaries(payer.value, key, value);
   }

   /// Lambda emplace: construct the value in-place.
   template<typename Lambda, typename = std::enable_if_t<std::is_invocable_v<Lambda, V&>>>
   void emplace(name payer, const K& key, Lambda&& constructor, const char* exists_msg = "key already exists") {
      V value{};
      constructor(value);
      emplace(payer, key, value, exists_msg);
   }

   /// Upsert convenience alias.
   void set(name payer, const K& key, const V& value) { upsert(payer, key, value); }

   /// Insert or update a row. One kv_set when the table has no secondary index. With one, the old
   /// value is read first so its entries can be stored or moved, which adds one kv_get.
   void upsert(name payer, const K& key, const V& value) {
      auto k = make_key(key);
      if constexpr (sizeof...(Indices) > 0) {
         V old_value;
         if (read_value(k.data(), k.size(), old_value)) update_secondaries(payer.value, key, old_value, value);
         else store_secondaries(payer.value, key, value);
      }
      sysio::check(write_value(payer.value, k.data(), k.size(), value, kv_set_upsert) == kv_status_ok,
                   msg::write_refused);
   }

   /// Insert with a default value, or update with a lambda.
   /// If the key does not exist, inserts \p default_value.
   /// If the key exists, reads the old value, applies \p updater, and writes back.
   /// Cost: 1 kv_get + 1 kv_set = 2 intrinsic calls (optimal for insert-or-modify).
   ///
   /// Example:
   /// \code
   ///   tbl.upsert(payer, key, account{initial_deposit},
   ///      [&](auto& a) { a.balance += deposit; });
   /// \endcode
   template<typename Lambda, typename = std::enable_if_t<std::is_invocable_v<Lambda, V&>>>
   void upsert(name payer, const K& key, const V& default_value, Lambda&& updater) {
      auto k = make_key(key);
      V old_value;
      V new_value;
      if (read_value(k.data(), k.size(), old_value)) {
         new_value = old_value;
         updater(new_value);
         update_secondaries(payer.value, key, old_value, new_value);
      } else {
         new_value = default_value;
         store_secondaries(payer.value, key, new_value);
      }
      sysio::check(write_value(payer.value, k.data(), k.size(), new_value, kv_set_upsert) == kv_status_ok,
                   msg::write_refused);
   }

   /// Replace the row at \p it. On a table with secondary indices this reads the row first, for one
   /// more kv_get, and moves its index entries from what is stored, because \p it holds a copy that
   /// another iterator's write can have left behind. \p it reads the new value afterwards.
   void modify(name payer, const const_iterator& it, const V& new_value) {
      sysio::check(it.valid(), msg::modify_end);
      modify_held_row(payer.value, it._row.key, it._row.value, new_value);
      it._row.value = new_value;
   }

   void modify(const const_iterator& it, const V& new_value) {
      modify(name{}, it, new_value);
   }

   /// Modify by key + lambda (like multi_index).
   template<typename Lambda>
   void modify(name payer, const K& key, Lambda&& updater, const char* not_found_msg = "key not found") {
      V old_val = get(key, not_found_msg);
      V new_val = old_val;
      updater(new_val);
      update_row(payer.value, key, old_val, new_val, not_found_msg);
   }

   template<typename Lambda>
   void modify(const K& key, Lambda&& updater) {
      modify(name{}, key, std::forward<Lambda>(updater));
   }

private:
   /// Erase the row at \p it and leave it owing its step to the next row.
   const_iterator erase_and_owe(const_iterator&& it) {
      sysio::check(it.valid(), msg::erase_end);
      do_erase(it._row.key, it._row.value);
      it._next_owed = true;
      return std::move(it);
   }

public:
   /// Erase the row at \p it and return an iterator to the row after it.
   ///
   /// The returned iterator owes its step, so ignoring it makes no further host call. It takes the
   /// step when first used: it lands on the first row past the erased key at that moment, so a row
   /// written between this call and that use is seen.
   ///
   /// The result takes over \p it's host handle, so `it = tbl.erase(it)` and
   /// `it = tbl.erase(std::move(it))` make the same host calls, though the lvalue form also copies
   /// the key and value. An lvalue stays on the erased row, holding that row's copy and no handle:
   /// stepping it opens one by key. If it still owed a step from an earlier erase it keeps owing
   /// that one, and takes it by key at first use.
   const_iterator erase(const_iterator& it) {
      const_iterator next(std::as_const(it));
      next._handle = std::move(it._handle);
      return erase_and_owe(std::move(next));
   }

   const_iterator erase(const_iterator&& it) { return erase_and_owe(std::move(it)); }
   const_iterator erase(const const_iterator& it) { return erase_and_owe(const_iterator(it)); }

   /// Erase by key. Asserts \p not_found_msg if the key is not found. One kv_erase when the table
   /// has no secondary index. With one, the row is read first to find its entries.
   ///
   /// On a handle built for a foreign account, as with every write here, the row erased is the
   /// receiver's. A table with no index never looks at that account, so \p not_found_msg then reports
   /// the receiver's row. With an index, the read goes to the foreign account first.
   void erase(const K& key, const char* not_found_msg = "key not found") {
      if constexpr (sizeof...(Indices) == 0) {
         auto k = make_key(key);
         sysio::check(::kv_erase(_table_id, k.data(), k.size()) == kv_status_ok, not_found_msg);
      } else {
         V val = get(key, not_found_msg);
         do_erase(key, val);
      }
   }

   // --- Iterator aliases ---

   const_iterator cbegin() const { return begin(); }
   const_iterator cend() const { return end(); }

   using const_reverse_iterator = detail::reverse_cursor<const_iterator>;

   /// Two host calls: kv_it_create past the last row, then kv_it_prev. Each step after that is one.
   const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }

   /// No host call.
   const_reverse_iterator rend() const { return const_reverse_iterator::rend(end()); }
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
      --it;
      if (it == end()) return 0;
      auto pk = it.key().primary_key();
      sysio::check(pk < std::numeric_limits<uint64_t>::max(), msg::pk_limit);
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

      table_impl* _tbl;
      secondary_index_view(table_impl& tbl) : _tbl(&tbl) {}

      // --- key_row for key-only iteration ---
      struct key_row {
         K              key;
         secondary_key_type sec_key;
      };

      // --- Secondary const_iterator ---
      struct const_iterator {
         using iterator_category = std::bidirectional_iterator_tag;
         using value_type        = V;
         using difference_type   = std::ptrdiff_t;
         using pointer           = const V*;
         using reference         = const V&;

         const_iterator() = default;

         const V& operator*()  const { sysio::check(valid(), "deref invalid sec iter"); return _row.value; }
         const V* operator->() const { sysio::check(valid(), "deref invalid sec iter"); return &_row.value; }
         const K& key()  const { sysio::check(valid(), "key() on invalid sec iter"); return _row.key; }

         const_iterator& operator++() {
            if (valid()) step_next();
            return *this;
         }

         const_iterator& operator--() {
            settle();
            if (_handle < 0) {
               sysio::check(_tbl != nullptr, msg::no_table);
               detail::end_seek seek;
               _handle.reset(_tbl->create_index_it(_sec_table_id, seek.data, seek.size));
            }
            take_entry<::kv_idx_prev>();
            return *this;
         }

         // Post-increment/decrement deleted: KV iterator handle is a unique resource.
         const_iterator operator++(int) = delete;
         const_iterator operator--(int) = delete;

         friend bool operator==(const const_iterator& a, const const_iterator& b) {
            a.settle();
            b.settle();
            if (!a._valid && !b._valid) return true;
            if (!a._valid || !b._valid) return false;
            return a._pri_bytes == b._pri_bytes;
         }

         const_iterator(const_iterator&& o) noexcept
            : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid), _next_owed(o._next_owed),
              _row(std::move(o._row)), _pri_bytes(std::move(o._pri_bytes))
         { o._valid = false; o._next_owed = false; }
         const_iterator& operator=(const_iterator&& o) noexcept {
            if (this != &o) {
               _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid; _next_owed = o._next_owed;
               _row = std::move(o._row); _pri_bytes = std::move(o._pri_bytes);
               o._valid = false; o._next_owed = false;
            }
            return *this;
         }
         const_iterator(const const_iterator&) = delete;
         const_iterator& operator=(const const_iterator&) = delete;

      private:
         friend struct secondary_index_view;

         table_impl* _tbl = nullptr;
         kv::detail::idx_handle _handle;
         // Mutable because a const read settles an owed step, the one thing a read can trigger.
         mutable bool    _valid = false;
         mutable bool    _next_owed = false;   ///< erase() deferred the step to the next entry
         mutable row     _row;
         mutable key_buf _pri_bytes;

         /// Take over a handle and read the entry it was opened on.
         const_iterator(table_impl* tbl, int32_t handle) : _tbl(tbl), _handle(handle) {
            take_entry<::kv_idx_read>();
         }

         static const_iterator make_end(table_impl* tbl) {
            const_iterator it; it._tbl = tbl; return it;
         }

         /// Take the step erase() deferred, if there is one.
         void settle() const {
            if (!_next_owed) return;
            _next_owed = false;
            step_next();
         }

         /// True when positioned on an entry, after settling.
         bool valid() const {
            settle();
            return _valid;
         }

         void step_next() const { take_entry<::kv_idx_next>(); }

         /// Run one host call that lands the handle on an entry, then read its row: two host calls
         /// per entry. The secondary key is skipped, since nothing here needs it.
         template<auto HostCall>
         void take_entry() const {
            char     pri_stack[key_buf::inline_cap];
            uint32_t sec_size = 0;
            uint32_t pri_size = 0;
            _valid = HostCall(_handle, nullptr, 0, pri_stack, key_buf::inline_cap,
                              &sec_size, &pri_size) == kv_status_ok;
            if (!_valid) return;
            if (pri_size <= key_buf::inline_cap) {
               _pri_bytes.assign(pri_stack, pri_size);
            } else {
               ser_buf pri_heap(pri_size);
               sysio::check(::kv_idx_read(_handle, nullptr, 0, pri_heap.data(), pri_size,
                                          &sec_size, &pri_size) == kv_status_ok, msg::row_lost);
               _pri_bytes.assign(pri_heap.data(), pri_size);
            }
            // pri_bytes is unscoped [K] -- decode directly
            _row.key = decode_unscoped_key(_pri_bytes.data(), _pri_bytes.size());

            // Reconstruct full scoped key for kv_get
            auto full = _tbl->make_full_key_from_pri(_pri_bytes.data(), _pri_bytes.size());
            _valid = _tbl->read_value(full.data(), full.size(), _row.value);
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
            if (_valid) take_keys<::kv_idx_next>();
            return *this;
         }

         key_iterator& operator--() {
            if (_handle < 0) {
               sysio::check(_tbl != nullptr, msg::no_table);
               detail::end_seek seek;
               _handle.reset(_tbl->create_index_it(_sec_table_id, seek.data, seek.size));
            }
            take_keys<::kv_idx_prev>();
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

         table_impl* _tbl = nullptr;
         kv::detail::idx_handle _handle;
         bool           _valid = false;
         key_row        _kr;
         key_buf        _pri_bytes;

         /// Take over a handle and read the entry it was opened on.
         key_iterator(table_impl* tbl, int32_t handle) : _tbl(tbl), _handle(handle) {
            take_keys<::kv_idx_read>();
         }

         static key_iterator make_end(table_impl* tbl) {
            key_iterator it; it._tbl = tbl; return it;
         }

         /// Run one host call that lands the handle on an entry and hold both of its keys: one host
         /// call per entry, read again at full size if a key outgrows the stack buffers.
         template<auto HostCall>
         void take_keys() {
            char     sec_stack[key_buf::inline_cap];
            char     pri_stack[key_buf::inline_cap];
            uint32_t sec_size = 0;
            uint32_t pri_size = 0;
            _valid = HostCall(_handle, sec_stack, key_buf::inline_cap, pri_stack, key_buf::inline_cap,
                              &sec_size, &pri_size) == kv_status_ok;
            if (!_valid) return;
            if (sec_size <= key_buf::inline_cap && pri_size <= key_buf::inline_cap) {
               hold_keys(sec_stack, sec_size, pri_stack, pri_size);
            } else {
               ser_buf sec_heap(sec_size);
               ser_buf pri_heap(pri_size);
               sysio::check(::kv_idx_read(_handle, sec_heap.data(), sec_size, pri_heap.data(), pri_size,
                                          &sec_size, &pri_size) == kv_status_ok, msg::row_lost);
               hold_keys(sec_heap.data(), sec_size, pri_heap.data(), pri_size);
            }
         }

         void hold_keys(const char* sec, uint32_t sec_size, const char* pri, uint32_t pri_size) {
            _pri_bytes.assign(pri, pri_size);
            // pri_bytes is unscoped [K] -- decode directly
            _kr.key = decode_unscoped_key(pri, pri_size);
            _kr.sec_key = _tbl->template decode_sec_key<secondary_key_type>(sec, sec_size);
         }
      };

      // --- Secondary index view API ---

      const_iterator begin() const {
         return const_iterator(_tbl, _tbl->create_index_it(_sec_table_id));
      }

      const_iterator end() const { return const_iterator::make_end(_tbl); }
      const_iterator cbegin() const { return begin(); }
      const_iterator cend() const { return end(); }

      template<typename SecKey>
      const_iterator find(const SecKey& sec_key) const {
         auto sec = _tbl->encode_sec_key(secondary_key_type(sec_key));
         const auto prefix = _tbl->make_prefix();
         int32_t handle = ::kv_idx_find(_sec_table_id, _tbl->code(), prefix.data, prefix.size,
                                        sec.data(), sec.size());
         if (handle < 0) return end();
         return const_iterator(_tbl, handle);
      }

      template<typename SecKey>
      const_iterator lower_bound(const SecKey& sec_key) const {
         auto sec = _tbl->encode_sec_key(secondary_key_type(sec_key));
         return const_iterator(_tbl, _tbl->create_index_it(_sec_table_id, sec.data(), sec.size()));
      }

      template<typename SecKey>
      const_iterator upper_bound(const SecKey& sec_key) const {
         auto sec = _tbl->encode_sec_key(secondary_key_type(sec_key));
         // Past every entry with this secondary key, whatever its primary key: NUL-escaped strings
         // use 0x00 as their sentinel, so the seek cannot collide with a valid encoding.
         detail::successor_key seek(sec.data(), sec.size());
         return const_iterator(_tbl, _tbl->create_index_it(_sec_table_id, seek.data(), seek.size()));
      }

      template<typename SecKey>
      const_iterator require_find(const SecKey& sec_key,
                                  const char* msg = "unable to find secondary key") const {
         auto itr = find(sec_key);
         sysio::check(itr != end(), msg);
         return itr;
      }

      /// Modify the primary row referenced by a secondary iterator. It reads the row first, for one
      /// more kv_get, and moves the index entries from what is stored.
      /// The iterator reads the new value afterwards, so a further modify or erase through it is
      /// safe. Its position stays old: if the secondary key changed, it still steps from the old key,
      /// so find the row again to step from the new one.
      void modify(name payer, const const_iterator& itr, const V& new_value) {
         sysio::check(itr.valid(), msg::modify_end);
         _tbl->modify_held_row(payer.value, itr._row.key, itr._row.value, new_value);
         itr._row.value = new_value;
      }

      void modify(const const_iterator& itr, const V& new_value) {
         modify(name{}, itr, new_value);
      }

      /// Erase the primary row referenced by a secondary iterator and return an iterator to the
      /// next entry. As with table_impl::erase, the step is deferred to the result's first use, so
      /// ignoring the result makes no further host call.
      const_iterator erase(const_iterator itr) {
         sysio::check(itr.valid(), msg::erase_end);
         _tbl->do_erase(itr._row.key, itr._row.value);
         itr._next_owed = true;
         return itr;
      }

      // Key-only iteration
      key_iterator key_begin() const {
         return key_iterator(_tbl, _tbl->create_index_it(_sec_table_id));
      }

      key_iterator key_end() const { return key_iterator::make_end(_tbl); }

      name get_code() const { return _tbl->get_code(); }
   };

   template<name::raw IndexName>
   secondary_index_view<IndexName> get_index() {
      return secondary_index_view<IndexName>(*this);
   }

};

// ---------------------------------------------------------------------------
// table — public unscoped wrapper (Scoped=false).
// ---------------------------------------------------------------------------

template<name::raw TableName, typename K, typename V, typename... Indices>
class table : public table_impl<TableName, K, V, false, Indices...> {
   using base = table_impl<TableName, K, V, false, Indices...>;
public:
   table(sysio::name code = sysio::name{}) : base(code) {}
};

} } // namespace sysio::kv
