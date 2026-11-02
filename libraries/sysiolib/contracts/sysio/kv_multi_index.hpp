#pragma once
/**
 * KV-backed multi_index emulation layer.
 *
 * A shim for sysio::multi_index that uses KV intrinsics instead of the legacy db_*_i64
 * ones. The template API is the same in almost every respect; the places it is not are
 * listed above the class.
 *
 * Key encoding: [scope: 8B BE][primary_key: 8B BE] = 16 bytes.
 * Table name is encoded in table_id (DJB2 hash of raw template parameter),
 * which provides namespace isolation without per-row key overhead.
 *
 * The payer parameter is honored — RAM is charged to the specified payer,
 * matching the behavior of the legacy sysio::multi_index.
 */

#include <cstdint>
#include <sysio/kv_utils.hpp>   // the KV host functions, through <sysio/kv.h>

#include <sysio/name.hpp>
#include <sysio/fixed_bytes.hpp>       // checksum256, one of the five secondary key types
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/check.hpp>
#include <sysio/action.hpp>
#include <sysio/context.hpp>

#include <vector>
#include <memory>
#include <map>
#include <cstring>
#include <type_traits>
#include <utility>
#include <limits>
#include <iterator>

namespace sysio {

// Type definitions needed by contracts using secondary indices.
// These are pure type templates with no legacy db_* dependencies.
template<name::raw IndexName, typename Extractor>
struct indexed_by {
   enum constants : uint64_t { index_name = static_cast<uint64_t>(IndexName) };
   typedef Extractor secondary_extractor_type;
};

template<class Class, typename Type, Type (Class::*PtrToMemberFunction)()const>
struct const_mem_fun {
   typedef typename std::remove_reference<Type>::type result_type;
   Type operator()(const Class& x) const { return (x.*PtrToMemberFunction)(); }
   Type operator()(const Class* x) const { return (x->*PtrToMemberFunction)(); }
};

namespace _kv_multi_index_detail {

   // Encoded sizes for big-endian KV key components.
   static constexpr size_t u64_size   = sizeof(uint64_t);
   static constexpr size_t u128_size  = sizeof(uint128_t);

   // Fixed-size buffer with vector-compatible .data()/.size() interface.
   // Avoids heap allocation for small known-size secondary key encodings.
   template<size_t N>
   struct fixed_buf {
      char data_[N] = {};
      const char* data() const { return data_; }
      char* data() { return data_; }
      constexpr size_t size() const { return N; }
      friend bool operator==(const fixed_buf& a, const fixed_buf& b) { return memcmp(a.data_, b.data_, N) == 0; }
      friend bool operator!=(const fixed_buf& a, const fixed_buf& b) { return !(a == b); }
   };

   // The closed set of secondary key types.
   //
   // multi_index is the backward-compatibility shim: it is here so a contract written for
   // an Antelope chain builds unchanged. Upstream's secondary index backend is five
   // db_idx* intrinsic families and no more, so a port cannot arrive with a key outside
   // this set -- it would not have compiled where it came from. Each of the five has an
   // order-preserving encoder below, which is what the index needs: kv_idx_lower_bound
   // compares the encoded key with memcmp, so the encoding, not the C++ type, decides the
   // order. A sixth type would reach a generic pack(), which writes integers in native
   // little-endian -- under which 0x00000100 sorts below 0x00000001 -- and iterate wrong
   // while find() still matched. It is refused instead.
   //
   // A contract written FOR Wire should use kv::table, whose kv::index keys through
   // sysio::kv::be_key_stream and accepts any type that encoder can spell: the narrow
   // integers, name, and composite structs -- not enums, which none of its overloads
   // accept. The chain agrees with that encoder
   // from the other side -- get_table_rows builds its query bound with
   // sysio::chain::be_key_codec, which is big-endian for every leaf kind it knows.
   template<typename T>
   inline constexpr bool is_supported_secondary_key =
      std::is_same<T, uint64_t>::value    || std::is_same<T, uint128_t>::value ||
      std::is_same<T, double>::value      || std::is_same<T, long double>::value ||
      std::is_same<T, fixed_bytes<32>>::value;

// A macro, not a constexpr char*, because static_assert takes a string literal. Both the
// encoder and secondary_index_view assert the same condition and must say the same thing.
#define SYSIO_MI_SECONDARY_KEY_ERROR                                                        \
   "multi_index secondary keys are limited to the five types upstream supports: uint64_t, " \
   "uint128_t, double, long double, checksum256. Any other type has no order-preserving "   \
   "encoding here and would iterate in its byte order rather than its value order. For a "  \
   "wider key -- a narrow integer, a name, or a composite struct -- use kv::table "\
   "with kv::index, which encodes through be_key_stream."

   // Deliberately viable for every T rather than SFINAE'd to the five: constraining it
   // would leave an unsupported key to fail against the overloads below, which reports an
   // ambiguous call or no matching function from inside store_all. Keeping the candidate
   // reachable puts the explanation here instead. secondary_index_view asserts the same
   // thing, and fires earlier -- at get_index<>() -- for a table that is queried too.
   template<typename T>
   inline fixed_buf<1> encode_secondary(const T&) {
      static_assert(is_supported_secondary_key<T>, SYSIO_MI_SECONDARY_KEY_ERROR);
      return {};
   }

   // Non-template overloads, preferred over the template above.

   inline fixed_buf<u64_size> encode_secondary(const uint64_t& key) {
      fixed_buf<u64_size> buf;
      kv::encode_be64(buf.data(), key);
      return buf;
   }

   inline fixed_buf<u128_size> encode_secondary(const uint128_t& key) {
      fixed_buf<u128_size> buf;
      kv::encode_be64(buf.data(), static_cast<uint64_t>(key >> 64));
      kv::encode_be64(buf.data() + u64_size, static_cast<uint64_t>(key));
      return buf;
   }

   inline fixed_buf<u64_size> encode_secondary(const double& key) {
      uint64_t bits;
      memcpy(&bits, &key, u64_size);
      fixed_buf<u64_size> buf;
      kv::encode_be64(buf.data(), kv::detail::sortable_float_bits(bits));
      return buf;
   }

   // checksum256. Byte-for-byte what the previous pack() path produced: fixed_bytes
   // serialises through extract_as_byte_array(), which emits each word big-endian in
   // _data order and so agrees with fixed_bytes::operator<. Spelled out here because the
   // generic above no longer encodes anything.
   inline fixed_buf<32> encode_secondary(const fixed_bytes<32>& key) {
      const auto arr = key.extract_as_byte_array();
      fixed_buf<32> buf;
      memcpy(buf.data(), arr.data(), arr.size());
      return buf;
   }

   // Sized off long double itself, not off u128_size. They are the same 16 bytes on
   // wasm32, which is what contracts compile for, so no stored key moves -- but this
   // header also compiles natively for the test harness, and macOS on ARM64 makes
   // long double an 8-byte double. Reading u128_size bytes out of it is an
   // out-of-bounds read, and the encoding would be twice sizeof, which breaks the
   // width invariant the reverse-iteration sentinel depends on.
   //
   // The byte reversal only yields a MEANINGFUL order where long double is IEEE
   // binary128. Elsewhere it is merely well-defined; see kv_secondary_key_tests.
   inline fixed_buf<sizeof(long double)> encode_secondary(const long double& key) {
      constexpr size_t n = sizeof(long double);
      char raw[n];
      memcpy(raw, &key, n);
      fixed_buf<n> buf;
      for (size_t i = 0; i < n; ++i)
         buf.data_[i] = raw[n - 1 - i];
      if (static_cast<uint8_t>(buf.data_[0]) & 0x80u)
         for (size_t i = 0; i < n; ++i) buf.data_[i] = ~buf.data_[i];
      else
         buf.data_[0] = static_cast<char>(static_cast<uint8_t>(buf.data_[0]) ^ 0x80u);
      return buf;
   }

   /// A row as one table handle holds it. Iterators and references point here, one object per
   /// primary key, and a modify updates it in place. An erase marks it rather than freeing it, so an
   /// iterator left on the row aborts with kv::msg::iterator_erased instead of reading freed memory.
   /// The object lasts as long as the handle, and a later write at the same primary key reuses it.
   ///
   /// Declared here, on the row type alone, and not inside kv_multi_index. Nested there it would be a
   /// new type per table and index set, and each would instantiate the row cache's std::map again.
   template<typename T>
   struct cached_row {
      T    row;
      bool erased = false;

      explicit cached_row(T&& value) : row(std::move(value)) {}
   };

} // namespace _kv_multi_index_detail

// Uses sysio::indexed_by and sysio::const_mem_fun from the standard CDT headers.
//
// A shim for the EOSIO multi_index over a different store. Nearly all contract code carries
// over. What does not comes in three kinds, and they are worth keeping apart.
//
// SOURCE BREAKS AGAINST UPSTREAM -- code that compiles there and not here:
//
//   - the postfix iterator operators are deleted, because copying a KV iterator duplicates a
//     host-side handle. The reverse iterators keep theirs, so upstream's reverse loops still
//     compile, and each `rit++` pays for that copy;
//   - the primary bounds are uint64_t/name overloads where upstream has a member template, so
//     an explicit call -- `t.template lower_bound<uint64_t>(k)`, likewise upper_bound -- is
//     rejected with "does not refer to a template". A wrapper convertible to BOTH uint64_t and
//     name is also ambiguous here (see the note at the bounds);
//   - const_reverse_iterator is its own class rather than a std::reverse_iterator, so
//     `std::reverse_iterator<table::const_iterator> r = t.rbegin();` is rejected with "no viable
//     conversion", and iterator_type is gone. Declare it auto or table::const_reverse_iterator.
//     std::make_reverse_iterator(it) still compiles and compares against rend().
//
// RUNTIME DIVERGENCES FROM UPSTREAM -- code that compiles in both and behaves differently:
//
//   - a reverse iterator holds the row it reads, where std::reverse_iterator holds the row past it
//     and works the row out on each read, so it does not follow the table after a write.
//     `while (rit != t.rend()) t.erase(*rit);` never moves rit, and aborts on its second pass with
//     kv::msg::iterator_erased. So does reading through rit after an erase, as in
//     `t.erase(*rit); rit->id`, and copying it, which base() and rit++ do. A row emplaced past the
//     position is not seen. `t.erase(*rit); ++rit;` reaches the row before, which upstream skips. To
//     carry on from an erase, rebuild the iterator: `rit = const_reverse_iterator(t.erase(--rit.base()));`
//   - after a modify moves a secondary key, an iterator on that entry reads the new value, but its
//     ++ and -- start from the old key, because the host keeps a handle on the key it was on.
//     Upstream's iterator moves with the entry. A copy, or a new find, starts from the new key.
//
// RESTRICTIONS SHARED WITH UPSTREAM, which are not breaks even though they bite:
//
//   - taking the bare address of a primary bound, `&table::lower_bound`, does not compile --
//     here because the name is an overload set, upstream because a member template's parameter
//     cannot be deduced. A named static_cast resolves one on Wire;
//   - a secondary key must be trivially copyable. Upstream's supported secondary types are all
//     trivially copyable too; what differs is only where it is diagnosed. The static_assert
//     lives in secondary_index_view, so it fires at get_index<...>(), not at declaration.
//
// The mutators reject a duplicate primary key, a handle whose code is not the receiving account
// and an object that is not a row of this handle, matching upstream. Each guard is documented
// where it stands.
//
// sysio::multi_index is a direct alias of this template. sysio::singleton is not: it aliases
// kv_singleton, which holds a kv_multi_index as a PRIVATE member. Its surface is single-row
// accessors and mutators -- not the table's iterators, bounds or secondary-index API -- so it
// is bound by the mutators' guards, and a singleton handle on another account is read-only,
// but none of the divergences above are reachable through it.

template<name::raw TableName, typename T, typename... Indices>
class kv_multi_index {
   static_assert(sizeof...(Indices) <= 16, "multi_index supports at most 16 secondary indices");
   static_assert(std::is_default_constructible_v<T>,
                 "kv_multi_index row type must be default-constructible; add a default ctor to T");

   /// table_id computed at compile time from the template parameter.
   static constexpr uint32_t _table_id = sysio::kv::compute_table_id(static_cast<uint64_t>(TableName));

   // Helper: convert primary_key() result to uint64_t regardless of return type (uint64_t or name)
   static uint64_t to_pk_uint64(uint64_t pk) { return pk; }
   static uint64_t to_pk_uint64(name pk) { return pk.value; }

   name     _code;
   uint64_t _scope;
   mutable uint64_t _next_primary_key = 0;
   mutable bool     _next_primary_key_cached = false;

   /// The first primary key available_primary_key() refuses. Upstream reserves it and the one above it
   /// as tags, so it never hands either out, and a port's keys keep the same range.
   static constexpr uint64_t no_available_primary_key = std::numeric_limits<uint64_t>::max() - 1;

   static uint64_t next_primary_key_after(uint64_t pk) {
      return pk >= no_available_primary_key ? no_available_primary_key : pk + 1;
   }

   using cached_row = _kv_multi_index_detail::cached_row<T>;

   // Cached items (deserialized rows), keyed by primary key
   mutable std::map<uint64_t, std::unique_ptr<cached_row>> _items;

   // --- Key encoding ---
   // Key layout: [scope:8B BE][pk:8B BE] = 16 bytes.
   // Table name is encoded in table_id (passed to intrinsics), not in the key.
   static constexpr size_t prefix_size = kv::kv_scope_size;
   static constexpr size_t key_size    = prefix_size + _kv_multi_index_detail::u64_size;

   struct primary_key_buf {
      char data[key_size];
   };

   primary_key_buf make_pk(uint64_t pk) const {
      primary_key_buf key;
      kv::encode_be64(key.data,               _scope);
      kv::encode_be64(key.data + prefix_size, pk);
      return key;
   }

   // 8-byte prefix for iteration (scope only — table_id provides isolation)
   struct prefix_buf {
      char data[prefix_size];
   };

   prefix_buf make_prefix() const {
      prefix_buf p;
      kv::encode_be64(p.data, _scope);
      return p;
   }

   // Primary key for secondary index: [pk:8B]
   // Scope is encoded in the sec_key prefix instead (see store_all).
   struct pk_bytes_buf {
      char data[_kv_multi_index_detail::u64_size];
   };

   pk_bytes_buf pk_to_bytes(uint64_t pk) const {
      pk_bytes_buf b;
      kv::encode_be64(b.data, pk);
      return b;
   }

   /// Hold \p obj as the row at \p pk, in the object iterators on that row already point at.
   cached_row* cache_row(uint64_t pk, T&& obj) const {
      auto& cached = _items[pk];
      if (cached) cached->row = std::move(obj);
      else cached = std::make_unique<cached_row>(std::move(obj));
      cached->erased = false;
      return cached.get();
   }

   /// The row at \p pk, read from the host unless it is cached. A row erased through this handle may
   /// have been written since through another, so a marked row is looked up again.
   /// @return the cached row, or nullptr when none exists
   cached_row* load_object(uint64_t pk) const {
      auto it = _items.find(pk);
      if (it != _items.end() && !it->second->erased) return it->second.get();

      auto key = make_pk(pk);
      T obj;
      const bool found = kv::detail::read_row<kv::kv_value_stack_size>(
         _table_id, _code.value, key.data, key_size,
         [&](const char* data, uint32_t size) { obj = kv::deserialize_value<T>(data, size); });
      if (!found) return nullptr;

      return cache_row(pk, std::move(obj));
   }

   /// The cached row \p obj is, which is how a mutator knows it is a row of this handle and not a copy.
   /// A copy may be out of date, and its secondary keys are what the host is told to move.
   /// @param not_a_row raised when \p obj is anything else, in upstream's words
   cached_row& require_cached(const T& obj, const char* not_a_row) const {
      auto cached = _items.find(to_pk_uint64(obj.primary_key()));
      check(cached != _items.end() && &cached->second->row == &obj, not_a_row);
      return *cached->second;
   }

   // Encode [scope:8B BE][secondary_value] using the correct sort-preserving
   // encoder for each type.  The chain's kv_idx_* intrinsics have no scope
   // parameter; encoding scope into sec_key provides equivalent isolation to
   // the legacy db_idx*_find_secondary(code, scope, ...) API.
   //
   // _kv_multi_index_detail::encode_secondary() has an overload for each of the five
   // supported key types and refuses anything else. Every encoding has a fixed size.
   template<typename SecVal>
   auto encode_scoped_secondary(const SecVal& val) const {
      const auto raw = _kv_multi_index_detail::encode_secondary(val);
      _kv_multi_index_detail::fixed_buf<prefix_size + sizeof(raw)> buf;
      kv::encode_be64(buf.data(), _scope);
      memcpy(buf.data() + prefix_size, raw.data(), raw.size());
      return buf;
   }

   // --- Secondary index helpers ---
   template<size_t N, typename Index, typename... Rest>
   struct secondary_ops {
      static constexpr uint32_t _sec_tid = sysio::kv::compute_mi_sec_table_id(static_cast<uint64_t>(TableName), N);

      static void store_all(uint64_t payer, const kv_multi_index& idx, const T& obj) {
         using extractor_t = typename Index::secondary_extractor_type;
         extractor_t ext;
         auto sec_key = idx.encode_scoped_secondary(ext(obj));
         auto pri_key = idx.pk_to_bytes(kv_multi_index::to_pk_uint64(obj.primary_key()));
         kv::detail::idx_store(_sec_tid, payer, pri_key.data, _kv_multi_index_detail::u64_size,
                               sec_key.data(), sec_key.size());
         if constexpr (sizeof...(Rest) > 0) {
            secondary_ops<N+1, Rest...>::store_all(payer, idx, obj);
         }
      }

      static void remove_all(const kv_multi_index& idx, const T& obj) {
         using extractor_t = typename Index::secondary_extractor_type;
         extractor_t ext;
         auto sec_key = idx.encode_scoped_secondary(ext(obj));
         auto pri_key = idx.pk_to_bytes(kv_multi_index::to_pk_uint64(obj.primary_key()));
         kv::detail::idx_erase(_sec_tid, pri_key.data, _kv_multi_index_detail::u64_size,
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
         auto pri_key = idx.pk_to_bytes(kv_multi_index::to_pk_uint64(old_obj.primary_key()));
         if (old_sec != new_sec) {
            kv::detail::idx_update(_sec_tid, payer, pri_key.data, _kv_multi_index_detail::u64_size,
                                   old_sec.data(), old_sec.size(), new_sec.data(), new_sec.size());
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

      const_iterator() : _idx(nullptr), _valid(false) {}

      const T& operator*() const {
         check(_valid && _obj, "dereferencing invalid iterator");
         check(!_obj->erased, kv::msg::iterator_erased);
         return _obj->row;
      }

      const T* operator->() const { return &**this; }

      const_iterator& operator++() {
         check(_idx, "incrementing invalid iterator");
         check(_valid, "cannot increment end iterator");
         char key_buf[key_size];
         uint32_t actual_key = 0;
         uint32_t value_size = 0;
         land(::kv_it_next(_handle, key_buf, key_size, nullptr, 0, &actual_key, &value_size), key_buf, actual_key);
         return *this;
      }

      // Post-increment/decrement deleted: copy requires a kv_it_create at the key
      // (O(log n) per call). Use ++it / --it instead.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      const_iterator& operator--() {
         const char* no_row_before = _handle < 0 ? kv::msg::table_is_empty : kv::msg::table_start;
         check(step_back(), no_row_before);
         return *this;
      }

      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         if (!a._valid && !b._valid) return true; // both end
         if (!a._valid || !b._valid) return false;
         return a._obj && b._obj && a._obj->row.primary_key() == b._obj->row.primary_key();
      }

      friend bool operator!=(const const_iterator& a, const const_iterator& b) {
         return !(a == b);
      }

      const_iterator(const_iterator&& o) noexcept
         : _idx(o._idx), _handle(std::move(o._handle)), _valid(o._valid), _obj(o._obj) {
         o._valid = false;
         o._obj = nullptr;
      }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            _idx = o._idx;
            _handle = std::move(o._handle);
            _valid = o._valid;
            _obj = o._obj;
            o._valid = false;
            o._obj = nullptr;
         }
         return *this;
      }

      // Copy (creates new iterator handle)
      const_iterator(const const_iterator& o) : _idx(o._idx), _valid(o._valid), _obj(o._obj) {
         clone_handle(o);
      }

      const_iterator& operator=(const const_iterator& o) {
         if (this != &o) {
            _idx = o._idx;
            _valid = o._valid;
            _obj = o._obj;
            clone_handle(o);
         }
         return *this;
      }

   private:
      friend class kv_multi_index;
      friend class kv::detail::reverse_cursor<const_iterator>;
      const kv_multi_index* _idx;
      kv::detail::it_handle _handle;
      bool _valid;
      const cached_row* _obj = nullptr;

      /// reverse_cursor's test for a row here: false at end.
      bool positioned() const { return _valid; }

      /// Move to the previous row, landing on end past the first one.
      /// @return false when there was no row to reach, as from the first row or in an empty table
      bool step_back() {
         check(_idx, kv::msg::no_table);
         if (_handle < 0) {
            // End iterator: open a real iterator past this scope's rows, so prev lands on the last.
            auto prefix = _idx->make_prefix();
            kv::detail::end_seek seek;
            _handle.reset(::kv_it_create(_table_id, _idx->_code.value, prefix.data, prefix_size, seek.data, seek.size));
         }
         char key_buf[key_size];
         uint32_t actual_key = 0;
         uint32_t value_size = 0;
         const int32_t status = ::kv_it_prev(_handle, key_buf, key_size, nullptr, 0, &actual_key, &value_size);
         land(status, key_buf, actual_key);
         return status == kv_status_ok;
      }

      /// reverse_cursor's move to the first row, or to end on an empty table. Aborts on a
      /// default-constructed iterator.
      void to_first() {
         check(_idx, kv::msg::no_table);
         *this = _idx->begin();
      }

      /// Take over a handle and load the row it was opened on. A negative handle is the end iterator.
      const_iterator(const kv_multi_index* idx, int32_t handle)
         : _idx(idx), _handle(handle), _valid(false), _obj(nullptr) {
         if (_handle < 0) return;
         char key_buf[key_size];
         uint32_t actual_key = 0;
         uint32_t value_size = 0;
         land(::kv_it_read(_handle, 0, key_buf, key_size, nullptr, 0, &actual_key, &value_size), key_buf, actual_key);
      }

      /// Open this iterator's own handle at the position of \p o. A seek cannot sit on an erased key,
      /// so the row has to be there: copying an iterator after erasing its row aborts.
      void clone_handle(const const_iterator& o) {
         if (o._handle < 0 || !_idx) { _handle.reset(); return; }
         auto prefix = _idx->make_prefix();
         if (_valid && _obj) {
            auto key = _idx->make_pk(to_pk_uint64((**this).primary_key()));
            _handle.reset(::kv_it_create(_table_id, _idx->_code.value, prefix.data, prefix_size, key.data, key_size));
         } else {
            kv::detail::end_seek seek;
            _handle.reset(::kv_it_create(_table_id, _idx->_code.value, prefix.data, prefix_size, seek.data, seek.size));
         }
      }

      /// Load the row a host call landed on. The primary key is the last 8 bytes of the 16-byte key.
      void land(int32_t status, const char* key, uint32_t actual_key) {
         _valid = status == kv_status_ok && actual_key == key_size;
         _obj = nullptr;
         if (!_valid) return;
         _obj = _idx->load_object(kv::decode_be64(key + prefix_size));
         _valid = _obj != nullptr;
      }
   };

   // --- Primary key operations ---

   const_iterator begin() const {
      auto prefix = make_prefix();
      return const_iterator(this, ::kv_it_create(_table_id, _code.value, prefix.data, prefix_size, nullptr, 0));
   }

   const_iterator end() const {
      return const_iterator(this, -1);
   }

   const_iterator cbegin() const { return begin(); }
   const_iterator cend() const { return end(); }

   using const_reverse_iterator = kv::detail::reverse_cursor<const_iterator>;
   const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
   const_reverse_iterator rend() const { return const_reverse_iterator::rend(end()); }
   const_reverse_iterator crbegin() const { return rbegin(); }
   const_reverse_iterator crend() const { return rend(); }

   const_iterator find(name primary) const { return find(primary.value); }
   const_iterator find(uint64_t primary) const {
      auto key = make_pk(primary);
      if (::kv_get(_table_id, _code.value, key.data, key_size, 0, nullptr, 0) < 0) return end();

      // Create iterator positioned at this key
      auto prefix = make_prefix();
      return const_iterator(this, ::kv_it_create(_table_id, _code.value, prefix.data, prefix_size, key.data, key_size));
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
      return obj->row;
   }

   /// Two concrete overloads, the same shape find/require_find/get use above: a one-line
   /// `name` form delegating to the `uint64_t` one.
   ///
   /// Concrete overloads rather than a template or a converting-proxy parameter, because both
   /// of those change what the argument means. A template cannot deduce `lower_bound({42})`;
   /// a proxy accepts `lower_bound({w})` for a `w` converting to a narrower type, which a real
   /// `uint64_t` parameter rejects as narrowing. The parameter here is a `uint64_t`, so every
   /// conversion is the one a `uint64_t` parameter performs.
   ///
   /// Two consequences of the overload pair, both shared with the three siblings above:
   ///
   ///   - `&table::lower_bound` is an overload set, so the bare address cannot be taken. A
   ///     named cast resolves either one:
   ///     `static_cast<const_iterator (table::*)(uint64_t) const>(&table::lower_bound)`.
   ///   - a wrapper convertible to BOTH `uint64_t` and `name` is ambiguous.
   ///
   /// Both are pinned by test.
   const_iterator lower_bound(name primary) const { return lower_bound(primary.value); }
   const_iterator lower_bound(uint64_t primary) const {
      auto key = make_pk(primary);
      auto prefix = make_prefix();
      return const_iterator(this, ::kv_it_create(_table_id, _code.value, prefix.data, prefix_size, key.data, key_size));
   }

   const_iterator upper_bound(name primary) const { return upper_bound(primary.value); }
   const_iterator upper_bound(uint64_t primary) const {
      if (primary == std::numeric_limits<uint64_t>::max()) return end();
      return lower_bound(primary + 1);
   }

   const_iterator iterator_to(const T& obj) const {
      uint64_t pk = to_pk_uint64(obj.primary_key());
      auto cached = _items.find(pk);
      check(cached != _items.end() && !cached->second->erased,
            "object passed to iterator_to is not in multi_index");
      auto key = make_pk(pk);
      auto prefix = make_prefix();
      return const_iterator(this, ::kv_it_create(_table_id, _code.value, prefix.data, prefix_size, key.data, key_size));
   }

   // --- Mutation ---

   template<typename Lambda>
   const_iterator emplace(name payer, Lambda&& constructor) {
      // Reads honour _code (kv_get takes a code argument) but writes do not: kv_set and
      // kv_idx_store have no code parameter and always land on the receiver. A foreign-code
      // handle would therefore read one account and write another -- upstream rejects that,
      // and a ported contract relying on the abort would otherwise get a silent write to its
      // own row. Checked before the constructor runs, so a lambda with side effects is not
      // executed on the rejected path.
      check(_code == kv::detail::receiving_account(), "cannot create objects in table of another contract");

      T obj;
      constructor(obj);

      uint64_t pk = to_pk_uint64(obj.primary_key());
      auto key = make_pk(pk);
      auto value = kv::serialize_value(obj);

      // Insert mode rejects a duplicate primary key in the one search that stores the row.
      // Without it the row would be silently overwritten, and store_secondaries would leave
      // the old (sec_key -> pri_key) mapping pointing at a row whose secondary value has
      // changed. kv::table::emplace inserts the same way.
      check(::kv_set(_table_id, payer.value, key.data, key_size, value.data(), value.size(),
                     kv_set_insert) == kv_status_ok,
            "object with the same primary key already exists");
      store_secondaries(payer.value, obj);

      cache_row(pk, std::move(obj));

      // Update auto-increment. Storing a row at the limit is fine: only available_primary_key()
      // refuses, as upstream's does.
      if (_next_primary_key_cached && pk >= _next_primary_key)
         _next_primary_key = next_primary_key_after(pk);

      return lower_bound(pk);
   }

   template<typename Lambda>
   void modify(const const_iterator& itr, name payer, Lambda&& updater) {
      check(itr != end(), "cannot pass end iterator to modify");
      modify(*itr, payer, std::forward<Lambda>(updater));
   }

   template<typename Lambda>
   void modify(const T& obj, name payer, Lambda&& updater) {
      check(_code == kv::detail::receiving_account(), "cannot modify objects in table of another contract");
      cached_row& cached = require_cached(obj, "object passed to modify is not in multi_index");

      T old_obj = obj;
      // Cast away const for modification (same pattern as legacy multi_index)
      auto& mutable_obj = const_cast<T&>(obj);
      updater(mutable_obj);
      check(mutable_obj.primary_key() == old_obj.primary_key(), "updater cannot change primary key when modifying an object");

      uint64_t pk = to_pk_uint64(mutable_obj.primary_key());
      auto key = make_pk(pk);
      auto value = kv::serialize_value(mutable_obj);
      // Update mode refuses a row that has since gone, which an upsert would write back.
      check(::kv_set(_table_id, payer.value, key.data, key_size, value.data(), value.size(),
                     kv_set_update) == kv_status_ok, kv::msg::row_not_modified);

      update_secondaries(payer.value, old_obj, mutable_obj);

      // The cached object was updated in place. Iterators and references point at it, so they keep
      // working and read the new value, as upstream's do. A secondary iterator whose key moved
      // still steps from the old key; see the comment above the class.
      cached.erased = false;
   }

   const_iterator erase(const_iterator itr) {
      check(itr != end(), "cannot pass end iterator to erase");
      const T& row = *itr;
      ++itr;

      erase(row);
      return itr;
   }

   void erase(const T& obj) {
      check(_code == kv::detail::receiving_account(), "cannot erase objects in table of another contract");
      cached_row& cached = require_cached(obj, "object passed to erase is not in multi_index");

      uint64_t pk = to_pk_uint64(obj.primary_key());
      auto key = make_pk(pk);

      remove_secondaries(obj);
      kv::detail::erase_row(_table_id, key.data, key_size);

      cached.erased = true;
   }

   uint64_t available_primary_key() const {
      if (!_next_primary_key_cached) {
         // Open past every primary key and step back to the last one. An empty scope has none, and
         // its next key is 0.
         auto prefix = make_prefix();
         kv::detail::end_seek seek;
         kv::detail::it_handle it(
            ::kv_it_create(_table_id, _code.value, prefix.data, prefix_size, seek.data, seek.size));

         char key_buf[key_size];
         uint32_t actual_size = 0;
         uint32_t value_size = 0;
         uint64_t next = 0;
         if (::kv_it_prev(it, key_buf, key_size, nullptr, 0, &actual_size, &value_size) == kv_status_ok &&
             actual_size == key_size) {
            next = next_primary_key_after(kv::decode_be64(key_buf + prefix_size));
         }

         _next_primary_key = next;
         _next_primary_key_cached = true;
      }
      check(_next_primary_key < no_available_primary_key, kv::msg::pk_limit);
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
      static constexpr uint32_t _sec_table_id = sysio::kv::compute_mi_sec_table_id(static_cast<uint64_t>(TableName), index_number);

      using index_type = typename std::tuple_element<index_number, std::tuple<Indices...>>::type;
      using secondary_extractor_type = typename index_type::secondary_extractor_type;
      // Derived by CALLING the extractor with a row. Upstream spells this --
      //    typedef typename std::decay<decltype( Extractor()(nullptr) )>::type secondary_key_type;
      // (AntelopeIO CDT multi_index.hpp) -- which works there because its const_mem_fun
      // carries a ChainedPtr overload that nullptr binds to. Passing a ROW REFERENCE
      // instead accepts strictly more: const_mem_fun, an upstream-shaped functor taking
      // a pointer, AND one that only takes `const T&` -- which the old result_type form
      // allowed here and nullptr rejects. Every extraction site calls the extractor with
      // a row, so the reference overload is the one an extractor cannot do without.
      //
      // Through a MUTABLE lvalue, because that is what every extraction site is:
      // `extractor_t ext; ext(obj)`. Deducing through a const lvalue is stricter than the
      // code that runs -- it rejects an extractor whose operator() is not const-qualified,
      // and where both qualifications exist it could pick an overload returning a
      // different type than the one that actually writes the key.
      //
      // Unevaluated operand: nothing is constructed and no row is read.
      using secondary_key_type =
         std::decay_t<decltype(std::declval<secondary_extractor_type&>()(std::declval<const T&>()))>;
      // Secondary key encoding uses sizeof(secondary_key_type) for stack buffer sizing.
      // Trivially copyable types guarantee sizeof == packed size (no varint prefixes),
      // and each of the five encoders below writes exactly sizeof bytes.
      static_assert(std::is_trivially_copyable<secondary_key_type>::value,
                    "secondary key type must be trivially copyable for fixed-size encoding");
      // ...and it has to be one of the five types this shim supports, or lower_bound and
      // ordered iteration would return rows in the key's byte order instead of its value
      // order. find() would still match, so the failure is silent.
      static_assert(_kv_multi_index_detail::is_supported_secondary_key<secondary_key_type>,
                    SYSIO_MI_SECONDARY_KEY_ERROR);

      const kv_multi_index* _mi;
      secondary_index_view(const kv_multi_index& mi) : _mi(&mi) {}

      /// Open a secondary handle bounded to this table's scope, which leads every secondary key.
      static int32_t open_index(const kv_multi_index* mi, const void* seek, uint32_t seek_size) {
         auto prefix = mi->make_prefix();
         return ::kv_idx_create(_sec_table_id, mi->_code.value, prefix.data, prefix_size, seek, seek_size);
      }

      /// Run one host call that lands a secondary handle on an entry, reading its primary key.
      /// The stored pri_key is [pk:8B]. The secondary key is skipped.
      template<auto HostCall>
      static bool read_primary_key(int32_t handle, uint64_t& pk) {
         char pri_buf[_kv_multi_index_detail::u64_size];
         uint32_t sec_size = 0;
         uint32_t pri_size = 0;
         const int32_t status = HostCall(handle, nullptr, 0, pri_buf, _kv_multi_index_detail::u64_size,
                                         &sec_size, &pri_size);
         if (status != kv_status_ok || pri_size != _kv_multi_index_detail::u64_size) return false;
         pk = kv::decode_be64(pri_buf);
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

         const T& operator*() const {
            check(_obj, "deref invalid sec iter");
            check(!_obj->erased, kv::msg::iterator_erased);
            return _obj->row;
         }
         const T* operator->() const { return &**this; }

         const_iterator& operator++() {
            check(_obj, "cannot increment end iterator");
            land<::kv_idx_next>();
            return *this;
         }
         // Post-increment/decrement deleted: copy requires host function calls. Use ++it / --it.
         const_iterator operator++(int) = delete;
         const_iterator operator--(int) = delete;

         const_iterator& operator--() {
            const char* no_entry_before = _handle < 0 ? kv::msg::index_is_empty : kv::msg::index_start;
            check(step_back(), no_entry_before);
            return *this;
         }
         friend bool operator==(const const_iterator& a, const const_iterator& b) {
            if (!a._obj && !b._obj) return true;
            if (!a._obj || !b._obj) return false;
            return a._pk == b._pk;
         }
         friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); }

         const_iterator(const_iterator&& o) noexcept
            : _mi(o._mi), _handle(std::move(o._handle)), _pk(o._pk), _obj(o._obj)
            { o._obj = nullptr; }
         const_iterator& operator=(const_iterator&& o) noexcept {
            if (this != &o) {
               _mi = o._mi; _handle = std::move(o._handle); _pk = o._pk; _obj = o._obj;
               o._obj = nullptr;
            }
            return *this;
         }
         const_iterator(const const_iterator& o) : _mi(o._mi), _pk(o._pk), _obj(o._obj) {
            _clone_handle(o);
         }
         const_iterator& operator=(const const_iterator& o) {
            if (this != &o) {
               _mi = o._mi; _pk = o._pk; _obj = o._obj;
               _clone_handle(o);
            }
            return *this;
         }

      private:
         /// Open this iterator's own handle at the position of \p o, from the row's secondary key. That
         /// key is gone with the row, so copying an iterator after erasing its row aborts. So does
         /// copying one whose entry another table object has moved, or the copy could never step.
         void _clone_handle(const const_iterator& o) {
            if (o._handle >= 0 && _mi && _obj) {
               using extractor_t = typename std::tuple_element<index_number, std::tuple<typename Indices::secondary_extractor_type...>>::type;
               extractor_t ext;
               auto sec_bytes = _mi->encode_scoped_secondary(ext(**this));
               auto prefix = _mi->make_prefix();
               _handle.reset(::kv_idx_find(_sec_table_id, _mi->_code.value, prefix.data, prefix_size,
                                           sec_bytes.data(), sec_bytes.size()));
               check(_handle >= 0, kv::msg::entry_moved);
               // kv_idx_find lands on the first row with this secondary key, but the source
               // iterator may point to a later duplicate. Advance until we find the matching
               // primary key.
               uint64_t found_pk = 0;
               bool     found = read_primary_key<::kv_idx_read>(_handle, found_pk);
               while (found && found_pk != _pk) found = read_primary_key<::kv_idx_next>(_handle, found_pk);
               check(found, kv::msg::entry_moved);
            } else {
               _handle.reset();
            }
         }

         friend struct secondary_index_view;
         friend class kv::detail::reverse_cursor<const_iterator>;
         const kv_multi_index* _mi = nullptr;
         kv::detail::idx_handle _handle;
         uint64_t _pk = 0;
         const cached_row* _obj = nullptr;   ///< the cached row, null at end

         /// reverse_cursor's test for an entry here: false at end.
         bool positioned() const { return _obj != nullptr; }

         /// Move to the previous entry, landing on end past the first one.
         /// @return false when there was no entry to reach, as from the first entry or in an empty index
         bool step_back() {
            check(_mi, kv::msg::no_table);
            if (_handle < 0) {
               // End sentinel: open a real iterator past THIS scope's entries, so prev lands on the
               // last entry in the scope.
               kv::detail::end_seek seek;
               _handle.reset(open_index(_mi, seek.data, seek.size));
            }
            return land<::kv_idx_prev>();
         }

         /// reverse_cursor's move to the first entry, or to end on an empty index. Aborts on a
         /// default-constructed iterator.
         void to_first() {
            check(_mi, kv::msg::no_table);
            *this = const_iterator(_mi, open_index(_mi, nullptr, 0));
         }

         /// Take over a handle and load the entry it was opened on.
         const_iterator(const kv_multi_index* mi, int32_t handle) : _mi(mi), _handle(handle) {
            land<::kv_idx_read>();
         }
         static const_iterator make_end(const kv_multi_index* mi) {
            const_iterator it; it._mi = mi; return it;
         }

         /// Run one host call that lands the handle on an entry, then load its row.
         /// @return false when the call found no entry
         template<auto HostCall>
         bool land() {
            _obj = nullptr;
            if (!read_primary_key<HostCall>(_handle, _pk)) return false;
            _obj = _mi->load_object(_pk);
            return true;
         }
      };

      const_iterator end() const { return const_iterator::make_end(_mi); }
      const_iterator cend() const { return end(); }
      const_iterator cbegin() const { return begin(); }

      using const_reverse_iterator = kv::detail::reverse_cursor<const_iterator>;
      const_reverse_iterator rbegin() const { return const_reverse_iterator(end()); }
      const_reverse_iterator rend() const { return const_reverse_iterator::rend(end()); }
      const_reverse_iterator crbegin() const { return rbegin(); }
      const_reverse_iterator crend() const { return rend(); }

      const_iterator begin() const {
         const_iterator it(_mi, open_index(_mi, nullptr, 0));
         if (!it._obj) return end();
         return it;
      }

      template<typename SecKey>
      const_iterator find(const SecKey& sec_key) const {
         auto sec_bytes = _mi->encode_scoped_secondary(secondary_key_type(sec_key));
         auto prefix = _mi->make_prefix();
         int32_t handle = ::kv_idx_find(_sec_table_id, _mi->_code.value, prefix.data, prefix_size,
                                        sec_bytes.data(), sec_bytes.size());
         if (handle < 0) return end();
         return const_iterator(_mi, handle);
      }

      template<typename SecKey>
      const_iterator lower_bound(const SecKey& sec_key) const {
         auto sec_bytes = _mi->encode_scoped_secondary(secondary_key_type(sec_key));
         const_iterator it(_mi, open_index(_mi, sec_bytes.data(), sec_bytes.size()));
         if (!it._obj) return end();
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
         // Past every entry with this secondary key, whatever its primary key. Secondary keys have one
         // fixed width, so no other key sorts between this one and its successor.
         auto sec_bytes = _mi->encode_scoped_secondary(secondary_key_type(sec_key));
         kv::detail::successor_key seek(sec_bytes.data(), sec_bytes.size());
         const_iterator it(_mi, open_index(_mi, seek.data(), seek.size()));
         if (!it._obj) return end();
         return it;
      }

      template<typename SecKey>
      const T& get(const SecKey& sec_key, const char* msg = "unable to find secondary key") const {
         auto itr = find(sec_key);
         check(itr != end(), msg);
         return *itr;
      }

      // Modify the primary row referenced by a secondary iterator. If the secondary key moves, the
      // iterator reads the new value but steps from the old key; find the row again to step from it.
      template<typename Lambda>
      void modify(const const_iterator& itr, name payer, Lambda&& updater) {
         check(itr._obj, "cannot pass end iterator to modify");
         const_cast<kv_multi_index*>(_mi)->modify(*itr, payer, std::forward<Lambda>(updater));
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
         check(itr._obj, "cannot pass end iterator to erase");
         auto next = itr;
         ++next;
         const_cast<kv_multi_index*>(_mi)->erase(*itr);
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

// Both uses are above; do not leak it out of a public header.
#undef SYSIO_MI_SECONDARY_KEY_ERROR
