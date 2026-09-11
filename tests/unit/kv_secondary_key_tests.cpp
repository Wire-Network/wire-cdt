/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  A secondary index is a byte-ordered map: kv_idx_lower_bound hands the encoded key to the
 *  chain, which compares it with memcmp. So the ENCODING carries the order, not the C++ type,
 *  and the property under test is that for values in ascending order the encodings are in
 *  ascending memcmp order.
 *
 *  multi_index supports exactly the five types upstream does, because it exists to carry an
 *  Antelope contract over unchanged and a port cannot arrive with anything else -- upstream's
 *  backend is five db_idx* intrinsic families. Each has an order-preserving encoder; a sixth
 *  type is refused at compile time rather than encoded by a generic pack(), which writes
 *  integers in native little-endian and would iterate wrong while find() still matched.
 *  tests/toolchain/compile-fail/mi_secondary_key_type.cpp pins that refusal.
 *
 *  For a wider key, kv::table's kv::index encodes through be_key_stream. That path is covered
 *  by kv_table_tests and by wire-sysio's get_table_tests.
 */

#include <sysio/tester.hpp>
#include <sysio/multi_index.hpp>

#include <cstring>
#include <limits>
#include <string>
#include <utility>

using namespace sysio;

namespace kvdetail = _kv_multi_index_detail;

// memcmp over the encodings must agree with < over the values.
template<typename T, size_t N>
static void check_ascending(const char* label, const T (&values)[N]) {
   for (size_t i = 0; i + 1 < N; ++i) {
      const auto a = kvdetail::encode_secondary(values[i]);
      const auto b = kvdetail::encode_secondary(values[i + 1]);
      const bool same_width = a.size() == b.size();
      if (!same_width)
         sysio::print("encoding width differs: ", label, " at ", std::to_string(i), "\n");
      CHECK_EQUAL(same_width, true)
      if (!same_width) continue;
      const bool ordered = std::memcmp(a.data(), b.data(), a.size()) < 0;
      if (!ordered)
         sysio::print("out of order: ", label, " at ", std::to_string(i), "\n");
      CHECK_EQUAL(ordered, true)
   }
}

template<typename T, size_t N>
static void check_bytes(const char* label, const T& value, const unsigned char (&expect)[N]) {
   const auto e = kvdetail::encode_secondary(value);
   const bool sized = e.size() == N;
   if (!sized) sysio::print("encoding width wrong: ", label, "\n");
   CHECK_EQUAL(sized, true)
   if (!sized) return;
   const bool match = std::memcmp(e.data(), expect, N) == 0;
   if (!match) sysio::print("encoding bytes wrong: ", label, "\n");
   CHECK_EQUAL(match, true)
}

static checksum256 cs_from_bytes(const unsigned char (&b)[32]) {
   checksum256 v;
   datastream<const char*> ds(reinterpret_cast<const char*>(b), 32);
   ds >> v;
   return v;
}

// Every encoder must also write exactly sizeof(T) bytes: the reverse-iteration sentinel in
// secondary_index_view::operator-- sizes its all-0xFF bound from sizeof, so an encoding of a
// different width would leave that bound in the wrong place.
template<typename T>
static void check_width(const char* label, const T& value) {
   const auto e = kvdetail::encode_secondary(value);
   const bool exact = e.size() == sizeof(T);
   if (!exact) sysio::print("encoded width is not sizeof: ", label, "\n");
   CHECK_EQUAL(exact, true)
}

SYSIO_TEST_BEGIN(secondary_key_order_integral)
   const uint64_t u64[] = {0, 1, 0xFFFFULL, 0x10000ULL, 0x7FFFFFFFFFFFFFFFULL,
                           0x8000000000000000ULL, 0xFFFFFFFFFFFFFFFFULL};
   const uint128_t u128[] = {
      0,
      1,
      static_cast<uint128_t>(0xFFFFFFFFFFFFFFFFULL),
      static_cast<uint128_t>(1) << 64,
      ~static_cast<uint128_t>(0)
   };
   check_ascending("uint64_t", u64);
   check_ascending("uint128_t", u128);
   check_width("uint64_t", u64[0]);
   check_width("uint128_t", u128[0]);
SYSIO_TEST_END

// The float encoders flip the whole word for negatives and just the sign bit for
// non-negatives, so the two halves of the number line join up in the right order.
SYSIO_TEST_BEGIN(secondary_key_order_floating)
   const double d[] = {-1e300, -1.0, -0.5, 0.0, 0.5, 1.0, 1e300};
   check_ascending("double", d);
   check_width("double", d[0]);
   check_width("long double", 1.0L);

   // long double is IEEE binary128 on wasm32, which is the target contracts compile for and
   // what the encoder's byte reversal assumes. This file also builds natively, where it is the
   // x87 80-bit format padded to 16 bytes -- the padding reverses to the front and the encoding
   // does not order. That is how the encoder has always behaved; the guard keeps the native run
   // honest rather than asserting something false.
   if constexpr (std::numeric_limits<long double>::is_iec559
                 && std::numeric_limits<long double>::digits == 113) {
      const long double ld[] = {-1e300L, -1.0L, -0.5L, 0.0L, 0.5L, 1.0L, 1e300L};
      check_ascending("long double", ld);
   }
SYSIO_TEST_END

SYSIO_TEST_BEGIN(secondary_key_order_checksum256)
   const unsigned char lo[32]  = {0x00};
   const unsigned char mid[32] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01};
   const unsigned char hi[32]  = {0x01};
   const checksum256 c[] = {cs_from_bytes(lo), cs_from_bytes(mid), cs_from_bytes(hi)};
   check_ascending("checksum256", c);
   check_width("checksum256", c[0]);
   // The encoding is the value's own big-endian bytes, unchanged. checksum256 used to reach
   // this through the generic pack() path; it now has an explicit overload, and this pins that
   // the bytes did not move.
   check_bytes("checksum256 bytes", c[1], mid);
SYSIO_TEST_END

// Byte-exactness for the three types whose encoding is the same on every host, so a future
// change to any of them has to be deliberate: each is a stored key layout. long double is
// absent on purpose -- its bytes depend on the float format, and this file builds natively
// (x87 80-bit) while contracts compile for wasm32 (IEEE binary128). What holds for it here
// is the width check above; its ordering is pinned on the wasm side by the s1secord
// integration action.
SYSIO_TEST_BEGIN(secondary_key_bytes_unchanged)
   const unsigned char u64_be[8] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF};
   check_bytes("uint64_t", static_cast<uint64_t>(0x0123456789ABCDEFULL), u64_be);

   const unsigned char u128_be[16] = {0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
                                      0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10};
   const uint128_t u128 = (static_cast<uint128_t>(0x0123456789ABCDEFULL) << 64)
                        | static_cast<uint128_t>(0xFEDCBA9876543210ULL);
   check_bytes("uint128_t", u128, u128_be);

   // 1.0 is 0x3FF0000000000000; non-negative, so only the sign bit flips.
   const unsigned char one_be[8] = {0xBF, 0xF0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
   check_bytes("double", 1.0, one_be);

   // -1.0 is 0xBFF0000000000000; negative, so every bit flips.
   const unsigned char neg_one_be[8] = {0x40, 0x0F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
   check_bytes("double negative", -1.0, neg_one_be);
SYSIO_TEST_END

// The static_assert in secondary_index_view fires only when the view is instantiated, and
// nothing above does that. Forcing the type to completion here proves the five are accepted
// through get_index<>(), so a future narrowing breaks this file rather than a contract. No
// intrinsic is called: the table is never constructed, only named.
namespace {

struct idx_row {
   uint64_t    id = 0;
   uint64_t    u64 = 0;
   uint128_t   u128 = 0;
   double      dbl = 0;
   long double ldbl = 0;
   checksum256 hash;

   uint64_t    primary_key() const { return id; }
   uint64_t    by_u64()      const { return u64; }
   uint128_t   by_u128()     const { return u128; }
   double      by_double()   const { return dbl; }
   long double by_ldouble()  const { return ldbl; }
   checksum256 by_hash()     const { return hash; }
};

// A hand-written functor extractor, with no result_type typedef. Upstream derives the key
// type by calling the extractor -- std::decay<decltype(Extractor()(nullptr))> -- so this
// shape compiles on an Antelope chain, and a contract using it has to build here too.
// const_mem_fun supplies result_type in both projects, so the common case is unaffected;
// this pins the uncommon one.
struct by_plain_functor {
   uint64_t operator()(const idx_row& r) const { return r.u64; }
   uint64_t operator()(const idx_row* r) const { return r->u64; }
};

using idx_table = sysio::kv_multi_index<"ordtbl"_n, idx_row,
   sysio::indexed_by<"byint"_n,  sysio::const_mem_fun<idx_row, uint64_t,    &idx_row::by_u64>>,
   sysio::indexed_by<"bybig"_n, sysio::const_mem_fun<idx_row, uint128_t,   &idx_row::by_u128>>,
   sysio::indexed_by<"bydbl"_n,  sysio::const_mem_fun<idx_row, double,      &idx_row::by_double>>,
   sysio::indexed_by<"byldbl"_n, sysio::const_mem_fun<idx_row, long double, &idx_row::by_ldouble>>,
   sysio::indexed_by<"byhash"_n, sysio::const_mem_fun<idx_row, checksum256, &idx_row::by_hash>>,
   sysio::indexed_by<"byfunc"_n, by_plain_functor>>;

template<sysio::name::raw N>
constexpr bool view_is_complete() {
   using view = decltype(std::declval<const idx_table&>().template get_index<N>());
   return sizeof(view) > 0;  // needs a complete type, so its static_asserts run
}

static_assert(view_is_complete<"byint"_n>(),  "uint64_t secondary key rejected");
static_assert(view_is_complete<"bybig"_n>(), "uint128_t secondary key rejected");
static_assert(view_is_complete<"bydbl"_n>(),  "double secondary key rejected");
static_assert(view_is_complete<"byldbl"_n>(), "long double secondary key rejected");
static_assert(view_is_complete<"byhash"_n>(), "checksum256 secondary key rejected");
static_assert(view_is_complete<"byfunc"_n>(),
              "a functor extractor without result_type was rejected; upstream accepts it");

} // namespace

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(secondary_key_order_integral);
   SYSIO_TEST(secondary_key_order_floating);
   SYSIO_TEST(secondary_key_order_checksum256);
   SYSIO_TEST(secondary_key_bytes_unchanged);
   return has_failed();
}
