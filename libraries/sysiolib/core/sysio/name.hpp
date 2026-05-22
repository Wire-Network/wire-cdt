/**
 *  @file
 *  @copyright defined in eos/LICENSE
 */
#pragma once

#include "basic_name.hpp"
#include "reflect.hpp"

#include <string>
#include <string_view>

namespace sysio {
   namespace internal_use_do_not_use {
      extern "C" {
         __attribute__((sysio_wasm_import))
         void printn(uint64_t);
      }
   }

   /**
    * @defgroup name
    * @ingroup core
    * @ingroup types
    * @brief SYSIO Name Type
    */

   /// Alphabet + length traits for the SYSIO account-name encoding: up to 13
   /// base-32 symbols over ".12345a-z". Drives sysio::basic_name.
   struct sysio_name_traits {
      static constexpr int              max_len  = 13;
      static constexpr std::string_view alphabet = ".12345abcdefghijklmnopqrstuvwxyz";
      // Symbol 0 ('.') is an ordinary interior character, not a terminator.
      static constexpr bool             zero_terminates = false;
      static constexpr const char* bad_char_message =
         "character is not in allowed character set for names";
      static constexpr const char* too_long_message =
         "string is too long to be a valid name";
      static constexpr const char* bad_final_symbol_message =
         "thirteenth character in name cannot be a letter that comes after j";
   };

   /**
    * Wraps a %uint64_t to ensure it is only passed to methods that expect a %name.
    *
    * The packed encoding and value semantics (constructors, comparisons,
    * to_string, serialization) live in sysio::basic_name; name adds the
    * contract-side surface: raw, print, length, prefix()/suffix(),
    * write_as_string.
    *
    * @ingroup name
    */
   struct name : basic_name<sysio_name_traits> {
      using base = basic_name<sysio_name_traits>;

      /// Scoped enumerated alias of uint64_t, for the raw packed value.
      enum class raw : uint64_t {};

      using base::base;                  // name(uint64_t), name(std::string_view)
      constexpr name() = default;

      /**
       * Construct a new name given a scoped enumerated type of raw (uint64_t).
       */
      constexpr explicit name( name::raw r ) : base( static_cast<uint64_t>(r) ) {}

      /**
       *  Converts a %name Base32 symbol into its corresponding value.
       *  Throws via sysio::check if the character is not in the allowed set.
       *
       *  @param c - Character to be converted
       *  @return constexpr uint8_t - Converted value
       */
      static constexpr uint8_t char_to_value( char c ) {
         return static_cast<uint8_t>( base::symbol(c) );
      }

      /**
       *  Returns the length of the %name
       */
      constexpr uint8_t length()const {
         constexpr uint64_t mask = 0xF800000000000000ull;

         if( value == 0 )
            return 0;

         uint8_t l = 0;
         uint8_t i = 0;
         for( auto v = value; i < 13; ++i, v <<= 5 ) {
            if( (v & mask) > 0 ) {
               l = i;
            }
         }

         return l + 1;
      }

      /**
       *  Returns the suffix of the %name
       */
      constexpr name suffix()const {
         uint32_t remaining_bits_after_last_actual_dot = 0;
         uint32_t tmp = 0;
         for( int32_t remaining_bits = 59; remaining_bits >= 4; remaining_bits -= 5 ) { // Note: remaining_bits must remain signed integer
            // Get characters one-by-one in name in order from left to right (not including the 13th character)
            auto c = (value >> remaining_bits) & 0x1Full;
            if( !c ) { // if this character is a dot
               tmp = static_cast<uint32_t>(remaining_bits);
            } else { // if this character is not a dot
               remaining_bits_after_last_actual_dot = tmp;
            }
         }

         uint64_t thirteenth_character = value & 0x0Full;
         if( thirteenth_character ) { // if 13th character is not a dot
            remaining_bits_after_last_actual_dot = tmp;
         }

         if( remaining_bits_after_last_actual_dot == 0 ) // there is no actual dot in the %name other than potentially leading dots
            return name{value};

         // At this point remaining_bits_after_last_actual_dot has to be within the range of 4 to 59 (and restricted to increments of 5).

         // Mask for remaining bits corresponding to characters after last actual dot, except for 4 least significant bits (corresponds to 13th character).
         uint64_t mask = (1ull << remaining_bits_after_last_actual_dot) - 16;
         uint32_t shift = 64 - remaining_bits_after_last_actual_dot;

         return name{ ((value & mask) << shift) + (thirteenth_character << (shift-1)) };
      }

      /**
       *  Returns the prefix of the %name
       */
      constexpr name prefix() const {
         uint64_t result = value;
         bool not_dot_character_seen = false;
         uint64_t mask = 0xFull;

         // Get characters one-by-one in name in order from right to left
         for( int32_t offset = 0; offset <= 59; ) {
            auto c = (value >> offset) & mask;

            if( !c ) { // if this character is a dot
               if(not_dot_character_seen) { // we found the rightmost dot character
                  result = (value >> offset) << offset;
                  break;
               }
            } else {
               not_dot_character_seen = true;
            }

            if (offset == 0) {
               offset += 4;
               mask = 0x1Full;
            } else {
               offset += 5;
            }
         }

         return name{ result };
      }

      /**
       * Casts a name to raw
       *
       * @return Returns an instance of raw based on the value of a name
       */
      constexpr operator raw()const { return raw(value); }

      /**
       *  Writes the %name as a string to the provided char buffer
       *
       *  @pre The range [begin, end) must be a valid range of memory to write to.
       *  @param begin - The start of the char buffer
       *  @param end - Just past the end of the char buffer
       *  @param dry_run - If true, do not actually write anything into the range.
       *  @return char* - Just past the end of the last character that would be written assuming dry_run == false and end was large enough to provide sufficient space. (Meaning only applies if returned pointer >= begin.)
       *  @post If the output string fits within the range [begin, end) and dry_run == false, the range [begin, returned pointer) contains the string representation of the %name. Nothing is written if dry_run == true or returned pointer > end (insufficient space) or if returned pointer < begin (overflow in calculating desired end).
       */
      char* write_as_string( char* begin, char* end, bool dry_run = false )const {
         static const char* charmap = ".12345abcdefghijklmnopqrstuvwxyz";
         constexpr uint64_t mask = 0xF800000000000000ull;

         if( dry_run || (begin + 13 < begin) || (begin + 13 > end) ) {
            char* actual_end = begin + length();
            if( dry_run || (actual_end < begin) || (actual_end > end) ) return actual_end;
         }

         auto v = value;
         for( auto i = 0; i < 13; ++i, v <<= 5 ) {
            if( v == 0 ) return begin;

            auto indx = (v & mask) >> (i == 12 ? 60 : 59);
            *begin = charmap[indx];
            ++begin;
         }

         return begin;
      }

      /**
       * Prints an names as base32 encoded string
       *
       * @param name to be printed
       */
      inline void print()const {
        internal_use_do_not_use::printn(value);
      }

      CDT_REFLECT(value);
      // name's own serialization: an exact-match operator<<(ds, const name&)
      // must exist, else the generic bluegrass::meta field-iterator is chosen
      // and rejects name as a non-aggregate. (basic_name has its own, used by
      // slug_name, which is the alias type itself rather than a derived type.)
      SYSLIB_SERIALIZE( name, (value) )
   };

   namespace detail {
      template <char... Str>
      struct to_const_char_arr {
         static constexpr const char value[] = {Str...};
      };
   } /// namespace detail
} /// namespace sysio

/**
 * @ingroup name
 * @brief "foo"_n is a shortcut for name("foo")
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
template <typename T, T... Str>
inline constexpr sysio::name operator""_n() {
   constexpr auto x = sysio::name{std::string_view{sysio::detail::to_const_char_arr<Str...>::value, sizeof...(Str)}};
   return x;
}
#pragma clang diagnostic pop
