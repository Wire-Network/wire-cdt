/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Unit tests for sysio::slug_name — the packed registry-code identifier, an
 *  instantiation of sysio::basic_name over the [A-Z0-9_] alphabet.
 *
 *  basic_name's generic behaviour (both packing directions, the traits concept)
 *  is covered by basic_name_tests.cpp. What is pinned HERE is the slug policy
 *  itself, and above all its BYTE IDENTITY with the host-side fc::slug_name:
 *  the two are separate instantiations in separate toolchains, agreeing only
 *  because their traits agree. The expected values below are what fc produces.
 *  If either side's alphabet, length, terminator rule or packing direction
 *  drifts, these fail — which is the only mechanical guard the cross-language
 *  encoding has.
 */

#include <string>
#include <string_view>

#include <sysio/sysio.hpp>
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
   // These constants are fc::slug_name's output. Do not "fix" a failure by
   // editing them — a mismatch means the two traits have diverged.
   CHECK_EQUAL(slug_name{"A"}.value,        4398046511104ull)
   CHECK_EQUAL(slug_name{"ETH"}.value,     23373212024832ull)
   CHECK_EQUAL(slug_name{"WIRE"}.value,   101792956284928ull)
   CHECK_EQUAL(slug_name{"SOLANA"}.value,  84606581215232ull)
   CHECK_EQUAL(slug_name{"LIQSOL"}.value,  53413609783296ull)
   CHECK_EQUAL(slug_name{"12345678"}.value, 125170908010659ull)
   CHECK_EQUAL(slug_name{"_"}.value,      162727720910848ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_round_trips_canonical_spellings)
   for (const char* s : {"A", "ETH", "WIRE", "SOLANA", "LIQSOL", "12345678", "_"}) {
      CHECK_EQUAL(slug_name{std::string_view{s}}.to_string(), std::string{s})
   }
   CHECK_EQUAL(slug_name{}.to_string(), std::string{})
   CHECK_EQUAL(slug_name{std::string_view{""}}.value, 0ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_literals_are_compile_time)
   static_assert("ETH"_s.value    == 23373212024832ull);
   static_assert("LIQSOL"_s.value == 53413609783296ull);
   static_assert("ETH"_s == slug_name{23373212024832ull});
   CHECK_EQUAL("WIRE"_s.value, 101792956284928ull)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_canonical_values_are_at_or_above_the_2_42_floor)
   // char[0] sits at bits [42..47], so any canonical (non-empty) slug is
   // >= 1<<42 — and conversely every value below the floor has a zero in the
   // char[0] slot and so decodes to the empty string. This is why the host's
   // JSON carrier cannot be string-only.
   constexpr uint64_t floor = 1ull << 42;
   CHECK_EQUAL(slug_name{"A"}.value >= floor, true)
   CHECK_EQUAL(slug_name{"ETH"}.value >= floor, true)
   CHECK_EQUAL(slug_name{uint64_t{7}}.to_string(), std::string{})
   CHECK_EQUAL(slug_name{floor - 1}.to_string(), std::string{})
SYSIO_TEST_END

SYSIO_TEST_BEGIN(slug_name_groups_shared_prefixes_in_the_high_bits)
   // The grouping property slug_name exists for: a shared textual prefix is a
   // shared leading BIT prefix, so prefix-related codes are contiguous in key
   // order. k symbols share the top 6k bits of the 48-bit payload.
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

int main(int argc, char* argv[]) {
   SYSIO_TEST(slug_name_byte_identity_with_the_host)
   SYSIO_TEST(slug_name_round_trips_canonical_spellings)
   SYSIO_TEST(slug_name_literals_are_compile_time)
   SYSIO_TEST(slug_name_canonical_values_are_at_or_above_the_2_42_floor)
   SYSIO_TEST(slug_name_groups_shared_prefixes_in_the_high_bits)
   return has_failed();
}
