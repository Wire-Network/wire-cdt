#pragma once
/**
 *  @file sysio/basic_name.hpp
 *  @brief Generic MSB-first packed 64-bit identifier (contract side).
 *
 *  basic_name<Traits> is the shared core behind sysio::name and
 *  sysio::slug_name. It mirrors the host-side fc::basic_name, but with CDT
 *  semantics: per-character validation via sysio::check (WASM has no
 *  exceptions) in a constexpr constructor, so `_n` / `_s` literals are checked
 *  at compile time.
 *
 *  Traits is the policy that specialises the template; it must satisfy the
 *  basic_name_traits concept (declared below). alphabet[0] is the pad symbol;
 *  zero_terminates selects how to_string() treats a symbol-0 slot — a hard
 *  terminator (slug-style) or an ordinary interior character (name's '.').
 *
 *  The symbol width is derived (the minimal bits to index the alphabet);
 *  symbols are packed most-significant-first, the final symbol narrowed if
 *  max_len * width would exceed 64.
 *
 *  @see fc::basic_name (host-side mirror)
 */

#include "check.hpp"
#include "serialize.hpp"

#include <compare>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace sysio {

/// Compile-time contract for a basic_name Traits policy: an alphabet and a
/// length, a zero_terminates flag steering to_string(), and the three
/// sysio::check messages. Enforced in place of a prose list of requirements.
template <typename Traits>
concept basic_name_traits =
   requires {
      { Traits::max_len }                  -> std::convertible_to<int>;
      { Traits::alphabet }                 -> std::convertible_to<std::string_view>;
      { Traits::zero_terminates }          -> std::convertible_to<bool>;
      { Traits::bad_char_message }         -> std::convertible_to<const char*>;
      { Traits::too_long_message }         -> std::convertible_to<const char*>;
      { Traits::bad_final_symbol_message } -> std::convertible_to<const char*>;
   }
   && Traits::max_len > 0
   && std::string_view{ Traits::alphabet }.size() > 0;

template <basic_name_traits Traits>
struct basic_name {
   uint64_t value = 0;

   constexpr basic_name() = default;
   constexpr explicit basic_name( uint64_t v ) : value(v) {}

   /// Per-character validated string constructor. sysio::check-throws on an
   /// over-long string, an out-of-alphabet character, or a final symbol too
   /// wide for its (possibly narrowed) slot. constexpr — so an invalid
   /// `_n` / `_s` literal is a compile error.
   constexpr explicit basic_name( std::string_view str ) : value(0) {
      // sysio::check is not constexpr — invoke it only on the failure path so a
      // valid `_n` / `_s` literal still constant-evaluates (a bad one reaches
      // check and is therefore a compile error).
      if ( str.size() > static_cast<std::size_t>(Traits::max_len) )
         sysio::check( false, Traits::too_long_message );
      const int n = static_cast<int>(str.size());
      for ( int i = 0; i < Traits::max_len && i < n; ++i ) {
         const uint64_t sym = symbol( str[i] );
         if ( sym > width_mask(i) )
            sysio::check( false, Traits::bad_final_symbol_message );
         value |= sym << shift(i);
      }
   }

   constexpr uint64_t to_uint64_t() const { return value; }
   constexpr bool     empty()      const { return value == 0; }
   constexpr bool     good()       const { return value != 0; }
   constexpr explicit operator bool() const { return value != 0; }

   std::string to_string() const {
      std::string s;
      for ( int i = 0; i < Traits::max_len; ++i ) {
         const uint64_t sym = (value >> shift(i)) & width_mask(i);
         // A zero-terminated alphabet (slug-style) ends at the first symbol-0
         // slot; for name, symbol 0 ('.') is an ordinary interior character.
         if ( Traits::zero_terminates && sym == 0 )
            break;
         s.push_back( character( sym ) );
      }
      if ( !Traits::zero_terminates ) {
         const char pad = character(0);
         while ( !s.empty() && s.back() == pad )
            s.pop_back();
      }
      return s;
   }

   /// character -> symbol; sysio::check-throws on a character outside the
   /// alphabet. (sysio::name re-exposes this as char_to_value.)
   static constexpr uint64_t symbol( char c ) {
      const std::string_view a = Traits::alphabet;
      for ( std::size_t s = 0; s < a.size(); ++s )
         if ( a[s] == c ) return static_cast<uint64_t>(s);
      sysio::check( false, Traits::bad_char_message );
      return 0; // unreachable
   }
   /// symbol -> character; out-of-range symbols decode as the pad (alphabet[0]).
   static constexpr char character( uint64_t s ) {
      const std::string_view a = Traits::alphabet;
      return s < a.size() ? a[s] : a[0];
   }

   // Total order on the packed value; MSB-first packing makes it match the
   // decoded string's lexicographic order. Defaulted <=> / == synthesize the
   // four relational operators and !=.
   friend constexpr std::strong_ordering operator<=>( basic_name a, basic_name b ) = default;
   friend constexpr bool                 operator==( basic_name a, basic_name b ) = default;

   SYSLIB_SERIALIZE( basic_name, (value) )

private:
   // --- symbol width: minimal bits to index the alphabet ---
   static constexpr int symbol_bits( std::size_t alphabet_size ) {
      int b = 0;
      while ( (std::size_t{1} << b) < alphabet_size ) ++b;
      return b;
   }
   static constexpr int bits       = symbol_bits( Traits::alphabet.size() );
   static constexpr int total_bits = Traits::max_len * bits < 64
                                   ? Traits::max_len * bits : 64;
   static_assert( (Traits::max_len - 1) * bits < 64,
                  "basic_name: symbol layout does not fit in 64 bits" );

   // --- MSB-first bit layout; the final symbol absorbs any shortfall ---
   static constexpr uint32_t shift( int i ) {
      const int s = total_bits - bits * (i + 1);
      return s > 0 ? static_cast<uint32_t>(s) : 0u;
   }
   static constexpr uint64_t width_mask( int i ) {
      const int w = (i == Traits::max_len - 1) ? total_bits - bits * i : bits;
      return (static_cast<uint64_t>(1) << w) - 1;
   }
};

} // namespace sysio
