/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <sysio/tester.hpp>
#include <sysio/crypto_ext.hpp>

using namespace sysio::native;

// Definitions in `sysio.cdt/libraries/sysio/crypto_ext.hpp`
EOSIO_TEST_BEGIN(ec_point_test)
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
EOSIO_TEST_END

EOSIO_TEST_BEGIN(g1_point_test)
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
EOSIO_TEST_END

EOSIO_TEST_BEGIN(g2_point_test)
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
EOSIO_TEST_END

EOSIO_TEST_BEGIN(bigint_test)
   std::vector<char> chars_128(128), chars_256(256);

   CHECK_EQUAL( (sysio::bigint{chars_128}.size()), 128 );
   CHECK_EQUAL( (sysio::bigint{chars_256}.size()), 256 );
EOSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   EOSIO_TEST(ec_point_test)
   EOSIO_TEST(g1_point_test)
   EOSIO_TEST(g2_point_test)
   EOSIO_TEST(bigint_test)

   return has_failed();
}
