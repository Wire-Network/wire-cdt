/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/crypto.hpp>

#include <cstring>
#include <optional>

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

// Exercises the recover_key / try_recover_key wrappers end to end against a
// stubbed host intrinsic (set_intrinsic): wrapper -> ::recover_key -> the
// native intrinsic table -> datastream unpack. Pins the host return-code
// contract the wrappers depend on: a non-negative result is the recovered
// public key's packed size; a negative result is a contract-observable
// failure. recover_key aborts on failure; try_recover_key surfaces it as
// std::nullopt without trapping.
SYSIO_TEST_BEGIN(try_recover_key_test)
   // Deterministic K1 public key (variant index 0) round-tripped through the
   // stub so the recovered key is checkable without a real secp256k1 vector.
   std::array<char, 33> raw{};
   for ( size_t i = 0; i < raw.size(); ++i ) raw[i] = static_cast<char>(i + 1);
   const public_key expected( std::in_place_index<0>, raw );
   const std::vector<char> packed = sysio::pack( expected );

   const sysio::checksum256 digest;                                 // host stubbed; contents irrelevant
   const signature sig( std::in_place_index<0>, std::array<char, 65>{} );

   // --- success: host returns the packed key and its size ---
   intrinsics::set_intrinsic<intrinsics::recover_key>(
      [&]( const capi_checksum256*, const char*, size_t, char* pub, size_t publen ) -> int {
         if ( pub != nullptr && publen >= packed.size() )
            std::memcpy( pub, packed.data(), packed.size() );
         return static_cast<int>( packed.size() );
      } );
   {
      auto recovered = sysio::try_recover_key( digest, sig );
      CHECK_EQUAL( recovered.has_value(), true )
      // CHECK_EQUAL is non-fatal; guard the deref so a regression reports a
      // clean failure instead of dereferencing an empty optional (UB).
      if ( recovered )
         CHECK_EQUAL( (*recovered == expected), true )
      CHECK_EQUAL( (sysio::recover_key( digest, sig ) == expected), true )
   }

   // --- failure: host signals a contract-observable failure (rc < 0) ---
   intrinsics::set_intrinsic<intrinsics::recover_key>(
      []( const capi_checksum256*, const char*, size_t, char*, size_t ) -> int {
         return -1;
      } );
   {
      auto recovered = sysio::try_recover_key( digest, sig );
      CHECK_EQUAL( recovered.has_value(), false )                    // clean nullopt, no trap
      CHECK_ASSERT( "unable to recover public key from signature",
         ([&](){ sysio::recover_key( digest, sig ); }) )
   }
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(public_key_type_test)
   SYSIO_TEST(signature_type_test)
   SYSIO_TEST(try_recover_key_test)
   return has_failed();
}
