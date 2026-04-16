/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Native-compile regression test for be_key_reader in kv_table.hpp.
 *
 *  Background: add_native_contract() defines uint128_t and int128_t as
 *  preprocessor macros that expand to two-token type names
 *  (unsigned __int128 / __int128). Functional-cast syntax like uint128_t(x)
 *  then becomes "unsigned __int128(x)", which is a parse error because
 *  functional-cast requires a single type-id token. be_key_reader had two
 *  such casts on lines 186/191; native-compiled consumers (sysio.system,
 *  sysio.bios, etc.) failed to build with "type-id cannot have a name".
 *  Fixed by switching to static_cast<uint128_t>(x).
 *
 *  This test builds natively with the same macro definitions a native
 *  contract gets, so any future regression in that header will fail to
 *  compile here first.
 */

#include <sysio/tester.hpp>
#include <sysio/kv_table.hpp>

#include <cstring>

using namespace sysio;
using sysio::kv::be_key_reader;

// Round-trip a known 128-bit big-endian value through be_key_reader.
SYSIO_TEST_BEGIN(be_key_reader_uint128_roundtrip)
   // [hi:8B BE][lo:8B BE] = 0x0123456789ABCDEF_FEDCBA9876543210
   const unsigned char buf[16] = {
      0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
      0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   uint128_t v = 0;
   r >> v;
   const uint128_t expected =
      (static_cast<uint128_t>(0x0123456789ABCDEFULL) << 64)
      | static_cast<uint128_t>(0xFEDCBA9876543210ULL);
   CHECK_EQUAL(v, expected)
SYSIO_TEST_END

// int128_t is encoded with its high bit flipped so BE-lex ordering matches
// numeric ordering. Decoding 0x80 00 … 00 must yield 0.
SYSIO_TEST_BEGIN(be_key_reader_int128_zero)
   const unsigned char buf[16] = {
      0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 1; // non-zero so a no-op read would fail the check
   r >> v;
   CHECK_EQUAL(v, static_cast<int128_t>(0))
SYSIO_TEST_END

// Decoding 0x7F FF … FF (-1 with sign-bit flip) must yield -1.
SYSIO_TEST_BEGIN(be_key_reader_int128_negative_one)
   const unsigned char buf[16] = {
      0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 0;
   r >> v;
   CHECK_EQUAL(v, static_cast<int128_t>(-1))
SYSIO_TEST_END

// Decoding 0xFF FF … FF (max int128 with sign-bit flip) must yield INT128_MAX.
SYSIO_TEST_BEGIN(be_key_reader_int128_max)
   const unsigned char buf[16] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 0;
   r >> v;
   const int128_t expected =
      static_cast<int128_t>((static_cast<uint128_t>(~0ULL) << 64)
                            | static_cast<uint128_t>(~0ULL))
      ^ (static_cast<int128_t>(1) << 127);
   CHECK_EQUAL(v, expected)
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(be_key_reader_uint128_roundtrip);
   SYSIO_TEST(be_key_reader_int128_zero);
   SYSIO_TEST(be_key_reader_int128_negative_one);
   SYSIO_TEST(be_key_reader_int128_max);
   return has_failed();
}
