#pragma once

#include <sysio/kv_constants.hpp>
#include <sysio/name.hpp>
#include <sysio/serialize.hpp>
#include <sysio/datastream.hpp>
#include <sysio/action.hpp>
#include <sysio/context.hpp>

#include "../../capi/sysio/kv.h"

#include <vector>
#include <optional>
#include <cstring>
#include <iterator>
#include <limits>
#include <utility>

#include <sysio/kv_it_handle.hpp>

namespace sysio { namespace kv {

namespace detail {

/// The receiving account, avoiding a host call where possible.
///
/// The generated dispatcher stores the receiver in sysio_contract_name at the top of apply() --
/// and apply() is re-entered per receiver, so it is correct under notification too -- making this
/// a plain global read. SYSIO_DISPATCH emits its own strong apply() and the native dispatch sets
/// nothing, leaving the global 0, which is not a valid account name and so is a safe "unset"
/// sentinel; there we pay the intrinsic. Deliberately not cached on a table object: a contract
/// may hold a `static` table, and the receiver differs between the initial action and a
/// notification handler.
inline name receiving_account() {
   const name ctx = current_context_contract();
   return ctx.value ? ctx : current_receiver();
}

} // namespace detail

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

namespace detail {

/// The smallest key above a key: the same bytes followed by a NUL. Keys sort as unsigned bytes,
/// shorter prefix first, so nothing sorts between the two. Seeking this lands strictly past a key
/// whether or not a row is stored there, which is what upper_bound and a step without a handle need.
class successor_key {
   ser_buf _bytes;
public:
   successor_key(const char* key, uint32_t key_size) : _bytes(key_size + 1) {
      if (key_size > 0) std::memcpy(_bytes.data(), key, key_size);
      _bytes.data()[key_size] = '\0';
   }
   const char* data() const { return _bytes.data(); }
   uint32_t size() const { return _bytes.size(); }
};

/// A seek above every key the chain can store, whatever the scope. It opens a handle at the end of
/// its prefix range, the position prev() walks back from to reach the last row. One byte longer than
/// the largest key, so a key of all 0xFF at that size sorts below it. The host puts no limit on a seek.
struct end_seek {
   static constexpr uint32_t size = kv_key_max_bytes + 1;
   char data[size];
   end_seek() { std::memset(data, 0xFF, size); }
};

/// Reverse iteration over a table or an index, held as one forward iterator on the row this position
/// reads, stepped in place.
///
/// std::reverse_iterator does not suit these iterators. It keeps the position one row past the one it
/// reads and steps a temporary copy on every dereference. An iterator here holds its row, so `*rit`
/// would refer into that temporary, and each read would pay for a stepping copy of its own.
///
/// A reverse scan therefore costs what a forward scan costs, and a reference from `*rit` lasts as
/// long as one from the forward iterator does. A forward iterator at end means rend(), so rend()
/// makes no host call.
///
/// The price is that a position does not follow the table after a write, which one worked out on
/// every read does. Code relying on that compiles unchanged and behaves differently:
///
///   - `while (rit != t.rend()) t.erase(*rit);` never moves rit. On multi_index its second pass aborts
///     with msg::iterator_erased. On kv::table `*rit` is not a key, so the loop does not compile, and
///     `t.erase(rit.key())` in its place aborts with erase's not-found text.
///   - Reading through rit after an erase aborts the same way on multi_index, and on kv::table returns
///     the iterator's own copy of the erased row.
///   - A row written past the position is not seen.
///
/// `++rit` after an erase reaches the row before. To read that row at once, rebuild the iterator from
/// the result of erase: `rit = const_reverse_iterator(t.erase(--rit.base()));`
///
/// \tparam Forward a const_iterator that befriends this class and gives it positioned(), step_back(),
///         which lands on end past the first row instead of aborting, and to_first()
template<typename Forward>
class reverse_cursor {
   Forward _position;

public:
   using iterator_category = std::bidirectional_iterator_tag;
   using value_type        = typename Forward::value_type;
   using difference_type   = typename Forward::difference_type;
   using pointer           = typename Forward::pointer;
   using reference         = typename Forward::reference;

   reverse_cursor() = default;

   /// The row before \p base, as with std::reverse_iterator.
   explicit reverse_cursor(Forward base) : _position(std::move(base)) { _position.step_back(); }

   /// Lets the result of std::make_reverse_iterator compare against rend(), as ported code does.
   /// Each comparison pays for a copy and a step. Dereferencing that std adaptor on a kv::table refers
   /// into a temporary, so it is for comparing only; rbegin() is the form to read through.
   reverse_cursor(const std::reverse_iterator<Forward>& other) : reverse_cursor(other.base()) {}

   /// The position past the first row. Takes the container's end(), which reaches no host.
   static reverse_cursor rend(Forward end) {
      reverse_cursor it;
      it._position = std::move(end);
      return it;
   }

   reference operator*()  const { return *_position; }
   pointer   operator->() const { return _position.operator->(); }

   /// The row's key, on a container whose iterator has one.
   decltype(auto) key() const { return _position.key(); }

   reverse_cursor& operator++() {
      sysio::check(_position.positioned(), msg::past_rend);
      _position.step_back();
      return *this;
   }

   reverse_cursor& operator--() {
      if (_position.positioned()) ++_position;
      else _position.to_first();
      sysio::check(_position.positioned(), msg::before_rbegin);
      return *this;
   }

   // Postfix copies the iterator, which on multi_index opens a host handle. Prefer the prefix form.
   reverse_cursor operator++(int) { reverse_cursor old = *this; ++*this; return old; }
   reverse_cursor operator--(int) { reverse_cursor old = *this; --*this; return old; }

   /// The forward iterator one row past this position, as with std::reverse_iterator. Pays for a
   /// copy and a step, unlike the std adaptor, which held it already.
   Forward base() const {
      Forward next = _position;
      if (next.positioned()) ++next;
      else next.to_first();
      return next;
   }

   friend bool operator==(const reverse_cursor& a, const reverse_cursor& b) { return a._position == b._position; }
   friend bool operator!=(const reverse_cursor& a, const reverse_cursor& b) { return !(a == b); }
};

/// Store an index entry. The entry for a row is unique, so `exists` means the index and its table
/// have come apart: a wrapper has no way forward and no contract-supplied text for it.
inline void idx_store(uint32_t table_id, uint64_t payer, const void* pri, uint32_t pri_size,
                      const void* sec, uint32_t sec_size) {
   sysio::check(::kv_idx_store(table_id, payer, pri, pri_size, sec, sec_size) == kv_status_ok, msg::entry_exists);
}

/// Erase an index entry. As with idx_store, a miss means the index and its table have come apart.
inline void idx_erase(uint32_t table_id, const void* pri, uint32_t pri_size, const void* sec, uint32_t sec_size) {
   sysio::check(::kv_idx_erase(table_id, pri, pri_size, sec, sec_size) == kv_status_ok, msg::entry_not_erased);
}

/// Move an index entry to a new secondary key, telling a missing entry from a taken key.
inline void idx_update(uint32_t table_id, uint64_t payer, const void* pri, uint32_t pri_size,
                       const void* old_sec, uint32_t old_sec_size, const void* new_sec, uint32_t new_sec_size) {
   const int32_t status = ::kv_idx_update(table_id, payer, pri, pri_size, old_sec, old_sec_size,
                                          new_sec, new_sec_size);
   sysio::check(status != kv_status_no_row, msg::entry_not_moved);
   sysio::check(status == kv_status_ok, msg::entry_exists);
}

/// Erase a row a wrapper knows exists, reached by a stale iterator otherwise.
inline void erase_row(uint32_t table_id, const void* key, uint32_t key_size) {
   sysio::check(::kv_erase(table_id, key, key_size) == kv_status_ok, msg::row_not_found);
}

} // namespace detail

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

/// Payer constant (name{}): on an update, keeps the row's existing payer unchanged. Valid only on
/// update -- a new row has no existing payer to keep, so the host rejects same_payer on insert
/// (name an explicit paying account instead). Works with kv::table, kv::scoped_table, kv::global,
/// and kv_multi_index.
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

/// A value's stored bytes: a plain copy when that is what it packs to, its packed form otherwise.
template<typename T>
ser_buf serialize_value(const T& value) {
   if constexpr (std::is_trivially_copyable<T>::value) {
      if (sizeof(T) == sysio::pack_size(value)) {
         ser_buf buf(sizeof(T));
         std::memcpy(buf.data(), &value, sizeof(T));
         return buf;
      }
   }
   uint32_t sz = sysio::pack_size(value);
   ser_buf buf(sz);
   sysio::datastream<char*> ds(buf.data(), buf.size());
   ds << value;
   return buf;
}

/// The value serialize_value stored as \p data.
///
/// A row of exactly sizeof(T) bytes of a trivially copyable \p T is copied. Any other row goes through the
/// datastream, which traps when \p data runs out and ignores whatever \p data holds after the last field, so a
/// row longer than \p T reads its leading fields. Contracts may read rows a wider struct wrote, so this stays.
template<typename T>
T deserialize_value(const char* data, size_t size) {
   T value;
   if constexpr (std::is_trivially_copyable<T>::value) {
      if (size == sizeof(T)) {
         std::memcpy(&value, data, sizeof(T));
         return value;
      }
   }
   sysio::datastream<const char*> ds(data, size);
   ds >> value;
   return value;
}

namespace detail {

/// Read the value stored at a key: one kv_get into a stack buffer of \p StackCapacity bytes, and a
/// second at full size when the value overflows it.
/// @param decode called with the value's bytes and size
/// @return false, with \p decode not called, when no row exists
template<uint32_t StackCapacity, typename Decode>
bool read_row(uint32_t table_id, uint64_t code, const void* key, uint32_t key_size, Decode&& decode) {
   char stack[StackCapacity];
   const int32_t size = ::kv_get(table_id, code, key, key_size, 0, stack, StackCapacity);
   if (size < 0) return false;
   if (static_cast<uint32_t>(size) <= StackCapacity) {
      decode(stack, static_cast<uint32_t>(size));
   } else {
      ser_buf heap(static_cast<uint32_t>(size));
      sysio::check(::kv_get(table_id, code, key, key_size, 0, heap.data(), size) == size, msg::row_lost);
      decode(heap.data(), static_cast<uint32_t>(size));
   }
   return true;
}

/// A float's bits as an unsigned value in numeric order: a negative number flips every bit, and any
/// other gains the top bit.
template<typename Bits>
constexpr Bits sortable_float_bits(Bits bits) {
   constexpr Bits top_bit = Bits(1) << (std::numeric_limits<Bits>::digits - 1);
   return (bits & top_bit) ? ~bits : bits ^ top_bit;
}

/// The reverse of sortable_float_bits.
template<typename Bits>
constexpr Bits float_bits_from_sortable(Bits bits) {
   constexpr Bits top_bit = Bits(1) << (std::numeric_limits<Bits>::digits - 1);
   return (bits & top_bit) ? bits ^ top_bit : ~bits;
}

} // namespace detail

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
      encode_be64(buf_ + size_, v);
      size_ += sizeof(uint64_t);
   }

   void write_escaped(const char* data, size_t len) {
      // Each NUL takes an extra byte, so the check has to count them before writing anything.
      size_t escapes = 0;
      for (size_t i = 0; i < len; ++i) escapes += data[i] == '\0';
      check_capacity(len + escapes + terminator_size);
      for (size_t i = 0; i < len; ++i) {
         buf_[size_++] = data[i];
         if (data[i] == '\0') buf_[size_++] = '\x01';
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
      write_be32(detail::sortable_float_bits(bits));
      return *this;
   }

   be_key_stream& operator<<(double v) {
      static_assert(sizeof(double) == sizeof(uint64_t));
      static_assert(std::numeric_limits<double>::is_iec559);
      uint64_t bits;
      std::memcpy(&bits, &v, sizeof(double));
      write_be64(detail::sortable_float_bits(bits));
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
