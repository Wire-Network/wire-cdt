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

namespace sysio { namespace kv {

/**
 * Big-endian key encoder for ordered KV storage.
 *
 * Writes each field in big-endian byte order so that memcmp-based key
 * comparison produces correct lexicographic ordering.
 *
 * Variable-length fields (string, vector<char>) use NUL-escape encoding:
 *   0x00 -> 0x00 0x01, terminated by 0x00 0x00
 * This preserves sort order for arbitrary byte sequences including embedded NULs.
 *
 * Works with SYSLIB_SERIALIZE-generated operators: `stream << key`
 * calls operator<< for each field, routing to BE encoding automatically.
 */
class be_key_stream {
   std::vector<char> buf_;

   void write_be32(uint32_t v) {
      size_t off = buf_.size();
      buf_.resize(off + 4);
      for (int i = 3; i >= 0; --i) { buf_[off + i] = static_cast<char>(v & 0xFF); v >>= 8; }
   }

   void write_be64(uint64_t v) {
      size_t off = buf_.size();
      buf_.resize(off + 8);
      for (int i = 7; i >= 0; --i) { buf_[off + i] = static_cast<char>(v & 0xFF); v >>= 8; }
   }

   // NUL-escape encoding: 0x00 -> 0x00,0x01 + 0x00,0x00 terminator.
   // Preserves lexicographic ordering for arbitrary byte sequences.
   void write_escaped(const char* data, size_t len) {
      for (size_t i = 0; i < len; ++i) {
         buf_.push_back(data[i]);
         if (data[i] == '\0') buf_.push_back('\x01');
      }
      buf_.push_back('\0');
      buf_.push_back('\0');
   }

public:
   void write(const char* data, size_t len) {
      if (len > 0) buf_.insert(buf_.end(), data, data + len);
   }

   be_key_stream& operator<<(uint8_t v)  { buf_.push_back(static_cast<char>(v)); return *this; }
   be_key_stream& operator<<(int8_t v)   { return *this << static_cast<uint8_t>(static_cast<uint8_t>(v) ^ 0x80u); }

   be_key_stream& operator<<(uint16_t v) {
      buf_.push_back(static_cast<char>((v >> 8) & 0xFF));
      buf_.push_back(static_cast<char>(v & 0xFF));
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

   be_key_stream& operator<<(bool v) { buf_.push_back(v ? 1 : 0); return *this; }

   be_key_stream& operator<<(const std::string& v) {
      write_escaped(v.data(), v.size());
      return *this;
   }

   be_key_stream& operator<<(const std::vector<char>& v) {
      write_escaped(v.data(), v.size());
      return *this;
   }

   std::vector<char> release() { return std::move(buf_); }
   const char* data() const { return buf_.data(); }
   size_t size() const { return buf_.size(); }
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
   static std::vector<char> make_key(const K& key) {
      be_key_stream bs;
      bs << key;
      return bs.release();
   }

   static std::vector<char> serialize_value(const V& value) {
      if constexpr (std::is_trivially_copyable<V>::value) {
         if (sizeof(V) == sysio::pack_size(value)) {
            std::vector<char> buf(sizeof(V));
            std::memcpy(buf.data(), &value, sizeof(V));
            return buf;
         }
      }
      auto sz = sysio::pack_size(value);
      std::vector<char> buf(sz);
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
      auto v = serialize_value(value);
      return ::kv_set(kv_format_raw, payer.value, k.data(), k.size(), v.data(), v.size());
   }

   std::optional<V> get(const K& key) const {
      auto k = make_key(key);
      std::optional<V> result;

      int32_t sz = ::kv_get(kv_format_raw, code(), k.data(), k.size(), nullptr, 0);
      if (sz < 0) return result;

      std::vector<char> buf(sz);
      ::kv_get(kv_format_raw, code(), k.data(), k.size(), buf.data(), buf.size());
      result.emplace(deserialize_value(buf.data(), buf.size()));
      return result;
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
            // then prev to land on it. 1024 = max configurable key size (on-chain param).
            ensure_handle();
            char max_key[1024];
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

      ~const_iterator() { if (_handle >= 0) ::kv_it_destroy(_handle); }

      const_iterator(const_iterator&& o) noexcept
         : _tbl(o._tbl), _handle(o._handle), _valid(o._valid),
           _val(std::move(o._val)), _raw_key(std::move(o._raw_key))
      { o._handle = -1; o._valid = false; }

      const_iterator& operator=(const_iterator&& o) noexcept {
         if (this != &o) {
            if (_handle >= 0) ::kv_it_destroy(_handle);
            _tbl = o._tbl; _handle = o._handle; _valid = o._valid;
            _val = std::move(o._val); _raw_key = std::move(o._raw_key);
            o._handle = -1; o._valid = false;
         }
         return *this;
      }
      const_iterator(const const_iterator&) = delete;
      const_iterator& operator=(const const_iterator&) = delete;

   private:
      friend class raw_table;
      const raw_table* _tbl = nullptr;
      int32_t _handle = -1;
      bool _valid = false;
      V _val;
      std::vector<char> _raw_key;

      const_iterator(const raw_table* t, int32_t h, bool valid)
         : _tbl(t), _handle(h), _valid(valid) {
         if (_valid) load();
      }
      static const_iterator make_end(const raw_table* m) { const_iterator it; it._tbl = m; return it; }

      void load() {
         // Read key for equality comparison
         uint32_t key_size = 0;
         ::kv_it_key(_handle, 0, nullptr, 0, &key_size);
         _raw_key.resize(key_size);
         if (::kv_it_key(_handle, 0, _raw_key.data(), key_size, &key_size) != 0) {
            _valid = false; return;
         }
         // Read and deserialize value
         uint32_t val_size = 0;
         ::kv_it_value(_handle, 0, nullptr, 0, &val_size);
         std::vector<char> vbuf(val_size);
         ::kv_it_value(_handle, 0, vbuf.data(), val_size, &val_size);
         _val = deserialize_value(vbuf.data(), vbuf.size());
      }

      void ensure_handle() {
         if (_handle >= 0) return;
         if (!_tbl) return;
         // Empty prefix: iterate all format=0 entries in this contract
         _handle = ::kv_it_create(kv_format_raw, _tbl->code(), nullptr, 0);
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
      if (it != end() && it._raw_key == make_key(key)) ++it;
      return it;
   }
};

} } // namespace sysio::kv
