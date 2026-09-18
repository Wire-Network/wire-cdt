/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/crypto_ext.hpp>
#include <sysio/crypto_bls_ext.hpp>

#include <cstring>

using namespace sysio::native;

// Definitions in `sysio.cdt/libraries/sysio/crypto_ext.hpp`
SYSIO_TEST_BEGIN(ec_point_test)
   std::string x_str = "0123456789abcdeffedcba9876543210";
   std::string y_str = "fedcba98765432100123456789abcdef";
   std::string serialized_str = "0123456789abcdeffedcba9876543210fedcba98765432100123456789abcdef";
   std::vector<char> x( x_str.begin(), x_str.end() );
   std::vector<char> y( y_str.begin(), y_str.end() );
   std::vector<char> serialized( serialized_str.begin(), serialized_str.end() );

   sysio::ec_point point{x, y};
   auto point_serialized = point.serialized();
   CHECK_EQUAL( serialized, point_serialized );

   sysio::ec_point point_from_serialized {point_serialized};
   CHECK_EQUAL( point_from_serialized.serialized(), point_serialized);

   sysio::ec_point_view view{x.data(), static_cast<uint32_t>(x.size()), y.data(), static_cast<uint32_t>(y.size())};
   auto view_serialized = view.serialized();
   CHECK_EQUAL( serialized, view_serialized );

   sysio::ec_point_view view1{ view_serialized };
   CHECK_EQUAL( view1.serialized(), view_serialized );

   sysio::ec_point_view view2{ point };
   CHECK_EQUAL( view2.serialized(), serialized );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(g1_point_test)
   std::vector<char> chars_1(1), chars_32(32), chars_33(33), chars_64(64), chars_65(65);

   CHECK_EQUAL( (sysio::g1_point{chars_32, chars_32}.serialized().size()), 64 );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point{chars_1, chars_1};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point{chars_33, chars_33};}) );
   CHECK_ASSERT( "x's size must be equal to y's", ([&]() {sysio::g1_point{chars_32, chars_33};}) );

   CHECK_EQUAL( (sysio::g1_point_view{chars_32.data(), static_cast<uint32_t>(chars_32.size()), chars_32.data(), static_cast<uint32_t>(chars_32.size())}.size), 32 );

   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point_view{chars_1.data(), static_cast<uint32_t>(chars_1.size()), chars_1.data(), static_cast<uint32_t>(chars_1.size())};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point_view{chars_33.data(), static_cast<uint32_t>(chars_33.size()), chars_33.data(), static_cast<uint32_t>(chars_33.size())};}) );
   CHECK_ASSERT( "x's size must be equal to y's", ([&]() {sysio::g1_point_view{chars_32.data(), static_cast<uint32_t>(chars_32.size()), chars_33.data(), static_cast<uint32_t>(chars_33.size())};}) );

   CHECK_EQUAL( (sysio::g1_point_view{chars_64}.size), 32 );

   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point_view{chars_65};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g1_point_view{chars_32};}) );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(g2_point_test)
   std::vector<char> chars_1(1), chars_64(64), chars_65(65), chars_128(128), chars_129(129);

   CHECK_EQUAL( (sysio::g2_point{chars_64, chars_64}.serialized().size()), 128 );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point{chars_1, chars_1};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point{chars_65, chars_65};}) );
   CHECK_ASSERT( "x's size must be equal to y's", ([&]() {sysio::g2_point{chars_64, chars_65};}) );

   CHECK_EQUAL( (sysio::g2_point_view{chars_64.data(), static_cast<uint32_t>(chars_64.size()), chars_64.data(), static_cast<uint32_t>(chars_64.size())}.size), 64 );

   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point_view{chars_1.data(), static_cast<uint32_t>(chars_1.size()), chars_1.data(), static_cast<uint32_t>(chars_1.size())};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point_view{chars_65.data(), static_cast<uint32_t>(chars_65.size()), chars_65.data(), static_cast<uint32_t>(chars_65.size())};}) );
   CHECK_ASSERT( "x's size must be equal to y's", ([&]() {sysio::g2_point_view{chars_64.data(), static_cast<uint32_t>(chars_64.size()), chars_65.data(), static_cast<uint32_t>(chars_65.size())};}) );

   CHECK_EQUAL( (sysio::g2_point_view{chars_128}.size), 64 );

   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point_view{chars_129};}) );
   CHECK_ASSERT( "point size must match", ([&]() {sysio::g2_point_view{chars_64};}) );
SYSIO_TEST_END

SYSIO_TEST_BEGIN(bigint_test)
   std::vector<char> chars_128(128), chars_256(256);

   CHECK_EQUAL( (sysio::bigint{chars_128}.size()), 128 );
   CHECK_EQUAL( (sysio::bigint{chars_256}.size()), 256 );
SYSIO_TEST_END

// Exercises bls_pop_verify / bls_signature_verify end to end against stubbed host
// intrinsics (set_intrinsic): helper -> ::bls_pairing -> the native intrinsic
// table. Two contracts are pinned.
//
// The operand spans the native trampoline forwards. Both helpers pair two points,
// so the host must see 2*96 bytes of G1 against 2*192 bytes of G2. The trampoline
// used to pass the G1 span for both operands, which any real host rejects on size.
//
// The return-code contract. bls_pairing leaves res untouched when it rejects an
// operand, so a non-zero result must short-circuit rather than fall through to
// comparing the buffer -- which is what made the trampoline bug invisible.
SYSIO_TEST_BEGIN(bls_verify_pairing_contract_test)
   // g2_fromMessage hashes the input into a G2 point through these three before the
   // pairing. Their outputs are irrelevant here; they only have to be reachable.
   intrinsics::set_intrinsic<intrinsics::bls_fp_mod>(
      []( const char*, uint32_t, char*, uint32_t ) -> int32_t { return 0; } );
   intrinsics::set_intrinsic<intrinsics::bls_g2_map>(
      []( const char*, uint32_t, char*, uint32_t ) -> int32_t { return 0; } );
   intrinsics::set_intrinsic<intrinsics::bls_g2_add>(
      []( const char*, uint32_t, const char*, uint32_t, char*, uint32_t ) -> int32_t { return 0; } );

   const sysio::bls_g1 pubkey{};
   const sysio::bls_g2 proof{};

   uint32_t g1_len = 0;
   uint32_t g2_len = 0;
   uint32_t pairs  = 0;

   // --- success: the host reports GT_ONE, so both helpers must verify ---
   intrinsics::set_intrinsic<intrinsics::bls_pairing>(
      [&]( const char*, uint32_t g1l, const char*, uint32_t g2l, uint32_t n, char* res, uint32_t res_len ) -> int32_t {
         g1_len = g1l;
         g2_len = g2l;
         pairs  = n;
         if ( res != nullptr && res_len >= sysio::detail::GT_ONE.size() )
            std::memcpy( res, sysio::detail::GT_ONE.data(), sysio::detail::GT_ONE.size() );
         return 0;
      } );

   const uint32_t expected_g1_len = static_cast<uint32_t>( 2 * std::tuple_size<sysio::bls_g1>::value );
   const uint32_t expected_g2_len = static_cast<uint32_t>( 2 * std::tuple_size<sysio::bls_g2>::value );

   CHECK_EQUAL( sysio::bls_pop_verify( pubkey, proof ), true )
   CHECK_EQUAL( pairs, 2u )
   CHECK_EQUAL( g1_len, expected_g1_len )
   CHECK_EQUAL( g2_len, expected_g2_len )

   g1_len = g2_len = pairs = 0;
   CHECK_EQUAL( sysio::bls_signature_verify( pubkey, proof, "message" ), true )
   CHECK_EQUAL( pairs, 2u )
   CHECK_EQUAL( g1_len, expected_g1_len )
   CHECK_EQUAL( g2_len, expected_g2_len )

   // --- failure: the host rejects an operand. It deliberately writes a result that
   // WOULD compare equal, so these only pass if the return code is actually read.
   intrinsics::set_intrinsic<intrinsics::bls_pairing>(
      []( const char*, uint32_t, const char*, uint32_t, uint32_t, char* res, uint32_t res_len ) -> int32_t {
         if ( res != nullptr && res_len >= sysio::detail::GT_ONE.size() )
            std::memcpy( res, sysio::detail::GT_ONE.data(), sysio::detail::GT_ONE.size() );
         return -1;
      } );

   CHECK_EQUAL( sysio::bls_pop_verify( pubkey, proof ), false )
   CHECK_EQUAL( sysio::bls_signature_verify( pubkey, proof, "message" ), false )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(ec_point_test)
   SYSIO_TEST(g1_point_test)
   SYSIO_TEST(g2_point_test)
   SYSIO_TEST(bigint_test)
   SYSIO_TEST(bls_verify_pairing_contract_test)

   return has_failed();
}
