#pragma once

#include <sysio/name.hpp>
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/action.hpp>

// KV intrinsic declarations
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
#include <vector>
#include <optional>
#include <cstring>
#include <utility>

#include <sysio/kv_constants.hpp>
#include <sysio/kv_it_handle.hpp>

namespace sysio { namespace kv {

// ---------------------------------------------------------------------------
// key_buf — small-buffer key storage (64B inline, heap fallback for large keys)
// ---------------------------------------------------------------------------
struct key_buf {
   static constexpr uint32_t inline_cap = 64;
   char inline_[inline_cap];
   char* heap_ = nullptr;
   uint32_t len = 0;

   const char* data() const { return (len > inline_cap) ? heap_ : inline_; }
   uint32_t size() const { return len; }
   bool empty() const { return len == 0; }

   void assign(const char* d, uint32_t s) {
      if (s <= inline_cap) {
         delete[] heap_; heap_ = nullptr;
         if (d && s) std::memcpy(inline_, d, s);
      } else {
         if (!heap_ || len <= inline_cap || len < s) {
            delete[] heap_; heap_ = new char[s];
         }
         std::memcpy(heap_, d, s);
      }
      len = s;
   }

   void clear() { delete[] heap_; heap_ = nullptr; len = 0; }

   bool equals(const char* d, uint32_t s) const {
      return len == s && (len == 0 || std::memcmp(data(), d, len) == 0);
   }

   ~key_buf() { delete[] heap_; }
   key_buf() = default;
   key_buf(key_buf&& o) noexcept : len(o.len) {
      if (o.len > inline_cap) { heap_ = o.heap_; o.heap_ = nullptr; }
      else if (o.len) { std::memcpy(inline_, o.inline_, o.len); }
      o.len = 0;
   }
   key_buf& operator=(key_buf&& o) noexcept {
      if (this != &o) {
         delete[] heap_; heap_ = nullptr;
         len = o.len;
         if (o.len > inline_cap) { heap_ = o.heap_; o.heap_ = nullptr; }
         else if (o.len) { std::memcpy(inline_, o.inline_, o.len); }
         o.len = 0;
      }
      return *this;
   }
   key_buf(const key_buf&) = delete;
   key_buf& operator=(const key_buf&) = delete;

   friend bool operator==(const key_buf& a, const key_buf& b) { return a.equals(b.data(), b.len); }
   friend bool operator!=(const key_buf& a, const key_buf& b) { return !(a == b); }
};

// ---------------------------------------------------------------------------
// ser_buf — stack-first serialization buffer (kv_value_stack_size inline, heap fallback)
// ---------------------------------------------------------------------------
struct ser_buf {
   char stack_[kv_value_stack_size];
   char* heap_ = nullptr;
   char* ptr_;
   uint32_t size_;
   explicit ser_buf(uint32_t sz) : size_(sz) {
      ptr_ = (sz <= kv_value_stack_size) ? stack_ : (heap_ = new char[sz]);
   }
   ~ser_buf() { delete[] heap_; }
   ser_buf(ser_buf&& o) noexcept : size_(o.size_) {
      if (o.heap_) { heap_ = o.heap_; ptr_ = heap_; o.heap_ = nullptr; }
      else { std::memcpy(stack_, o.stack_, size_); ptr_ = stack_; }
   }
   ser_buf(const ser_buf&) = delete;
   ser_buf& operator=(const ser_buf&) = delete;
   ser_buf& operator=(ser_buf&&) = delete;
   char* data() { return ptr_; }
   const char* data() const { return ptr_; }
   uint32_t size() const { return size_; }
};

// ---------------------------------------------------------------------------
// Compile-time check: is T trivially copyable with sizeof == pack_size?
// When true, serialization is just memcpy of sizeof(T) bytes — no dynamic sizing.
// ---------------------------------------------------------------------------
template<typename T, typename = void>
struct is_fixed_serializable : std::false_type {};

template<typename T>
struct is_fixed_serializable<T, std::enable_if_t<
   std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>
   && (sizeof(T) == sysio::pack_size(T{}))
>> : std::true_type {};

template<typename T>
inline constexpr bool is_fixed_serializable_v = is_fixed_serializable<T>::value;

// ---------------------------------------------------------------------------
// be_key_stream — Big-endian key encoder (fixed buffer, no heap allocation)
//
// Writes each field in big-endian byte order so that memcmp-based key
// comparison produces correct lexicographic ordering.
//
// Variable-length fields (string, vector<char>) use NUL-escape encoding:
//   0x00 -> 0x00 0x01, terminated by 0x00 0x00
// This preserves sort order for arbitrary byte sequences including embedded NULs.
//
// Works with SYSLIB_SERIALIZE-generated operators: `stream << key`
// calls operator<< for each field, routing to BE encoding automatically.
// ---------------------------------------------------------------------------
class be_key_stream {
public:
   static constexpr uint32_t buf_cap = 128;
private:
   char buf_[buf_cap];
   uint32_t size_ = 0;

   void write_be32(uint32_t v) {
      for (int i = 3; i >= 0; --i) { buf_[size_ + i] = static_cast<char>(v & 0xFF); v >>= 8; }
      size_ += 4;
   }

   void write_be64(uint64_t v) {
      for (int i = 7; i >= 0; --i) { buf_[size_ + i] = static_cast<char>(v & 0xFF); v >>= 8; }
      size_ += 8;
   }

   void write_escaped(const char* data, size_t len) {
      for (size_t i = 0; i < len; ++i) {
         buf_[size_++] = data[i];
         if (data[i] == '\0') buf_[size_++] = '\x01';
      }
      buf_[size_++] = '\0';
      buf_[size_++] = '\0';
   }

public:
   void write(const char* data, size_t len) {
      if (len > 0) { std::memcpy(buf_ + size_, data, len); size_ += len; }
   }

   be_key_stream& operator<<(uint8_t v)  { buf_[size_++] = static_cast<char>(v); return *this; }
   be_key_stream& operator<<(int8_t v)   { return *this << static_cast<uint8_t>(static_cast<uint8_t>(v) ^ 0x80u); }

   be_key_stream& operator<<(uint16_t v) {
      buf_[size_++] = static_cast<char>((v >> 8) & 0xFF);
      buf_[size_++] = static_cast<char>(v & 0xFF);
      return *this;
   }
   be_key_stream& operator<<(int16_t v) { return *this << static_cast<uint16_t>(static_cast<uint16_t>(v) ^ 0x8000u); }

   be_key_stream& operator<<(uint32_t v) { write_be32(v); return *this; }
   be_key_stream& operator<<(int32_t v) { return *this << static_cast<uint32_t>(static_cast<uint32_t>(v) ^ 0x80000000u); }

   be_key_stream& operator<<(uint64_t v)  { write_be64(v); return *this; }
   be_key_stream& operator<<(int64_t v) {
      return *this << static_cast<uint64_t>(static_cast<uint64_t>(v) ^ (uint64_t(1) << 63));
   }

   be_key_stream& operator<<(uint128_t v) {
      write_be64(static_cast<uint64_t>(v >> 64));
      write_be64(static_cast<uint64_t>(v));
      return *this;
   }
   be_key_stream& operator<<(int128_t v) {
      uint128_t bits = static_cast<uint128_t>(v) ^ (uint128_t(1) << 127);
      write_be64(static_cast<uint64_t>(bits >> 64));
      write_be64(static_cast<uint64_t>(bits));
      return *this;
   }

   be_key_stream& operator<<(const name& v) { write_be64(v.value); return *this; }

   be_key_stream& operator<<(float v) {
      uint32_t bits;
      std::memcpy(&bits, &v, 4);
      if (bits >> 31) bits = ~bits;
      else            bits ^= (uint32_t(1) << 31);
      write_be32(bits);
      return *this;
   }

   be_key_stream& operator<<(double v) {
      uint64_t bits;
      std::memcpy(&bits, &v, 8);
      if (bits >> 63) bits = ~bits;
      else            bits ^= (uint64_t(1) << 63);
      write_be64(bits);
      return *this;
   }

   be_key_stream& operator<<(bool v) { buf_[size_++] = v ? 1 : 0; return *this; }

   be_key_stream& operator<<(const std::string& v) {
      write_escaped(v.data(), v.size());
      return *this;
   }

   be_key_stream& operator<<(const std::vector<char>& v) {
      write_escaped(v.data(), v.size());
      return *this;
   }

   const char* data() const { return buf_; }
   uint32_t size() const { return size_; }
};

/**
 * kv::raw_table -- Low-level ordered key-value storage using format=0 raw keys.
 *
 * WARNING: All raw_table instances on the same contract share a SINGLE FLAT
 * NAMESPACE (all format=0 entries). If your contract has multiple raw_tables,
 * you MUST add a discriminator field to your key struct to prevent collisions:
 *
 *   struct user_key {
 *      uint8_t  tag = 1;   // unique per raw_table instance
 *      uint64_t id;
 *      SYSLIB_SERIALIZE(user_key, (tag)(id))
 *   };
 *
 * Format=0 entries are stored separately from format=1 entries used by
 * kv_multi_index and kv::table, so there is no collision between raw_table
 * and multi_index/kv::table data.
 *
 * Keys are big-endian encoded for correct memcmp ordering. Values use standard
 * ABI serialization (little-endian) so SHiP clients can decode them via ABI.
 *
 * Performance tip: Keep keys compact. Key bytes are billed directly to RAM,
 * so smaller keys reduce per-row storage cost. The host uses an integer
 * fast-path comparator for 8-byte keys (single bswap64 comparison).
 *
 * Usage:
 *   struct my_key {
 *      std::string region;
 *      uint64_t    id;
 *      SYSLIB_SERIALIZE(my_key, (region)(id))
 *   };
 *   struct [[sysio::table("mydata"), sysio::kv_key("my_key")]] my_value {
 *      std::string payload;
 *      uint64_t    amount;
 *      SYSLIB_SERIALIZE(my_value, (payload)(amount))
 *   };
 *
 *   kv::raw_table<my_key, my_value> store;
 *   store.set({"us-east", 1}, {"hello", 100});
 *   auto val = store.get({"us-east", 1});
 *   for (auto it = store.begin(); it != store.end(); ++it) { ... }
 */
template<typename K, typename V>
class raw_table {
   static be_key_stream make_key(const K& key) {
      be_key_stream bs;
      bs << key;
      return bs;
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

   uint64_t _code = 0;

   uint64_t code() const { return _code ? _code : sysio::current_receiver().value; }

public:
   /// Construct a raw_table. Reads/iterates against \p code's KV data (default: current contract).
   raw_table(sysio::name code = sysio::name{}) : _code(code.value) {}

   // --- Point operations ---

   int64_t set(const K& key, const V& value, sysio::name payer = sysio::name{}) {
      auto k = make_key(key);
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         std::memcpy(vbuf, &value, sizeof(V));
         return ::kv_set(kv_format_raw, payer.value, k.data(), k.size(), vbuf, sizeof(V));
      } else {
         auto v = serialize_value(value);
         return ::kv_set(kv_format_raw, payer.value, k.data(), k.size(), v.data(), v.size());
      }
   }

   std::optional<V> get(const K& key) const {
      auto k = make_key(key);
      if constexpr (is_fixed_serializable_v<V>) {
         char vbuf[sizeof(V)];
         int32_t sz = ::kv_get(kv_format_raw, code(), k.data(), k.size(), vbuf, sizeof(V));
         if (sz < 0) return {};
         V val; std::memcpy(&val, vbuf, sizeof(V));
         return val;
      } else {
         char stack[kv_value_stack_size];
         int32_t sz = ::kv_get(kv_format_raw, code(), k.data(), k.size(), stack, kv_value_stack_size);
         if (sz < 0) return {};
         if (sz <= static_cast<int32_t>(kv_value_stack_size))
            return deserialize_value(stack, sz);
         char* heap = new char[sz];
         ::kv_get(kv_format_raw, code(), k.data(), k.size(), heap, sz);
         V val = deserialize_value(heap, sz);
         delete[] heap;
         return val;
      }
   }

   bool contains(const K& key) const {
      auto k = make_key(key);
      return ::kv_contains(kv_format_raw, code(), k.data(), k.size()) != 0;
   }

   int64_t erase(const K& key) {
      auto k = make_key(key);
      return ::kv_erase(kv_format_raw, k.data(), k.size());
   }

   // --- Ordered iteration ---

   struct const_iterator {
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = V;
      using difference_type = std::ptrdiff_t;
      using pointer = const V*;
      using reference = const V&;

      const_iterator() = default;

      const V& operator*() const  { sysio::check(_valid, "deref end iterator"); return _val; }
      const V* operator->() const { sysio::check(_valid, "deref end iterator"); return &_val; }

      const_iterator& operator++() {
         if (!_valid) return *this;
         if (::kv_it_next(_handle) != 0) _valid = false;
         else load();
         return *this;
      }
      // Post-increment/decrement deleted: copy requires host function calls. Use ++it / --it.
      const_iterator operator++(int) = delete;
      const_iterator operator--(int) = delete;

      const_iterator& operator--() {
         if (_handle < 0) {
            // End sentinel: create handle and seek past all entries.
            // Use a maximal key (all 0xFF) to position past the last entry,
            // then prev to land on it.
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
      friend bool operator==(const const_iterator& a, const const_iterator& b) {
         return a._valid == b._valid && (!a._valid || a._raw_key == b._raw_key);
      }
      friend bool operator!=(const const_iterator& a, const const_iterator& b) { return !(a == b); }

      const_iterator(const_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(std::move(o._handle)), _valid(o._valid),
           _val(std::move(o._val)), _raw_key(std::move(o._raw_key))
      { o._valid = false; }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            _tbl = o._tbl; _handle = std::move(o._handle); _valid = o._valid;
            _val = std::move(o._val); _raw_key = std::move(o._raw_key);
            o._valid = false;
         }
         return *this;
      }
      const_iterator(const const_iterator&) = delete;
      const_iterator& operator=(const const_iterator&) = delete;

   private:
      friend class raw_table;
      const raw_table* _tbl = nullptr;
      kv::detail::it_handle _handle;
      bool _valid = false;
      V _val;
      key_buf _raw_key;

      const_iterator(const raw_table* t, int32_t h, bool valid)
         : _tbl(t), _handle(h), _valid(valid) {
         if (_valid) load();
      }
      static const_iterator make_end(const raw_table* m) { const_iterator it; it._tbl = m; return it; }

      void load() {
         // Read key (stack-buffer-first: 1 host call for keys <= 64B)
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
         // Read and deserialize value
         if constexpr (is_fixed_serializable_v<V>) {
            char vbuf[sizeof(V)];
            uint32_t val_size = 0;
            ::kv_it_value(_handle, 0, vbuf, sizeof(V), &val_size);
            std::memcpy(&_val, vbuf, sizeof(V));
         } else {
            char val_stack[kv_value_stack_size];
            uint32_t val_size = 0;
            ::kv_it_value(_handle, 0, val_stack, kv_value_stack_size, &val_size);
            if (val_size <= kv_value_stack_size) {
               _val = deserialize_value(val_stack, val_size);
            } else {
               char* heap = new char[val_size];
               ::kv_it_value(_handle, 0, heap, val_size, &val_size);
               _val = deserialize_value(heap, val_size);
               delete[] heap;
            }
         }
      }

      void ensure_handle() {
         if (_handle >= 0) return;
         if (!_tbl) return;
         // Empty prefix: iterate all format=0 entries in this contract
         _handle.reset(::kv_it_create(kv_format_raw, _tbl->code(), nullptr, 0));
         if (_valid && !_raw_key.empty()) {
            ::kv_it_lower_bound(_handle, _raw_key.data(), _raw_key.size());
         }
      }
   };

   const_iterator end() const { return const_iterator::make_end(this); }

   const_iterator begin() const {
      // Empty prefix: iterate all format=0 entries
      uint32_t h = ::kv_it_create(kv_format_raw, code(), nullptr, 0);
      return const_iterator(this, h, ::kv_it_status(h) == 0);
   }

   /// First entry with key >= given key.
   const_iterator lower_bound(const K& key) const {
      auto full = make_key(key);
      uint32_t h = ::kv_it_create(kv_format_raw, code(), nullptr, 0);
      int32_t status = ::kv_it_lower_bound(h, full.data(), full.size());
      return const_iterator(this, h, status == 0);
   }

   /// First entry with key > given key.
   const_iterator upper_bound(const K& key) const {
      auto it = lower_bound(key);
      auto encoded = make_key(key);
      if (it != end() && it._raw_key.equals(encoded.data(), encoded.size())) ++it;
      return it;
   }
};

} } // namespace sysio::kv
