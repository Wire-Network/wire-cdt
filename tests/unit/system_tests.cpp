/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <string>
#include <type_traits>

#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

using std::move;
using std::string;

using sysio::check;

// Definitions in `sysio.cdt/libraries/sysiolib/system.hpp`
SYSIO_TEST_BEGIN(system_test)
   // ------------------------------------
   // inline void check(bool, const char*)
   CHECK_ASSERT( "asserted", []() { const char* str{"asserted"}; check(false, str);} );

   // --------------------------------------
   // inline void check(bool, const string&)
   CHECK_ASSERT( "asserted", []() { const string str{"asserted"}; check(false, str);} );

   // ---------------------------------------
   // inline void check(bool, const string&&)
   CHECK_ASSERT( "asserted", []() { const string str{"asserted"}; check(false, move(str));} );

   // --------------------------------------------
   // inline void check(bool, const char*, size_t)
   CHECK_ASSERT( "assert", []() { const char* str{"asserted"}; check(false, str, 6);} );

   // ----------------------------------------------
   // inline void check(bool, const string&, size_t)
   CHECK_ASSERT( "assert", []() { const string str{"asserted"}; check(false, str, 6);} );

   // ---------------------------------
   // inline void check(bool, uint64_t)
   CHECK_ASSERT("1", []() { check(false, 1);} );
   CHECK_ASSERT("100", []() { check(false, 100);} );
   CHECK_ASSERT("18446744073709551615", []() { check(false, 18446744073709551615ULL);} );
   CHECK_ASSERT("18446744073709551615", []() { check(false, -1ULL);} );

   // -----------------------------------------------------------
   // The flags are bool, so a non-zero bitwise result arrives as 1
   using namespace sysio::internal_use_do_not_use;
   static_assert( std::is_same_v<decltype(&sysio_assert), void (*)(bool, const char*)> );
   static_assert( std::is_same_v<decltype(&sysio_assert_message), void (*)(bool, const char*, uint32_t)> );
   static_assert( std::is_same_v<decltype(&sysio_assert_code), void (*)(bool, uint64_t)> );
   volatile uint32_t flags = 12;
   sysio_assert( flags & 4, "flag not set" );
   CHECK_ASSERT( "flag not set", [&]() { sysio_assert( flags & 2, "flag not set" ); } );
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(system_test);
   return has_failed();
}
