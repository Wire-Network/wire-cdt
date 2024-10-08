/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/crypto.hpp>

using sysio::public_key;
using sysio::signature;
using namespace sysio::native;

// Definitions in `sysio.cdt/libraries/sysio/crypto.hpp`
SYSIO_TEST_BEGIN(public_key_type_test)
   // -----------------------------------------------------
   // bool operator==(const public_key&, const public_key&)
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<char, 33>{})  == public_key(std::in_place_index<0>, std::array<char, 33>{})), true  )
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<char, 33>{1}) == public_key(std::in_place_index<0>, std::array<char, 33>{})), false )

   // -----------------------------------------------------
   // bool operator!=(const public_key&, const public_key&)
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<char, 33>{})  != public_key(std::in_place_index<0>, std::array<char, 33>{})), false )
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<char, 33>{1}) != public_key(std::in_place_index<0>, std::array<char, 33>{})), true  )
SYSIO_TEST_END

// Definitions in `sysio.cdt/libraries/sysio/crypto.hpp`
SYSIO_TEST_BEGIN(signature_type_test)
   // ---------------------------------------------------
   // bool operator==(const signature&, const signature&)
   CHECK_EQUAL( (signature(std::in_place_index<0>, std::array<char, 65>{})  == signature(std::in_place_index<0>, std::array<char, 65>{})), true  )
   CHECK_EQUAL( (signature(std::in_place_index<0>, std::array<char, 65>{1}) == signature(std::in_place_index<0>, std::array<char, 65>{})), false )

   // ---------------------------------------------------
   // bool operator!=(const signature&, const signature&)
   CHECK_EQUAL( (signature(std::in_place_index<0>, std::array<char, 65>{1}) != signature(std::in_place_index<0>, std::array<char, 65>{})), true  )
   CHECK_EQUAL( (signature(std::in_place_index<0>, std::array<char, 65>{})  != signature(std::in_place_index<0>, std::array<char, 65>{})), false )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(public_key_type_test)
   SYSIO_TEST(signature_type_test)
   return has_failed();
}
