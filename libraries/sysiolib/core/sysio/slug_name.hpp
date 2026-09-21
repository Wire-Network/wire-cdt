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

   /// Alphabet + length traits for the registry-code encoding: up to 8 symbols over [A-Z0-9_].
   /// Drives sysio::basic_name.
   ///
   /// Byte-identical with the host-side fc::slug_name, which instantiates fc::basic_name over the same
   /// alphabet, length, terminator rule and packing direction. The two traits structs are the whole
   /// specification — keep them diffable line for line.
   struct slug_name_traits {
      /// 8, not the 10 that would fill 64 bits: 8 symbols x 6 bits = 48, so every encoded value stays in
      /// [0, 2^48) — under JS Number's 2^53 safe integer limit, letting TS consumers use `number` rather
      /// than bigint.
      static constexpr int max_len = 8;

      /// Symbol 0 is the '\0' pad/terminator; 1-26 = A-Z, 27-36 = 0-9, 37 = '_'. Held as a named array so
      /// the length comes from sizeof — a string_view built straight from the literal would stop at the
      /// leading NUL.
      static constexpr char alphabet_storage[] = "\0ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
      static constexpr std::string_view alphabet{ alphabet_storage, sizeof(alphabet_storage) - 1 };

      /// A code must START with a letter. This is what makes the host's string
      /// carrier unambiguous: no legal code can be spelled like a number, so a
      /// bare JSON string is always a code and never a decimal. Without it the
      /// alphabet's digits make "7" both a valid code and a valid decimal, and
      /// "1E3" / "0X10" additionally collide with JS numeric syntax. Digits and
      /// '_' remain legal in every position after the first ("V1", "TRAIL_").
      /// The empty string is unaffected - it is the zero sentinel.
      static constexpr std::string_view leading_alphabet{ "ABCDEFGHIJKLMNOPQRSTUVWXYZ" };

      /// A symbol-0 slot TERMINATES the string, unlike name's '.' which is an ordinary interior character.
      /// This is why a slug_name is not total over uint64: every value below 2^42 has a zero in the char[0]
      /// slot and so decodes to the empty string.
      static constexpr bool zero_terminates = true;

      /// MSB-first: char[0] occupies bits [42..47]. This is what gives the packed value its grouping
      /// property — a shared textual prefix is a shared leading bit prefix, so prefix-related codes are
      /// contiguous in key order and retrievable as a range.
      static constexpr basic_name_endianness packing = basic_name_endianness::MSB;

      static constexpr const char* bad_char_message =
         "character is not in allowed character set for slug_names ([A-Z0-9_])";
      static constexpr const char* too_long_message = "string is too long to be a valid slug_name";
      static constexpr const char* bad_leading_char_message =
         "slug_name must start with a letter ([A-Z])";
      static constexpr const char* bad_final_symbol_message =
         "final character in slug_name does not fit its packed slot";
      static constexpr const char* not_normalized_message =
         "slug_name is not properly normalized";
   };

   /// Packed registry-code identifier — up to 8 symbols over [A-Z0-9_].
   ///
   /// A DERIVED STRUCT, not an alias -- the same shape as sysio::name, and for the same reason.
   ///
   /// abigen matches builtins on the namespace-stripped written spelling, and `slug_name` is in that
   /// set (plugins/sysio/gen.hpp) -- but an ALIAS never reaches that match: abigen resolves it through
   /// to the underlying template first and then emits BOTH a typedef
   /// (`slug_name` -> `basic_name_slug_name_traits`) AND a struct_def for the instantiation. The host,
   /// which knows `slug_name` intrinsically, then rejects the ABI outright:
   ///   duplicate_abi_type_def_exception: type already exists 'slug_name'
   /// `sysio::name` avoids this only because it is a derived struct, so the builtin match applies to
   /// the written name. slug_name follows it.
   struct slug_name : basic_name<slug_name_traits> {
      using base = basic_name<slug_name_traits>;
      using base::base;                  // slug_name(uint64_t), slug_name(std::string_view)
      constexpr slug_name() = default;

      /// slug_name's own serialization, for the same reason sysio::name carries one: a DERIVED type
      /// needs an EXACT-match operator on itself. The base's hidden friend takes `const basic_name&`,
      /// so reaching it from a `slug_name` requires a derived-to-base conversion -- and the generic
      /// class-template overload, which matches exactly, wins instead. That overload hands the type to
      /// the bluegrass::meta field iterator, which rejects anything with a user-declared constructor:
      ///   "Types with user specified constructors are not supported"
      /// Clang happens to tolerate it; GCC 13 does not, which matters because add_native_contract()
      /// builds with the HOST compiler and its generated dispatcher deserializes action arguments
      /// through this path. Forwarding to the base keeps the bytes identical to basic_name's.
      SYSLIB_SERIALIZE_DERIVED_EMPTY( slug_name, base )
   };

} // namespace sysio

/**
 *  Compile-time slug_name literal: `"ETH"_s`, `"LIQSOL"_s`. Validation happens in basic_name's constexpr
 *  constructor — a character outside [A-Z0-9_], or more than 8 characters, fails the constant evaluation.
 *  Mirrors the shape of sysio::name's `_n` literal, including its global scope.
 */
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-string-literal-operator-template"
template <typename T, T... Str>
inline constexpr sysio::slug_name operator""_s() {
   constexpr auto x = sysio::slug_name{ std::string_view{ sysio::detail::to_const_char_arr<Str...>::value,
                                                          sizeof...(Str) } };
   return x;
}
#pragma clang diagnostic pop
