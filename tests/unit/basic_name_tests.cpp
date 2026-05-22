/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Unit tests for sysio::basic_name<Traits> — the generic MSB-first packed
 *  identifier behind sysio::name. These exercise basic_name through a
 *  slug-style Traits policy (zero_terminates = true): the configuration a
 *  contract code/slug field uses. sysio::name itself is covered by
 *  name_tests.cpp.
 */

#include <cstring>
#include <string>
#include <string_view>

#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

using sysio::basic_name;
using sysio::basic_name_traits;

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
   static constexpr const char* bad_char_message =
      "slug: character is not in [A-Z0-9_]";
   static constexpr const char* too_long_message =
      "slug: string is longer than 8 characters";
   static constexpr const char* bad_final_symbol_message =
      "slug: final symbol does not fit its slot";
};
using test_slug = basic_name<test_slug_traits>;

// A deliberately incomplete policy — missing zero_terminates — used only to
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

   return has_failed();
}
