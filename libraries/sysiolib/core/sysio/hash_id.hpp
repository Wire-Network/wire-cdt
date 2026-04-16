#pragma once

#include "serialize.hpp"
#include "name.hpp"  // for sysio::detail::to_const_char_arr

#include <string_view>

namespace sysio {

   /**
    * hash_id — A 64-bit DJB2 hash of a string identifier.
    *
    * Supports longer names than sysio::name (up to 128 chars, a-zA-Z0-9_).
    * The `_i` literal creates a hash_id at compile time:
    *
    *   auto id = "user_balance_history"_i;  // DJB2 hash of the string
    *
    * Both `hash_id::raw` and `name::raw` are uint64_t enums, so they work
    * interchangeably as template parameters for kv::table, kv::global, etc.
    */
   struct hash_id {
   public:
      static constexpr uint32_t max_length = 128;

      enum class raw : uint64_t {};

      constexpr explicit hash_id(hash_id::raw r)
         : id(static_cast<uint64_t>(r)) {}

      constexpr hash_id() : id(0) {}

      constexpr explicit hash_id(std::string_view s)
         : id(djbh_hash(s)) {}

      static constexpr uint64_t djbh_hash(std::string_view s) {
         uint64_t hash = 5381;
         for (char c : s)
            hash = ((hash << 5) + hash) + static_cast<uint8_t>(c);
         return hash;
      }

      constexpr operator raw() const { return raw(id); }

      /// Allow hash_id to be used as name::raw template parameter.
      /// Both are uint64_t enums — this enables kv::table<"long_name"_i, K, V>.
      constexpr operator name::raw() const { return name::raw(id); }

      constexpr uint64_t value() const { return id; }

      friend constexpr bool operator==(const hash_id& a, const hash_id& b) { return a.id == b.id; }
      friend constexpr bool operator<(const hash_id& a, const hash_id& b) { return a.id < b.id; }

      uint64_t id = 0;

      SYSLIB_SERIALIZE(hash_id, (id))
   };

} // namespace sysio

/// The `_i` literal returns `name::raw` directly so it works as a template
/// parameter for kv::table, kv::global, etc. without a wrapper:
///   kv::table<"user_balance_history"_i, my_key, my_val>
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
template <typename T, T... Str>
inline constexpr sysio::name::raw operator""_i() {
   constexpr uint64_t hash = sysio::hash_id::djbh_hash(
      std::string_view{sysio::detail::to_const_char_arr<Str...>::value, sizeof...(Str)});
   return sysio::name::raw(hash);
}
#pragma clang diagnostic pop
