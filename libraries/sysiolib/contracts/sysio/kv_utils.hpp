#pragma once

#include <sysio/kv_constants.hpp>
#include <sysio/name.hpp>
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/action.hpp>

// KV intrinsic declarations
extern "C" {
   __attribute__((sysio_wasm_import))
   int64_t kv_set(uint32_t table_id, uint64_t payer, const void* key, uint32_t key_size, const void* value, uint32_t value_size);

   __attribute__((sysio_wasm_import))
   int32_t kv_get(uint32_t table_id, uint64_t code, const void* key, uint32_t key_size, void* value, uint32_t value_size);

   __attribute__((sysio_wasm_import))
   int64_t kv_erase(uint32_t table_id, const void* key, uint32_t key_size);

   __attribute__((sysio_wasm_import))
   int32_t kv_contains(uint32_t table_id, uint64_t code, const void* key, uint32_t key_size);

   __attribute__((sysio_wasm_import))
   uint32_t kv_it_create(uint32_t table_id, uint64_t code, const void* prefix, uint32_t prefix_size);

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

   const char* data() const {
      return (len > inline_cap) ? heap_ : inline_;
   }

   uint32_t size() const { return len; }
   bool empty() const { return len == 0; }

   void assign(const char* d, uint32_t s) {
      if (s <= inline_cap) {
         delete[] heap_; heap_ = nullptr;
         if (d && s) std::memcpy(inline_, d, s);
      } else if (d && s) {
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

   key_buf(const key_buf& o) : len(0) {
      if (o.len) assign(o.data(), o.len);
   }

   key_buf& operator=(const key_buf& o) {
      if (this != &o) { clear(); if (o.len) assign(o.data(), o.len); }
      return *this;
   }

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
      o.ptr_ = o.stack_; o.size_ = 0;
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

/// Payer constant: pass as payer to keep existing payer unchanged.
/// Works with kv::table, kv::scoped_table, kv::global, and kv_multi_index.
inline constexpr name same_payer{};

/// Encode a uint64_t to 8 bytes big-endian.
inline void encode_be64(char* buf, uint64_t v) {
   for (int i = sizeof(uint64_t) - 1; i >= 0; --i) { buf[i] = static_cast<char>(v & 0xFF); v >>= 8; }
}

/// Decode 8 bytes big-endian to uint64_t.
inline uint64_t decode_be64(const char* buf) {
   uint64_t v = 0;
   for (size_t i = 0; i < sizeof(uint64_t); ++i)
      v = (v << 8) | static_cast<uint8_t>(buf[i]);
   return v;
}

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
#ifndef KV_KEY_BUF_CAP
#define KV_KEY_BUF_CAP 256
#endif

class be_key_stream {
public:
   static constexpr uint32_t buf_cap = KV_KEY_BUF_CAP;
private:
   static constexpr size_t terminator_size = 2;  // 0x00 0x00 end-marker for escaped strings

   char buf_[buf_cap];
   uint32_t size_ = 0;

   void check_capacity(size_t n) {
      sysio::check(size_ + n <= buf_cap, "be_key_stream: key too large");
   }

   void write_be32(uint32_t v) {
      check_capacity(sizeof(uint32_t));
      for (int i = sizeof(uint32_t) - 1; i >= 0; --i) { buf_[size_ + i] = static_cast<char>(v & 0xFF); v >>= 8; }
      size_ += sizeof(uint32_t);
   }

   void write_be64(uint64_t v) {
      check_capacity(sizeof(uint64_t));
      for (int i = sizeof(uint64_t) - 1; i >= 0; --i) { buf_[size_ + i] = static_cast<char>(v & 0xFF); v >>= 8; }
      size_ += sizeof(uint64_t);
   }

   void write_escaped(const char* data, size_t len) {
      // Upfront: data bytes + 2-byte terminator (no NUL escapes)
      check_capacity(len + terminator_size);
      for (size_t i = 0; i < len; ++i) {
         buf_[size_++] = data[i];
         if (data[i] == '\0') {
            // NUL adds 1 extra byte; still need room for terminator
            check_capacity(1 + terminator_size);
            buf_[size_++] = '\x01';
         }
      }
      buf_[size_++] = '\0';
      buf_[size_++] = '\0';
   }

public:
   void write(const char* data, size_t len) {
      check_capacity(len);
      if (len > 0) { std::memcpy(buf_ + size_, data, len); size_ += len; }
   }

   be_key_stream& operator<<(uint8_t v)  { check_capacity(sizeof(uint8_t)); buf_[size_++] = static_cast<char>(v); return *this; }
   be_key_stream& operator<<(int8_t v)   { return *this << static_cast<uint8_t>(static_cast<uint8_t>(v) ^ 0x80u); }

   be_key_stream& operator<<(uint16_t v) {
      check_capacity(sizeof(uint16_t));
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
      uint128_t bits = static_cast<uint128_t>(v) ^ (static_cast<uint128_t>(1) << 127);
      write_be64(static_cast<uint64_t>(bits >> 64));
      write_be64(static_cast<uint64_t>(bits));
      return *this;
   }

   be_key_stream& operator<<(const name& v) { write_be64(v.value); return *this; }

   be_key_stream& operator<<(float v) {
      static_assert(sizeof(float) == sizeof(uint32_t));
      static_assert(std::numeric_limits<float>::is_iec559);
      uint32_t bits;
      std::memcpy(&bits, &v, sizeof(float));
      if (bits >> 31) bits = ~bits;
      else            bits ^= (uint32_t(1) << 31);
      write_be32(bits);
      return *this;
   }

   be_key_stream& operator<<(double v) {
      static_assert(sizeof(double) == sizeof(uint64_t));
      static_assert(std::numeric_limits<double>::is_iec559);
      uint64_t bits;
      std::memcpy(&bits, &v, sizeof(double));
      if (bits >> 63) bits = ~bits;
      else            bits ^= (uint64_t(1) << 63);
      write_be64(bits);
      return *this;
   }

   be_key_stream& operator<<(bool v) {
      static_assert(sizeof(bool) == 1);
      check_capacity(sizeof(bool));
      buf_[size_++] = v ? 1 : 0;
      return *this;
   }

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

} // namespace kv

using kv::same_payer;

} // namespace sysio
