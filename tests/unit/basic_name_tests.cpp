/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Unit tests for sysio::basic_name<Traits> - the generic packed identifier
 *  behind sysio::name. These exercise basic_name through a slug-style Traits
 *  policy (zero_terminates = true): the configuration a contract code/slug
 *  field uses. The MSB- and LSB-first packing paths are both exercised here
 *  via twin slug traits. sysio::name itself is covered by name_tests.cpp.
 */

#include <cstring>
#include <string>
#include <string_view>

#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

using sysio::basic_name;
using sysio::basic_name_traits;
using sysio::basic_name_endianness;

namespace {

// A slug-style policy: alphabet [A-Z0-9_], up to 8 symbols, zero-terminated.
// Mirrors the opp contract slug_name encoding (symbol 0 = '\0' pad/terminator;
// 1-26 = A-Z; 27-36 = 0-9; 37 = '_').
struct test_slug_traits {
   static constexpr int  max_len = 8;
   static constexpr char alphabet_storage[] =
      "\0ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
   static constexpr std::string_view alphabet{ alphabet_storage,
                                               sizeof(alphabet_storage) - 1 };
   static constexpr bool zero_terminates = true;
   static constexpr basic_name_endianness packing = basic_name_endianness::MSB;
   static constexpr const char* bad_char_message =
      "slug: character is not in [A-Z0-9_]";
   static constexpr const char* too_long_message =
      "slug: string is longer than 8 characters";
   static constexpr const char* bad_final_symbol_message =
      "slug: final symbol does not fit its slot";
};
using test_slug = basic_name<test_slug_traits>;

// LSB-packed twin of test_slug_traits. Same alphabet, same length, same
// zero-terminator semantics; symbols pack first-symbol-in-low-bits. Exists
// only here, to exercise the LSB branch of basic_name::shift().
struct test_slug_lsb_traits {
   static constexpr int  max_len = 8;
   static constexpr char alphabet_storage[] =
      "\0ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
   static constexpr std::string_view alphabet{ alphabet_storage,
                                               sizeof(alphabet_storage) - 1 };
   static constexpr bool zero_terminates = true;
   static constexpr basic_name_endianness packing = basic_name_endianness::LSB;
   static constexpr const char* bad_char_message =
      "slug-lsb: character is not in [A-Z0-9_]";
   static constexpr const char* too_long_message =
      "slug-lsb: string is longer than 8 characters";
   static constexpr const char* bad_final_symbol_message =
      "slug-lsb: final symbol does not fit its slot";
};
using test_slug_lsb = basic_name<test_slug_lsb_traits>;

// A deliberately incomplete policy - missing zero_terminates - used only to
// confirm the basic_name_traits concept rejects it.
struct incomplete_traits {
   static constexpr int              max_len  = 8;
   static constexpr std::string_view alphabet = "ABC";
   static constexpr const char* bad_char_message         = "x";
   static constexpr const char* too_long_message         = "x";
   static constexpr const char* bad_final_symbol_message = "x";
};

} // namespace

// basic_name_traits concept: real policies satisfy it, an incomplete one does not.
SYSIO_TEST_BEGIN(basic_name_test_concept)
   static_assert(  basic_name_traits<test_slug_traits> );
   static_assert(  basic_name_traits<sysio::sysio_name_traits> );
   static_assert( !basic_name_traits<incomplete_traits> );
   CHECK_EQUAL( basic_name_traits<test_slug_traits>,  true )
   CHECK_EQUAL( basic_name_traits<incomplete_traits>, false )
SYSIO_TEST_END

// A slug round-trips: string -> packed uint64 -> string.
SYSIO_TEST_BEGIN(basic_name_test_slug_roundtrip)
   CHECK_EQUAL( test_slug{""}.to_string(),         "" )
   CHECK_EQUAL( test_slug{"ETH"}.to_string(),      "ETH" )
   CHECK_EQUAL( test_slug{"USDC"}.to_string(),     "USDC" )
   CHECK_EQUAL( test_slug{"WIRE"}.to_string(),     "WIRE" )
   CHECK_EQUAL( test_slug{"PRIMARY"}.to_string(),  "PRIMARY" )
   CHECK_EQUAL( test_slug{"ETHEREUM"}.to_string(), "ETHEREUM" )  // exactly 8
   CHECK_EQUAL( test_slug{"A_B"}.to_string(),      "A_B" )
   CHECK_EQUAL( test_slug{"V1"}.to_string(),       "V1" )

   // the string constructor is constexpr — usable in _s-style literals
   static_assert( test_slug{""}.value    == 0 );
   static_assert( test_slug{"ETH"}.value != 0 );
SYSIO_TEST_END

// zero_terminates: to_string() ends at the first symbol-0 slot, so a raw value
// with an *interior* zero decodes to just the prefix — no embedded NUL. (With
// zero_terminates = false this value would decode to "A\0B".)
SYSIO_TEST_BEGIN(basic_name_test_slug_zero_terminates)
   // 6-bit slots, MSB-first: slot i sits at bit (42 - 6*i).
   // [A, <zero>, B] — symbol A = 1, symbol B = 2.
   constexpr uint64_t a_gap_b = (uint64_t{1} << 42) | (uint64_t{2} << 30);
   CHECK_EQUAL( test_slug{a_gap_b}.to_string(), "A" )

   // trailing-zero slots drop away the same way
   CHECK_EQUAL( test_slug{ uint64_t{1} << 42 }.to_string(), "A" )
   CHECK_EQUAL( test_slug{0}.to_string(), "" )
SYSIO_TEST_END

// Ordering / equality come from basic_name's defaulted operator<=> / operator==.
SYSIO_TEST_BEGIN(basic_name_test_slug_compare)
   CHECK_EQUAL( test_slug{"ETH"} == test_slug{"ETH"}, true )
   CHECK_EQUAL( test_slug{"ETH"} != test_slug{"SOL"}, true )
   CHECK_EQUAL( test_slug{"ABC"} <  test_slug{"ABD"}, true )
   CHECK_EQUAL( test_slug{"AAA"} <= test_slug{"AAA"}, true )
   CHECK_EQUAL( test_slug{"ZZZ"} >  test_slug{"AAA"}, true )

   static_assert( test_slug{"ETH"} == test_slug{"ETH"} );
   static_assert( test_slug{"ABC"} <  test_slug{"ABD"} );
SYSIO_TEST_END

// Validation: the constexpr constructor sysio::check-throws on bad input.
SYSIO_TEST_BEGIN(basic_name_test_slug_validation)
   CHECK_ASSERT( "slug: character is not in [A-Z0-9_]",
                 ([]() { test_slug{"eth"}; }) )            // lowercase
   CHECK_ASSERT( "slug: character is not in [A-Z0-9_]",
                 ([]() { test_slug{"ETH-1"}; }) )          // '-' not in alphabet
   CHECK_ASSERT( "slug: string is longer than 8 characters",
                 ([]() { test_slug{"TOOLONG12"}; }) )      // 9 characters
SYSIO_TEST_END

// zero_terminates traits must reject the pad symbol embedded anywhere in the
// input. Without this guard, `test_slug{"A\0B"}` would compile (each char is
// in the alphabet) and to_string() would decode it as just "A" - so the
// constructed value would not round-trip through to_string. With the guard,
// the embedded NUL trips bad_char_message just like an out-of-alphabet char.
SYSIO_TEST_BEGIN(basic_name_test_slug_rejects_embedded_terminator)
   CHECK_ASSERT( "slug: character is not in [A-Z0-9_]",
                 ([]() { test_slug{ std::string_view{"A\0B", 3} }; }) )
   CHECK_ASSERT( "slug: character is not in [A-Z0-9_]",
                 ([]() { test_slug{ std::string_view{"\0A", 2} };  }) )
   CHECK_ASSERT( "slug: character is not in [A-Z0-9_]",
                 ([]() { test_slug{ std::string_view{"A\0", 2} };  }) )
SYSIO_TEST_END

// zero_terminates = false (sysio::name) must NOT reject alphabet[0]: for the
// name alphabet, '.' is the pad symbol AND a legitimate interior character.
// Hardening the embedded-terminator guard must not have leaked into the
// non-zero-terminator branch. We construct names with '.' at every position
// the constructor could plausibly reject it.
SYSIO_TEST_BEGIN(basic_name_test_name_accepts_alphabet_zero)
   using sysio::name;
   CHECK_EQUAL( name{"sysio.token"}.to_string(), "sysio.token" )
   CHECK_EQUAL( name{".alpha"}.to_string(),      ".alpha"      )
   CHECK_EQUAL( name{"a.b.c"}.to_string(),       "a.b.c"       )
   // constexpr path (the `_n` literal):
   static_assert( "sysio.token"_n != name{} );
   static_assert( ".alpha"_n      != name{} );
SYSIO_TEST_END

// LSB packing: the first symbol of "A" sits at bits [0..5]. In the alphabet
// 'A' = 1, so the packed value is exactly 1. For comparison, MSB places 'A'
// at bits [42..47] -> 1 << 42.
SYSIO_TEST_BEGIN(basic_name_test_lsb_first_symbol_lives_in_low_bits)
   CHECK_EQUAL( test_slug_lsb{"A"}.value, uint64_t{1}  )
   CHECK_EQUAL( test_slug_lsb{"Z"}.value, uint64_t{26} )
   CHECK_EQUAL( test_slug_lsb{"_"}.value, uint64_t{37} )
   CHECK_EQUAL( test_slug{"A"}.value,     (uint64_t{1} << 42) )

   // constexpr at compile time too:
   static_assert( test_slug_lsb{"A"}.value == uint64_t{1}  );
   static_assert( test_slug{"A"}.value     == (uint64_t{1} << 42) );
SYSIO_TEST_END

// "AB" in LSB places 'A' at slot 0 (low) and 'B' at slot 1 (next-low):
//   1 | (2 << 6) = 129. MSB places 'A' at slot 0 (high) and 'B' at slot 1
//   (next-high): (1 << 42) | (2 << 36).
SYSIO_TEST_BEGIN(basic_name_test_lsb_two_symbol_layout)
   constexpr uint64_t lsb_ab = uint64_t{1}        | (uint64_t{2} << 6);
   constexpr uint64_t lsb_ba = uint64_t{2}        | (uint64_t{1} << 6);
   constexpr uint64_t msb_ab = (uint64_t{1} << 42) | (uint64_t{2} << 36);
   CHECK_EQUAL( test_slug_lsb{"AB"}.value, lsb_ab )
   CHECK_EQUAL( test_slug_lsb{"BA"}.value, lsb_ba )
   CHECK_EQUAL( test_slug{"AB"}.value,     msb_ab )
SYSIO_TEST_END

// LSB round-trips just like MSB: every string we feed in comes back out.
SYSIO_TEST_BEGIN(basic_name_test_lsb_roundtrip)
   CHECK_EQUAL( test_slug_lsb{""}.to_string(),         ""         )
   CHECK_EQUAL( test_slug_lsb{"ETH"}.to_string(),      "ETH"      )
   CHECK_EQUAL( test_slug_lsb{"USDC"}.to_string(),     "USDC"     )
   CHECK_EQUAL( test_slug_lsb{"WIRE"}.to_string(),     "WIRE"     )
   CHECK_EQUAL( test_slug_lsb{"PRIMARY"}.to_string(),  "PRIMARY"  )
   CHECK_EQUAL( test_slug_lsb{"ETHEREUM"}.to_string(), "ETHEREUM" )
   CHECK_EQUAL( test_slug_lsb{"A_B"}.to_string(),      "A_B"      )
   CHECK_EQUAL( test_slug_lsb{"V1"}.to_string(),       "V1"       )
SYSIO_TEST_END

// Same string, two packing directions: distinct integer values whenever the
// string is more than a single symbol. (A one-symbol "A" is also distinct
// because MSB and LSB place the same slot 0 at opposite ends.)
SYSIO_TEST_BEGIN(basic_name_test_lsb_differs_from_msb)
   CHECK_EQUAL( test_slug{"AB"}.value       != test_slug_lsb{"AB"}.value,       true )
   CHECK_EQUAL( test_slug{"ETH"}.value      != test_slug_lsb{"ETH"}.value,      true )
   CHECK_EQUAL( test_slug{"USDC"}.value     != test_slug_lsb{"USDC"}.value,     true )
   CHECK_EQUAL( test_slug{"ETHEREUM"}.value != test_slug_lsb{"ETHEREUM"}.value, true )
SYSIO_TEST_END

// Integer order vs string lex order: MSB matches, LSB inverts (the first
// symbol sits in the low bits, so higher-position symbols dominate).
SYSIO_TEST_BEGIN(basic_name_test_lsb_integer_order_does_not_match_lex)
   CHECK_EQUAL( test_slug{"AB"}.value     < test_slug{"BA"}.value,     true )
   CHECK_EQUAL( test_slug_lsb{"AB"}.value > test_slug_lsb{"BA"}.value, true )
SYSIO_TEST_END

// LSB-side validation behaves the same as MSB: out-of-alphabet, over-long,
// and embedded terminator all sysio::check-throw with the LSB messages.
SYSIO_TEST_BEGIN(basic_name_test_lsb_validation)
   CHECK_ASSERT( "slug-lsb: character is not in [A-Z0-9_]",
                 ([]() { test_slug_lsb{"eth"}; }) )
   CHECK_ASSERT( "slug-lsb: string is longer than 8 characters",
                 ([]() { test_slug_lsb{"TOOLONG12"}; }) )
   CHECK_ASSERT( "slug-lsb: character is not in [A-Z0-9_]",
                 ([]() { test_slug_lsb{ std::string_view{"A\0B", 3} }; }) )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(basic_name_test_concept)
   SYSIO_TEST(basic_name_test_slug_roundtrip)
   SYSIO_TEST(basic_name_test_slug_zero_terminates)
   SYSIO_TEST(basic_name_test_slug_compare)
   SYSIO_TEST(basic_name_test_slug_validation)
   SYSIO_TEST(basic_name_test_slug_rejects_embedded_terminator)
   SYSIO_TEST(basic_name_test_name_accepts_alphabet_zero)
   SYSIO_TEST(basic_name_test_lsb_first_symbol_lives_in_low_bits)
   SYSIO_TEST(basic_name_test_lsb_two_symbol_layout)
   SYSIO_TEST(basic_name_test_lsb_roundtrip)
   SYSIO_TEST(basic_name_test_lsb_differs_from_msb)
   SYSIO_TEST(basic_name_test_lsb_integer_order_does_not_match_lex)
   SYSIO_TEST(basic_name_test_lsb_validation)

   return has_failed();
}
