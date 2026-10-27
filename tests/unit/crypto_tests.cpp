/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/crypto.hpp>
#include <sysio/crypto_ext.hpp>

#include <cstring>
#include <optional>
#include <type_traits>

using sysio::public_key;
using sysio::signature;
using namespace sysio::native;

// Definitions in `sysio.cdt/libraries/sysio/crypto.hpp`
SYSIO_TEST_BEGIN(public_key_type_test)
   // -----------------------------------------------------
   // bool operator==(const public_key&, const public_key&)
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})  == public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})), true  )
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<uint8_t, 33>{1}) == public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})), false )

   // -----------------------------------------------------
   // bool operator!=(const public_key&, const public_key&)
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})  != public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})), false )
   CHECK_EQUAL( (public_key(std::in_place_index<0>, std::array<uint8_t, 33>{1}) != public_key(std::in_place_index<0>, std::array<uint8_t, 33>{})), true  )
SYSIO_TEST_END

// operator< on public_key must match the chain's canonical key ordering, which
// is an UNSIGNED byte comparison (libfc's authority validate() -> less_comparator
// -> std::memcmp). ecc_public_key is std::array<uint8_t,33>, so std::array's
// lexicographical operator< compares unsigned and agrees with the chain. A signed
// `char` element would order any key byte >= 0x80 as negative and disagree, so a
// contract that sorts such keys into an authority would have updateauth reject it
// as unsorted. These cases pin the unsigned behavior (the regression guard the old
// equality-only test lacked).
SYSIO_TEST_BEGIN(public_key_ordering_test)
   auto ecc = []( uint8_t second_byte ) {
      std::array<uint8_t, 33> a{};
      a[0] = 0x02;              // compressed-point prefix (irrelevant to the test)
      a[1] = second_byte;       // first byte that actually differs below
      return public_key( std::in_place_index<0>, a );
   };
   const public_key hi = ecc( 0x80 );   // high bit set -> 128 unsigned, -128 signed
   const public_key lo = ecc( 0x01 );   //                   1 unsigned,    1 signed

   // Unsigned: 0x80 (128) > 0x01 (1). Signed char would invert both of these.
   CHECK_EQUAL( (lo < hi), true  )
   CHECK_EQUAL( (hi < lo), false )

   // Different variant index always orders by index first, regardless of bytes:
   // a K1 key (index 0) precedes an EM key (index 3) even though `hi`'s bytes are
   // larger than the empty EM key's.
   const public_key em_zero( std::in_place_index<3>, std::array<uint8_t, 33>{} );
   CHECK_EQUAL( (hi < em_zero), true  )
   CHECK_EQUAL( (em_zero < hi), false )
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
   std::array<uint8_t, 33> raw{};
   for ( size_t i = 0; i < raw.size(); ++i ) raw[i] = static_cast<uint8_t>(i + 1);
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

/// Pin six-argument forwarding, borrowed-buffer identity, early exit, and abort propagation.
SYSIO_TEST_BEGIN(ed25519_verify_test)
   using namespace sysio; // Including both C and C++ crypto headers must leave this call unambiguous.
   using verify_function = int32_t (*)(std::span<const char>, std::span<const char>, std::span<const char>);
   static_assert(std::is_same_v<decltype(&sysio::ed25519_verify), verify_function>);
   constexpr int32_t success = 0;
   constexpr int32_t failure = -1;
   constexpr uint32_t signature_size = 64;
   constexpr uint32_t key_size = 32;
   constexpr char abort_message[] = "Ed25519 host abort";
   const std::array<char, signature_size> signature{};
   const std::array<char, key_size> key{};
   const char binary[] = {'a', '\0', static_cast<char>(0xff)};
   const sysio::ed25519_signature_view entry{signature, key, binary};
   const auto previous = intrinsics::get_intrinsic<intrinsics::ed25519_verify>();
   uint32_t calls = 0;
   uint32_t fail_at = 0;
   intrinsics::set_intrinsic<intrinsics::ed25519_verify>(
      [&](const char* sig, uint32_t sig_len, const char* pub, uint32_t pub_len,
          const char* msg, uint32_t msg_len) -> int32_t {
         CHECK_EQUAL(sig == signature.data(), true)
         CHECK_EQUAL(sig_len, signature.size())
         CHECK_EQUAL(pub == key.data(), true)
         CHECK_EQUAL(pub_len, key.size())
         CHECK_EQUAL(msg == binary, true)
         CHECK_EQUAL(msg_len, sizeof(binary))
         ++calls;
         return calls == fail_at ? failure : success;
      });
   CHECK_EQUAL(ed25519_verify(signature, key, binary), success)
   fail_at = calls + 1;
   CHECK_EQUAL(sysio::ed25519_verify(signature, key, binary), failure)
   CHECK_EQUAL(calls, 2u)
   calls = 0;
   CHECK_EQUAL(sysio::ed25519_verify_batch({}), failure)
   CHECK_EQUAL(calls, 0u)
   for (uint32_t count : {1u, 24u, 68u}) {
      const std::vector<sysio::ed25519_signature_view> entries(count, entry);
      calls = fail_at = 0;
      CHECK_EQUAL(sysio::ed25519_verify_batch(entries), success)
      CHECK_EQUAL(calls, count)
      for (uint32_t position : {1u, count / 2 + 1, count}) {
         calls = 0;
         fail_at = position;
         CHECK_EQUAL(sysio::ed25519_verify_batch(entries), failure)
         CHECK_EQUAL(calls, position)
      }
   }
   intrinsics::set_intrinsic<intrinsics::ed25519_verify>(
      [&](const char*, uint32_t, const char*, uint32_t, const char*, uint32_t) -> int32_t {
         sysio::check(false, abort_message);
         return failure;
      });
   CHECK_ASSERT(abort_message, ([&]() { sysio::ed25519_verify_batch({&entry, 1}); }))
   CHECK_ASSERT(abort_message, ([]() { sysio::ed25519_verify({}, {}, {}); }))
   intrinsics::set_intrinsic<intrinsics::ed25519_verify>(previous);
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(public_key_type_test)
   SYSIO_TEST(public_key_ordering_test)
   SYSIO_TEST(signature_type_test)
   SYSIO_TEST(try_recover_key_test)
   SYSIO_TEST(ed25519_verify_test)
   return has_failed();
}
