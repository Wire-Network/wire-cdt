/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 * Unit tests for sysio::slug_name — the packed registry-code identifier, an instantiation of sysio::basic_name over
 * the [A-Z0-9_] alphabet.
 *
 * basic_name's generic behaviour (both packing directions, the traits concept) is covered by basic_name_tests.cpp.
 * What is pinned HERE is the slug policy itself, and above all its BYTE IDENTITY with the host-side fc::slug_name:
 * the two are separate instantiations in separate toolchains, agreeing only because their traits agree. The expected
 * values below are what fc produces. If either side's alphabet, length, terminator rule or packing direction drifts,
 * these fail — which is the only mechanical guard the cross-language encoding has.
 */

#include <string>
#include <string_view>

#include <sysio/sysio.hpp>
#include <sysio/key_utils.hpp>
#include <sysio/slug_name.hpp>
#include <sysio/tester.hpp>

using sysio::slug_name;

// ── the traits policy, pinned ───────────────────────────────────────────────
static_assert(sysio::slug_name_traits::max_len == 8,
              "8 symbols x 6 bits = 48, keeping every value under JS Number's 2^53 limit");
static_assert(sysio::slug_name_traits::zero_terminates,
              "a symbol-0 slot terminates a slug, unlike name's '.'");
static_assert(sysio::slug_name_traits::packing == sysio::basic_name_endianness::MSB,
              "MSB-first is what gives a shared prefix a shared leading bit prefix");
static_assert(sysio::slug_name_traits::alphabet.size() == 38,
              "'\\0' pad + A-Z + 0-9 + '_'");

SYSIO_TEST_BEGIN(slug_name_byte_identity_with_the_host)
   // These constants are fc::slug_name's output. Do not "fix" a failure by editing them — a mismatch means the two
   // traits have diverged.
   CHECK_EQUAL(slug_name{"A"}.value,        4398046511104ull)
   CHECK_EQUAL(slug_name{"ETH"}.value,     23373212024832ull)
   CHECK_EQUAL(slug_name{"WIRE"}.value,   101792956284928ull)
   CHECK_EQUAL(slug_name{"SOLANA"}.value,  84606581215232ull)
   CHECK_EQUAL(slug_name{"LIQSOL"}.value,  53413609783296ull)
   CHECK_EQUAL(slug_name{"Z1234567"}.value, 116305004726370ull)
   CHECK_EQUAL(slug_name{"Z_______"}.value, 116932188985701ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_round_trips_canonical_spellings)
   for (const char* s : {"A", "ETH", "WIRE", "SOLANA", "LIQSOL", "Z1234567", "Z_______"}) {
      CHECK_EQUAL(slug_name{std::string_view{s}}.to_string(), std::string{s})
   }
   CHECK_EQUAL(slug_name{}.to_string(), std::string{})
   CHECK_EQUAL(slug_name{std::string_view{""}}.value, 0ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_must_start_with_a_letter)
   // The rule that makes the host's string carrier unambiguous: no legal code
   // can be spelled like a number. Byte-identical with the host-side rule in
   // fc::slug_name_traits::leading_alphabet — keep the two diffable.
   CHECK_ASSERT("slug_name must start with a letter ([A-Z])",
                []() { slug_name{std::string_view{"7"}}; })
   CHECK_ASSERT("slug_name must start with a letter ([A-Z])",
                []() { slug_name{std::string_view{"0"}}; })
   CHECK_ASSERT("slug_name must start with a letter ([A-Z])",
                []() { slug_name{std::string_view{"1E3"}}; })
   CHECK_ASSERT("slug_name must start with a letter ([A-Z])",
                []() { slug_name{std::string_view{"12345678"}}; })
   CHECK_ASSERT("slug_name must start with a letter ([A-Z])",
                []() { slug_name{std::string_view{"_"}}; })
   // Digits and '_' stay legal after the first symbol, and "" is the sentinel.
   CHECK_EQUAL(slug_name{std::string_view{"V1"}}.to_string(), std::string{"V1"})
   CHECK_EQUAL(slug_name{std::string_view{"TRAIL_"}}.to_string(), std::string{"TRAIL_"})
   CHECK_EQUAL(slug_name{std::string_view{""}}.value, 0ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_literals_are_compile_time)
   static_assert("ETH"_s.value    == 23373212024832ull);
   static_assert("LIQSOL"_s.value == 53413609783296ull);
   static_assert("ETH"_s == slug_name{23373212024832ull});
   CHECK_EQUAL("WIRE"_s.value, 101792956284928ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_canonical_values_are_at_or_above_the_2_42_floor)
   // char[0] sits at bits [42..47], so any canonical (non-empty) slug is >= 1<<42 — and conversely every value below
   // the floor has a zero in the char[0] slot and so decodes to the empty string. This is why the host's JSON carrier
   // cannot be string-only.
   constexpr uint64_t floor = 1ull << 42;
   CHECK_EQUAL(slug_name{"A"}.value >= floor, true)
   CHECK_EQUAL(slug_name{"ETH"}.value >= floor, true)
   CHECK_EQUAL(slug_name{uint64_t{7}}.to_string(), std::string{})
   CHECK_EQUAL(slug_name{floor - 1}.to_string(), std::string{})
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_groups_shared_prefixes_in_the_high_bits)
   // The grouping property slug_name exists for: a shared textual prefix is a shared leading BIT prefix, so
   // prefix-related codes are contiguous in key order. k symbols share the top 6k bits of the 48-bit payload.
   auto shared_high_bits = [](slug_name a, slug_name b) {
      int n = 0;
      for (int bit = 47; bit >= 0; --bit) {
         if (((a.value >> bit) & 1ull) != ((b.value >> bit) & 1ull)) break;
         ++n;
      }
      return n;
   };
   CHECK_EQUAL(shared_high_bits("LIQSOL"_s, "LIQETH"_s) >= 18, true)  // "LIQ"  = 3 x 6
   CHECK_EQUAL(shared_high_bits("WIRE"_s,   "WIREUSD"_s) >= 24, true) // "WIRE" = 4 x 6
   // And a differing first symbol shares nothing in the top 6 bits.
   CHECK_EQUAL(shared_high_bits("ETH"_s, "WIRE"_s) < 6, true)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_to_key_writes_eight_bytes)
   // Guards CDT_REFLECT(value) on basic_name. to_key's generic arm dispatches floating-point / integral / enum and
   // otherwise REFLECTS (key_utils.hpp:302-325) -- it never consults operator<<, so SYSLIB_SERIALIZE does not help
   // it. An unreflected basic_name yields field_count 0 and writes a ZERO-BYTE key, silently. Every other case in
   // this file exercises the TYPE (packing, literals, the 2^42 floor, prefix grouping) and would still pass with the
   // reflection deleted; this one would not.
   //
   // to_key has no callers today -- key_utils.hpp is included by nothing, and both kv::table and multi_index encode
   // through kv_utils.hpp's be_key_stream -- so the reflection is defensive. This test is what keeps it correct for
   // the day something does reach it.
   sysio::slug_name code{"LIQSOL"};
   char buf[16] = {};
   sysio::datastream<char*> ds(buf, sizeof(buf));
   sysio::to_key(code, ds);
   CHECK_EQUAL( ds.tellp(), 8 )

   // Big-endian, so byte order matches value order -- the property prefix grouping depends on.
   uint64_t be = 0;
   for (int i = 0; i < 8; ++i) be = (be << 8) | static_cast<unsigned char>(buf[i]);
   CHECK_EQUAL( be, code.value )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   SYSIO_TEST(slug_name_byte_identity_with_the_host)
   SYSIO_TEST(slug_name_round_trips_canonical_spellings)
   SYSIO_TEST(slug_name_must_start_with_a_letter)
   SYSIO_TEST(slug_name_literals_are_compile_time)
   SYSIO_TEST(slug_name_canonical_values_are_at_or_above_the_2_42_floor)
   SYSIO_TEST(slug_name_groups_shared_prefixes_in_the_high_bits)
   SYSIO_TEST(slug_name_to_key_writes_eight_bytes)
   return has_failed();
}
