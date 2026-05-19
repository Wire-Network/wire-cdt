/**
 * Unit tests for librt's standalone 128-bit compiler builtins.
 *
 * The implementations in libraries/rt/compiler_builtins.cpp use only
 * uint64_t operations to avoid recursion into themselves (clang lowers
 * `__int128 *=`/`/=`/`%=`/`<<=`/`>>=` to calls back into the very builtin
 * being defined). This test file exercises the public ABI directly and
 * pins behavior on the determinism-relevant edges:
 *
 *  - INT128_MIN/-1 wrap for signed div/mod (avoids signed-overflow UB)
 *  - shift count >= 128 saturation: 0 for left/logical-right, sign-extension
 *    for arithmetic-right
 *  - divide-by-zero assertion via sysio_assert("divide by zero")
 *
 * Built with `add_native_executable` so the linker pulls libnative_rt's
 * implementations (the same source compiled for the host); CDT's native
 * link line does not include compiler-rt, so no other __multi3 / __divti3
 * etc. is in scope to shadow the symbols under test.
 */

#include <cstdint>
#include <cstring>
#include <limits>

#include <sysio/tester.hpp>

extern "C" {
   void __multi3 (__int128&, uint64_t, uint64_t, uint64_t, uint64_t);
   void __divti3 (__int128&, uint64_t, uint64_t, uint64_t, uint64_t);
   void __udivti3(unsigned __int128&, uint64_t, uint64_t, uint64_t, uint64_t);
   void __modti3 (__int128&, uint64_t, uint64_t, uint64_t, uint64_t);
   void __umodti3(unsigned __int128&, uint64_t, uint64_t, uint64_t, uint64_t);
   void __ashlti3(__int128&, uint64_t, uint64_t, uint32_t);
   void __ashrti3(__int128&, uint64_t, uint64_t, uint32_t);
   void __lshlti3(__int128&, uint64_t, uint64_t, uint32_t);
   void __lshrti3(__int128&, uint64_t, uint64_t, uint32_t);
}

namespace {
   using s128 = __int128;
   using u128 = unsigned __int128;

   constexpr s128 int128_min() {
      return -(static_cast<s128>(static_cast<u128>(1) << 126))
             - (static_cast<s128>(static_cast<u128>(1) << 126));
   }
   constexpr u128 uint128_max() { return ~static_cast<u128>(0); }

   inline uint64_t lo (s128 v) { return static_cast<uint64_t>(static_cast<u128>(v)); }
   inline uint64_t hi (s128 v) { return static_cast<uint64_t>(static_cast<u128>(v) >> 64); }
   inline uint64_t ulo(u128 v) { return static_cast<uint64_t>(v); }
   inline uint64_t uhi(u128 v) { return static_cast<uint64_t>(v >> 64); }

   // Build a u128 from (lo, hi) using only a constant-distance shift and OR --
   // no u128 `/ % *`, so this does not lower to a __udivti3/__multi3 call that
   // would collide with libnative_rt's custom signatures (see note above
   // udivti3_basic). Expected values for the div/mod tests are precomputed
   // literals fed through this.
   inline u128 mk(uint64_t lo_, uint64_t hi_) {
      return (static_cast<u128>(hi_) << 64) | static_cast<u128>(lo_);
   }
}

// =====================================================================
// __multi3 -- truncated 128-bit multiplication
// =====================================================================
SYSIO_TEST_BEGIN(multi3_basic)
   s128 r = 0;
   // basic positive: 100 * 30 = 3000
   __multi3(r, 100, 0, 30, 0);
   CHECK_EQUAL(r, static_cast<s128>(3000))
   // negative: -30 * 100 = -3000
   __multi3(r, lo(-30), hi(-30), 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(-3000))
   // both negative: -30 * -30 = 900
   __multi3(r, lo(-30), hi(-30), lo(-30), hi(-30));
   CHECK_EQUAL(r, static_cast<s128>(900))
   // identity: 1 * x = x
   __multi3(r, 1, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(100))
   // zero
   __multi3(r, 0, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(0))
   // U64 carry: (1 << 63) * 2 == 1 << 64
   __multi3(r, static_cast<uint64_t>(1) << 63, 0, 2, 0);
   CHECK_EQUAL(static_cast<u128>(r), (static_cast<u128>(1) << 64))
   // 64x64 -> 128 product staying inside 128: UINT64_MAX * UINT64_MAX
   __multi3(r, 0xFFFFFFFFFFFFFFFFULL, 0, 0xFFFFFFFFFFFFFFFFULL, 0);
   // (2^64 - 1)^2 = 2^128 - 2*2^64 + 1
   //               = 0xFFFFFFFFFFFFFFFE_0000000000000001
   uint64_t expected_lo = 0x0000000000000001ULL;
   uint64_t expected_hi = 0xFFFFFFFFFFFFFFFEULL;
   u128 expected = (static_cast<u128>(expected_hi) << 64) | expected_lo;
   CHECK_EQUAL(static_cast<u128>(r), expected)
SYSIO_TEST_END

// =====================================================================
// __divti3 / __udivti3 / __modti3 / __umodti3 -- integer division
// =====================================================================
SYSIO_TEST_BEGIN(divti3_basic)
   s128 r = 0;
   __divti3(r, 100, 0, 30, 0);
   CHECK_EQUAL(r, static_cast<s128>(3))
   // negative dividend
   __divti3(r, lo(-100), hi(-100), 30, 0);
   CHECK_EQUAL(r, static_cast<s128>(-3))
   // negative divisor
   __divti3(r, 100, 0, lo(-30), hi(-30));
   CHECK_EQUAL(r, static_cast<s128>(-3))
   // both negative
   __divti3(r, lo(-100), hi(-100), lo(-30), hi(-30));
   CHECK_EQUAL(r, static_cast<s128>(3))
   // dividend < divisor truncates to 0
   __divti3(r, 30, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(0))
   // identity
   __divti3(r, 100, 0, 1, 0);
   CHECK_EQUAL(r, static_cast<s128>(100))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(divti3_int128_min_wrap)
   // INT128_MIN / -1 is signed-overflow UB; librt force-wraps to INT128_MIN.
   s128 r = 0;
   __divti3(r, lo(int128_min()), hi(int128_min()),
               0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
   CHECK_EQUAL(r, int128_min())
SYSIO_TEST_END

// Note: this test must not perform u128/int operations in expected-value
// expressions -- clang lowers those to __udivti3 calls expecting the
// compiler-rt standard signature `(u128, u128) -> u128`, which collides
// with libnative_rt's custom signature `(u128&, 4*u64) -> void`. Use
// precomputed literals instead.
SYSIO_TEST_BEGIN(udivti3_basic)
   u128 r = 0;
   __udivti3(r, 100, 0, 30, 0);
   CHECK_EQUAL(r, static_cast<u128>(3))
   // (2^128 - 30) / 100 == 0x028F5C28F5C28F5C28F5C28F5C28F5C2
   u128 expected = (static_cast<u128>(0x028F5C28F5C28F5CULL) << 64)
                 |  static_cast<u128>(0x28F5C28F5C28F5C2ULL);
   __udivti3(r, lo(-30), hi(-30), 100, 0);
   CHECK_EQUAL(r, expected)
   // dividend smaller than divisor
   __udivti3(r, 30, 0, lo(-100), hi(-100));
   CHECK_EQUAL(r, static_cast<u128>(0))
   // identity
   __udivti3(r, 100, 0, 1, 0);
   CHECK_EQUAL(r, static_cast<u128>(100))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(modti3_basic)
   s128 r = 0;
   __modti3(r, 100, 0, 30, 0);
   CHECK_EQUAL(r, static_cast<s128>(10))
   // negative dividend: result has sign of dividend
   __modti3(r, lo(-30), hi(-30), 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(-30))
   // 30 % -100: 30 - 0 * (-100) = 30
   __modti3(r, 30, 0, lo(-100), hi(-100));
   CHECK_EQUAL(r, static_cast<s128>(30))
   // 100 % -100 = 0
   __modti3(r, 100, 0, lo(-100), hi(-100));
   CHECK_EQUAL(r, static_cast<s128>(0))
   // x % x = 0
   __modti3(r, 100, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<s128>(0))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(modti3_int128_min_wrap)
   // INT128_MIN % -1 -- mathematical result is 0; librt force-handles the
   // signed-overflow UB case.
   s128 r = 1;  // poison
   __modti3(r, lo(int128_min()), hi(int128_min()),
               0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL);
   CHECK_EQUAL(r, static_cast<s128>(0))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(umodti3_basic)
   u128 r = 0;
   __umodti3(r, 100, 0, 30, 0);
   CHECK_EQUAL(r, static_cast<u128>(10))
   // (2^128 - 30) % 100 == 26
   __umodti3(r, lo(-30), hi(-30), 100, 0);
   CHECK_EQUAL(r, static_cast<u128>(26))
   // 30 % (2^128 - 100) == 30 (dividend smaller than divisor)
   __umodti3(r, 30, 0, lo(-100), hi(-100));
   CHECK_EQUAL(r, static_cast<u128>(30))
   // x % x = 0
   __umodti3(r, 100, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<u128>(0))
   // 0 % x = 0
   __umodti3(r, 0, 0, 100, 0);
   CHECK_EQUAL(r, static_cast<u128>(0))
SYSIO_TEST_END

// =====================================================================
// udivmod128 small-divisor fast path: 128-bit dividend / (<= 32-bit
// divisor). Exercises the four-32-bit-digit long division, its
// 0xFFFFFFFF upper boundary, and the just-over-boundary divisor
// (v == 2^32) that must fall through to the 128-iteration slow loop.
// Vectors precomputed in Python; full-128 dividends so the 64/64 fast
// path is skipped and the new path (or the slow loop) is taken.
// =====================================================================
SYSIO_TEST_BEGIN(udivti3_small_divisor_fastpath)
   u128 r = 0;
   // UINT128_MAX / 7 -- non-zero remainder carried across all four digits.
   __udivti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 7, 0);
   CHECK_EQUAL(r, mk(0x4924924924924924ULL, 0x2492492492492492ULL))
   // UINT128_MAX / 0xFFFFFFFF -- largest divisor still on the fast path.
   __udivti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFULL, 0);
   CHECK_EQUAL(r, mk(0x0000000100000001ULL, 0x0000000100000001ULL))
   // UINT128_MAX / 1 -- identity: q == u, r == 0 on the fast path.
   __udivti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 1, 0);
   CHECK_EQUAL(r, mk(0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL))
   // Mixed dividend / 0xDEADBEEF (divisor just under 2^32).
   __udivti3(r, 0xFEDCBA9876543210ULL, 0x0123456789ABCDEFULL, 0xDEADBEEFULL, 0);
   CHECK_EQUAL(r, mk(0x717DCE0520562DA1ULL, 0x00000000014EDB42ULL))
   // v == 2^32: one past the fast-path guard -> 128-iteration slow loop.
   __udivti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x100000000ULL, 0);
   CHECK_EQUAL(r, mk(0xFFFFFFFFFFFFFFFFULL, 0x00000000FFFFFFFFULL))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(umodti3_small_divisor_fastpath)
   u128 r = 0;
   // Remainders for the same vectors; result is always < v <= 2^32-1.
   __umodti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 7, 0);
   CHECK_EQUAL(r, static_cast<u128>(3))
   __umodti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFULL, 0);
   CHECK_EQUAL(r, static_cast<u128>(0))
   __umodti3(r, 0xFEDCBA9876543210ULL, 0x0123456789ABCDEFULL, 0xDEADBEEFULL, 0);
   CHECK_EQUAL(r, static_cast<u128>(0xDC351AC1ULL))
   // 0xFFFF...FFFF_0000...0001 / 3 -> remainder 1 (carry chain ends nonzero).
   __umodti3(r, 0x0000000000000001ULL, 0xFFFFFFFFFFFFFFFFULL, 3, 0);
   CHECK_EQUAL(r, static_cast<u128>(1))
   // v == 2^32: slow-loop remainder still exact.
   __umodti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0x100000000ULL, 0);
   CHECK_EQUAL(r, static_cast<u128>(0xFFFFFFFFULL))
SYSIO_TEST_END

// =====================================================================
// __ashlti3 / __lshlti3 -- left shift (same bit pattern)
// =====================================================================
SYSIO_TEST_BEGIN(shift_left_basic)
   s128 r = 0;
   // 1 << 0 == 1
   __ashlti3(r, 1, 0, 0);
   CHECK_EQUAL(r, static_cast<s128>(1))
   // 1 << 31 == 2^31
   __ashlti3(r, 1, 0, 31);
   CHECK_EQUAL(r, static_cast<s128>(1) << 31)
   // 1 << 63 == 2^63 (top of low half)
   __ashlti3(r, 1, 0, 63);
   CHECK_EQUAL(r, static_cast<s128>(1) << 63)
   // 1 << 64 == 2^64 (bottom of high half)
   __ashlti3(r, 1, 0, 64);
   CHECK_EQUAL(static_cast<u128>(r), static_cast<u128>(1) << 64)
   // 1 << 127 == sign bit (INT128_MIN bit pattern)
   __ashlti3(r, 1, 0, 127);
   CHECK_EQUAL(static_cast<u128>(r), static_cast<u128>(1) << 127)
   // mid-range cross-half shift
   __ashlti3(r, 0xFFFFFFFFFFFFFFFFULL, 0, 32);
   // 0x...FFFFFFFFFFFFFFFF << 32: low half = 0xFFFFFFFF00000000, hi = 0xFFFFFFFF
   uint64_t exp_lo = 0xFFFFFFFF00000000ULL;
   uint64_t exp_hi = 0x00000000FFFFFFFFULL;
   u128 exp = (static_cast<u128>(exp_hi) << 64) | exp_lo;
   CHECK_EQUAL(static_cast<u128>(r), exp)

   // __lshlti3 is the same bit pattern; spot-check
   __lshlti3(r, 1, 0, 64);
   CHECK_EQUAL(static_cast<u128>(r), static_cast<u128>(1) << 64)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(shift_left_oob_saturates_to_zero)
   s128 r = static_cast<s128>(0xDEADBEEFCAFEBABEULL); // poison
   __ashlti3(r, 1, 0, 128);
   CHECK_EQUAL(r, static_cast<s128>(0))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashlti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 200);
   CHECK_EQUAL(r, static_cast<s128>(0))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashlti3(r, 1, 0, 0xFFFFFFFFu);
   CHECK_EQUAL(r, static_cast<s128>(0))
   // __lshlti3 same saturation
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __lshlti3(r, 1, 0, 128);
   CHECK_EQUAL(r, static_cast<s128>(0))
SYSIO_TEST_END

// =====================================================================
// __lshrti3 -- logical right shift (zero-fill from high end)
// =====================================================================
SYSIO_TEST_BEGIN(lshrti3_basic)
   s128 r = 0;
   // (1 << 127) >> 0 == 1 << 127
   __lshrti3(r, 0, static_cast<uint64_t>(1) << 63, 0);
   CHECK_EQUAL(static_cast<u128>(r), static_cast<u128>(1) << 127)
   // (1 << 127) >> 64 == 2^63
   __lshrti3(r, 0, static_cast<uint64_t>(1) << 63, 64);
   CHECK_EQUAL(static_cast<u128>(r), static_cast<u128>(1) << 63)
   // (1 << 127) >> 127 == 1
   __lshrti3(r, 0, static_cast<uint64_t>(1) << 63, 127);
   CHECK_EQUAL(r, static_cast<s128>(1))
   // -1 (all bits set) >> 1 == 0x7FFF...FFFF (top bit cleared, rest set)
   __lshrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 1);
   CHECK_EQUAL(static_cast<u128>(r), uint128_max() >> 1)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(lshrti3_oob_saturates_to_zero)
   s128 r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __lshrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 128);
   CHECK_EQUAL(r, static_cast<s128>(0))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __lshrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFu);
   CHECK_EQUAL(r, static_cast<s128>(0))
SYSIO_TEST_END

// =====================================================================
// __ashrti3 -- arithmetic right shift (sign-extending fill)
// =====================================================================
SYSIO_TEST_BEGIN(ashrti3_positive)
   s128 r = 0;
   __ashrti3(r, 100, 0, 0);
   CHECK_EQUAL(r, static_cast<s128>(100))
   __ashrti3(r, 100, 0, 1);
   CHECK_EQUAL(r, static_cast<s128>(50))
   __ashrti3(r, 100, 0, 7);
   CHECK_EQUAL(r, static_cast<s128>(0))
   __ashrti3(r, 0, 0, 64);
   CHECK_EQUAL(r, static_cast<s128>(0))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(ashrti3_negative_sign_extends)
   s128 r = 0;
   // INT128_MIN >> 0 == INT128_MIN
   __ashrti3(r, lo(int128_min()), hi(int128_min()), 0);
   CHECK_EQUAL(r, int128_min())
   // INT128_MIN >> 1 == INT128_MIN / 2
   __ashrti3(r, lo(int128_min()), hi(int128_min()), 1);
   CHECK_EQUAL(r, int128_min() / 2)
   // INT128_MIN >> 64 == -2^63
   __ashrti3(r, lo(int128_min()), hi(int128_min()), 64);
   CHECK_EQUAL(r, -(static_cast<s128>(1) << 63))
   // INT128_MIN >> 127 == -1 (all bits set by sign extension)
   __ashrti3(r, lo(int128_min()), hi(int128_min()), 127);
   CHECK_EQUAL(r, static_cast<s128>(-1))
   // -1 >> any < 128 == -1
   __ashrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0);
   CHECK_EQUAL(r, static_cast<s128>(-1))
   __ashrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 64);
   CHECK_EQUAL(r, static_cast<s128>(-1))
   __ashrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 127);
   CHECK_EQUAL(r, static_cast<s128>(-1))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(ashrti3_oob_saturates_to_sign)
   // shift count >= 128: 0 for non-negative, -1 for negative
   s128 r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashrti3(r, 100, 0, 128);
   CHECK_EQUAL(r, static_cast<s128>(0))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashrti3(r, 100, 0, 0xFFFFFFFFu);
   CHECK_EQUAL(r, static_cast<s128>(0))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashrti3(r, lo(int128_min()), hi(int128_min()), 128);
   CHECK_EQUAL(r, static_cast<s128>(-1))
   r = static_cast<s128>(0xDEADBEEFCAFEBABEULL);
   __ashrti3(r, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFu);
   CHECK_EQUAL(r, static_cast<s128>(-1))
SYSIO_TEST_END

// =====================================================================
// Divide-by-zero -- librt fires sysio_assert("divide by zero").
// =====================================================================
SYSIO_TEST_BEGIN(divti3_by_zero_asserts)
   CHECK_ASSERT( "divide by zero", (
      []() { s128 r = 0; __divti3(r, 100, 0, 0, 0); return r; }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(udivti3_by_zero_asserts)
   CHECK_ASSERT( "divide by zero", (
      []() { u128 r = 0; __udivti3(r, 100, 0, 0, 0); return r; }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(modti3_by_zero_asserts)
   CHECK_ASSERT( "divide by zero", (
      []() { s128 r = 0; __modti3(r, 100, 0, 0, 0); return r; }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(umodti3_by_zero_asserts)
   CHECK_ASSERT( "divide by zero", (
      []() { u128 r = 0; __umodti3(r, 100, 0, 0, 0); return r; }))
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(multi3_basic);
   SYSIO_TEST(divti3_basic);
   SYSIO_TEST(divti3_int128_min_wrap);
   SYSIO_TEST(udivti3_basic);
   SYSIO_TEST(modti3_basic);
   SYSIO_TEST(modti3_int128_min_wrap);
   SYSIO_TEST(umodti3_basic);
   SYSIO_TEST(udivti3_small_divisor_fastpath);
   SYSIO_TEST(umodti3_small_divisor_fastpath);
   SYSIO_TEST(shift_left_basic);
   SYSIO_TEST(shift_left_oob_saturates_to_zero);
   SYSIO_TEST(lshrti3_basic);
   SYSIO_TEST(lshrti3_oob_saturates_to_zero);
   SYSIO_TEST(ashrti3_positive);
   SYSIO_TEST(ashrti3_negative_sign_extends);
   SYSIO_TEST(ashrti3_oob_saturates_to_sign);
   SYSIO_TEST(divti3_by_zero_asserts);
   SYSIO_TEST(udivti3_by_zero_asserts);
   SYSIO_TEST(modti3_by_zero_asserts);
   SYSIO_TEST(umodti3_by_zero_asserts);

   return has_failed();
}
