#pragma once

#include "basic_name.hpp"
#include "name.hpp"  // for sysio::detail::to_const_char_arr

#include <string_view>

namespace sysio {

   /**
    * @defgroup slug_name
    * @ingroup core
    * @ingroup types
    * @brief Packed registry-code identifier
    */

   /// Alphabet + length traits for the registry-code encoding: up to 8 symbols
   /// over [A-Z0-9_]. Drives sysio::basic_name.
   ///
   /// Byte-identical with the host-side fc::slug_name, which instantiates
   /// fc::basic_name over the same alphabet, length, terminator rule and
   /// packing direction. The two traits structs are the whole specification —
   /// keep them diffable line for line.
   struct slug_name_traits {
      /// 8, not the 10 that would fill 64 bits: 8 symbols x 6 bits = 48, so
      /// every encoded value stays in [0, 2^48) — under JS Number's 2^53 safe
      /// integer limit, letting TS consumers use `number` rather than bigint.
      static constexpr int max_len = 8;

      /// Symbol 0 is the '\0' pad/terminator; 1-26 = A-Z, 27-36 = 0-9, 37 = '_'.
      /// Held as a named array so the length comes from sizeof — a string_view
      /// built straight from the literal would stop at the leading NUL.
      static constexpr char alphabet_storage[] =
         "\0ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
      static constexpr std::string_view alphabet{ alphabet_storage,
                                                  sizeof(alphabet_storage) - 1 };

      /// A symbol-0 slot TERMINATES the string, unlike name's '.' which is an
      /// ordinary interior character. This is why a slug_name is not total over
      /// uint64: every value below 2^42 has a zero in the char[0] slot and so
      /// decodes to the empty string.
      static constexpr bool zero_terminates = true;

      /// MSB-first: char[0] occupies bits [42..47]. This is what gives the
      /// packed value its grouping property — a shared textual prefix is a
      /// shared leading bit prefix, so prefix-related codes are contiguous in
      /// key order and retrievable as a range.
      static constexpr basic_name_endianness packing = basic_name_endianness::MSB;

      static constexpr const char* bad_char_message =
         "character is not in allowed character set for slug_names ([A-Z0-9_])";
      static constexpr const char* too_long_message =
         "string is too long to be a valid slug_name";
      static constexpr const char* bad_final_symbol_message =
         "final character in slug_name does not fit its packed slot";
   };

   /// Packed registry-code identifier — up to 8 symbols over [A-Z0-9_].
   ///
   /// Declared as an alias rather than a derived struct on purpose. abigen
   /// matches builtins on the namespace-stripped written spelling, and
   /// `slug_name` is in that set (plugins/sysio/gen.hpp), so no typedef and no
   /// struct_def is emitted and a field declared `sysio::slug_name` carries the
   /// bare ABI type name. A derived struct would reflect as a base with zero
   /// declared fields the moment it stopped being a builtin.
   using slug_name = basic_name<slug_name_traits>;

} // namespace sysio

/**
 *  Compile-time slug_name literal: `"ETH"_s`, `"LIQSOL"_s`. Validation happens
 *  in basic_name's constexpr constructor — a character outside [A-Z0-9_], or
 *  more than 8 characters, fails the constant evaluation. Mirrors the shape of
 *  sysio::name's `_n` literal, including its global scope.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
template <typename T, T... Str>
inline constexpr sysio::slug_name operator""_s() {
   constexpr auto x = sysio::slug_name{
      std::string_view{ sysio::detail::to_const_char_arr<Str...>::value, sizeof...(Str) } };
   return x;
}
#pragma clang diagnostic pop
