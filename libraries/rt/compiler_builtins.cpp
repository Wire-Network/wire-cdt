#include "compiler_builtins.hpp"
#include <stdint.h>

// ============================================================================
// 128-bit compiler builtins -- standalone implementations.
//
// These intrinsics MUST NOT use multi-word `__int128` operators (`<<=`, `>>=`,
// `*=`, `/=`, `%=`, `<<`/`>>`/`*`/`/`/`%` on `__int128` with non-constant
// operands). clang lowers each of those to a call to the very builtin we are
// defining -- which would recurse infinitely when this librt is the contract's
// own compiler-rt provider (no host fallback to break the cycle).
//
// Allowed in this file: bitwise ops on `__int128` (AND/OR/XOR/~), comparisons,
// constant-distance shifts (clang inlines `__int128 << 64` etc.), and any
// operation on `uint64_t`. Final result packing uses __builtin_memcpy to write
// 16 bytes into the `__int128&` output without going through `__int128`
// arithmetic.
// ============================================================================

namespace {
   // 64x64 -> 128 unsigned multiplication via 32-bit schoolbook partial
   // products. No multi-word arithmetic involved -- only uint64 multiplies,
   // adds, and shifts by constant 32. Output: low 64 bits in *lo, high 64 in *hi.
   inline void mul64x64_to_128(uint64_t a, uint64_t b, uint64_t* lo, uint64_t* hi) {
      const uint64_t a_lo = a & 0xFFFFFFFFULL;
      const uint64_t a_hi = a >> 32;
      const uint64_t b_lo = b & 0xFFFFFFFFULL;
      const uint64_t b_hi = b >> 32;

      const uint64_t p_ll = a_lo * b_lo;
      const uint64_t p_lh = a_lo * b_hi;
      const uint64_t p_hl = a_hi * b_lo;
      const uint64_t p_hh = a_hi * b_hi;

      // mid bits accumulate the cross terms plus the high half of p_ll.
      const uint64_t mid = (p_ll >> 32) + (p_lh & 0xFFFFFFFFULL) + (p_hl & 0xFFFFFFFFULL);
      *lo = (p_ll & 0xFFFFFFFFULL) | (mid << 32);
      *hi = p_hh + (p_lh >> 32) + (p_hl >> 32) + (mid >> 32);
   }

   // 128-bit two's complement negation, in-place. Only uses uint64 arithmetic.
   inline void neg128(uint64_t* lo, uint64_t* hi) {
      const uint64_t inv_lo = ~*lo;
      const uint64_t inv_hi = ~*hi;
      const uint64_t res_lo = inv_lo + 1;
      const uint64_t carry  = (res_lo == 0) ? 1ULL : 0ULL;
      *lo = res_lo;
      *hi = inv_hi + carry;
   }

   // Unsigned 128-bit long division: u / v -> (q, r). Caller is responsible
   // for ensuring v != 0 (reported via sysio_assert at the public entry).
   // 128 iterations of shift-and-subtract. Slow but obviously correct, no
   // dependency on multi-word builtins.
   inline void udivmod128(uint64_t u_lo, uint64_t u_hi,
                          uint64_t v_lo, uint64_t v_hi,
                          uint64_t* q_lo, uint64_t* q_hi,
                          uint64_t* r_lo, uint64_t* r_hi) {
      // Fast path: 64-bit / 64-bit
      if (v_hi == 0 && u_hi == 0) {
         *q_lo = u_lo / v_lo;
         *q_hi = 0;
         *r_lo = u_lo % v_lo;
         *r_hi = 0;
         return;
      }
      // Fast path: 128-bit / (<= 32-bit divisor). Schoolbook long division
      // with the dividend split into four 32-bit digits, most significant
      // first. The running remainder r satisfies r < v <= 2^32-1 after every
      // `r %= v`, so the next dividend (r << 32) | digit < 2^64 -- it fits a
      // native uint64 and the i64.div_u below is exact (no recursion back into
      // __udivti3). The same bound gives (r << 32) | digit < v * 2^32, so each
      // per-digit quotient is < 2^32 and the (hi << 32) | lo packing is exact.
      // Since `digit < 2^32`, every `(r << 32) | digit` is a disjoint-bit add.
      if (v_hi == 0 && v_lo <= 0xFFFFFFFFULL) {
         const uint64_t v = v_lo;
         uint64_t r = u_hi >> 32;                            // digit 3 (top)
         const uint64_t q3 = r / v;   r %= v;
         r = (r << 32) | (u_hi & 0xFFFFFFFFULL);             // digit 2
         const uint64_t q2 = r / v;   r %= v;
         r = (r << 32) | (u_lo >> 32);                       // digit 1
         const uint64_t q1 = r / v;   r %= v;
         r = (r << 32) | (u_lo & 0xFFFFFFFFULL);             // digit 0 (bottom)
         const uint64_t q0 = r / v;   r %= v;
         *q_hi = (q3 << 32) | q2;
         *q_lo = (q1 << 32) | q0;
         *r_lo = r;                                          // r < v <= 2^32-1
         *r_hi = 0;
         return;
      }
      uint64_t ql = 0, qh = 0;
      uint64_t rl = 0, rh = 0;
      for (int i = 127; i >= 0; --i) {
         // r <<= 1 (shift 128-bit r left by 1)
         rh = (rh << 1) | (rl >> 63);
         rl <<= 1;
         // r |= bit i of u
         if (i >= 64) {
            rl |= (u_hi >> (i - 64)) & 1ULL;
         } else {
            rl |= (u_lo >> i) & 1ULL;
         }
         // if (r >= v) { r -= v; q |= (1 << i); }
         const bool r_ge_v = (rh > v_hi) || (rh == v_hi && rl >= v_lo);
         if (r_ge_v) {
            const uint64_t new_rl = rl - v_lo;
            const uint64_t borrow = (rl < v_lo) ? 1ULL : 0ULL;
            rh = rh - v_hi - borrow;
            rl = new_rl;
            if (i >= 64) qh |= 1ULL << (i - 64);
            else         ql |= 1ULL << i;
         }
      }
      *q_lo = ql; *q_hi = qh;
      *r_lo = rl; *r_hi = rh;
   }

   // Pack (lo, hi) into a 128-bit destination via a 16-byte memcpy. Avoids
   // any __int128 arithmetic at the assignment site.
   inline void store128(void* dst, uint64_t lo, uint64_t hi) {
      uint64_t buf[2] = { lo, hi };
      __builtin_memcpy(dst, buf, 16);
   }
}

extern "C" {
void sysio_assert(int32_t, const char*);

// Left shift -- arithmetic and logical share the bit pattern.
void __ashlti3(__int128& ret, uint64_t lo, uint64_t hi, uint32_t shift) {
   uint64_t r_lo, r_hi;
   if (shift >= 128) {
      r_lo = 0;
      r_hi = 0;
   } else if (shift == 0) {
      r_lo = lo;
      r_hi = hi;
   } else if (shift < 64) {
      r_hi = (hi << shift) | (lo >> (64 - shift));
      r_lo = lo << shift;
   } else { // 64 <= shift < 128
      r_hi = lo << (shift - 64);
      r_lo = 0;
   }
   store128(&ret, r_lo, r_hi);
}

void __lshlti3(__int128& ret, uint64_t lo, uint64_t hi, uint32_t shift) {
   __ashlti3(ret, lo, hi, shift); // same bit pattern; reuse
}

// Logical right shift -- zero-fill from the high end.
void __lshrti3(__int128& ret, uint64_t lo, uint64_t hi, uint32_t shift) {
   uint64_t r_lo, r_hi;
   if (shift >= 128) {
      r_lo = 0;
      r_hi = 0;
   } else if (shift == 0) {
      r_lo = lo;
      r_hi = hi;
   } else if (shift < 64) {
      r_lo = (lo >> shift) | (hi << (64 - shift));
      r_hi = hi >> shift;
   } else { // 64 <= shift < 128
      r_lo = hi >> (shift - 64);
      r_hi = 0;
   }
   store128(&ret, r_lo, r_hi);
}

// Arithmetic right shift -- sign-extend from bit 127 (top bit of hi).
void __ashrti3(__int128& ret, uint64_t lo, uint64_t hi, uint32_t shift) {
   const uint64_t sign_fill = (hi & (1ULL << 63)) ? ~0ULL : 0ULL;
   uint64_t r_lo, r_hi;
   if (shift >= 128) {
      r_lo = sign_fill;
      r_hi = sign_fill;
   } else if (shift == 0) {
      r_lo = lo;
      r_hi = hi;
   } else if (shift < 64) {
      // Logical shift the low half, then OR in the bottom of the high half.
      r_lo = (lo >> shift) | (hi << (64 - shift));
      // Arithmetic shift on hi: cast to signed for sign extension. shift < 64
      // here, so cast and shift are well-defined per C++20 [expr.shift]/3.
      r_hi = static_cast<uint64_t>(static_cast<int64_t>(hi) >> shift);
   } else { // 64 <= shift < 128
      r_lo = static_cast<uint64_t>(static_cast<int64_t>(hi) >> (shift - 64));
      r_hi = sign_fill;
   }
   store128(&ret, r_lo, r_hi);
}

// Truncated 128-bit multiplication: lower 128 bits of the 256-bit product.
//   (la, ha) * (lb, hb) where each pair is (low_64, high_64).
void __multi3(__int128& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb) {
   uint64_t lo, hi;
   mul64x64_to_128(la, lb, &lo, &hi);
   // Cross terms contribute only to the high half (their low 64 bits) because
   // we're truncating to 128 bits; ha*hb falls off the top.
   hi += ha * lb;
   hi += la * hb;
   store128(&ret, lo, hi);
}

// Unsigned 128-bit division.
void __udivti3(unsigned __int128& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb) {
   sysio_assert(lb != 0 || hb != 0, "divide by zero");
   uint64_t q_lo, q_hi, r_lo, r_hi;
   udivmod128(la, ha, lb, hb, &q_lo, &q_hi, &r_lo, &r_hi);
   store128(&ret, q_lo, q_hi);
}

// Unsigned 128-bit modulo.
void __umodti3(unsigned __int128& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb) {
   sysio_assert(lb != 0 || hb != 0, "divide by zero");
   uint64_t q_lo, q_hi, r_lo, r_hi;
   udivmod128(la, ha, lb, hb, &q_lo, &q_hi, &r_lo, &r_hi);
   store128(&ret, r_lo, r_hi);
}

// Signed 128-bit division. Implementation strategy:
//   1. Trap divide-by-zero.
//   2. Force-handle the INT128_MIN/-1 overflow case: result wraps to INT128_MIN.
//   3. Take absolute values in the unsigned domain (avoiding signed-overflow UB
//      in the negation step), divide via udivmod128, then re-apply sign.
void __divti3(__int128& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb) {
   sysio_assert(lb != 0 || hb != 0, "divide by zero");

   // INT128_MIN: lo=0, hi=0x8000000000000000. -1: lo=hi=0xFFFF...F.
   if (la == 0 && ha == 0x8000000000000000ULL &&
       lb == 0xFFFFFFFFFFFFFFFFULL && hb == 0xFFFFFFFFFFFFFFFFULL) {
      store128(&ret, 0, 0x8000000000000000ULL);
      return;
   }

   const bool neg_a = (ha & (1ULL << 63)) != 0;
   const bool neg_b = (hb & (1ULL << 63)) != 0;

   uint64_t ua_lo = la, ua_hi = ha;
   if (neg_a) neg128(&ua_lo, &ua_hi);
   uint64_t ub_lo = lb, ub_hi = hb;
   if (neg_b) neg128(&ub_lo, &ub_hi);

   uint64_t q_lo, q_hi, r_lo, r_hi;
   udivmod128(ua_lo, ua_hi, ub_lo, ub_hi, &q_lo, &q_hi, &r_lo, &r_hi);

   if (neg_a != neg_b) neg128(&q_lo, &q_hi);
   store128(&ret, q_lo, q_hi);
}

// Signed 128-bit modulo. Sign of the result follows the dividend.
void __modti3(__int128& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb) {
   sysio_assert(lb != 0 || hb != 0, "divide by zero");

   // INT128_MIN % -1 -- mathematical result is 0; pin the value here to avoid
   // any UB path inside the udivmod128 negation step.
   if (la == 0 && ha == 0x8000000000000000ULL &&
       lb == 0xFFFFFFFFFFFFFFFFULL && hb == 0xFFFFFFFFFFFFFFFFULL) {
      store128(&ret, 0, 0);
      return;
   }

   const bool neg_a = (ha & (1ULL << 63)) != 0;
   const bool neg_b = (hb & (1ULL << 63)) != 0;

   uint64_t ua_lo = la, ua_hi = ha;
   if (neg_a) neg128(&ua_lo, &ua_hi);
   uint64_t ub_lo = lb, ub_hi = hb;
   if (neg_b) neg128(&ub_lo, &ub_hi);

   uint64_t q_lo, q_hi, r_lo, r_hi;
   udivmod128(ua_lo, ua_hi, ub_lo, ub_hi, &q_lo, &q_hi, &r_lo, &r_hi);

   if (neg_a) neg128(&r_lo, &r_hi);
   store128(&ret, r_lo, r_hi);
}

// arithmetic long double
void __addtf3( float128_t& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   ret = f128_add( a, b );
}
void __subtf3( float128_t& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   ret = f128_sub( a, b );
}
void __multf3( float128_t& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   ret = f128_mul( a, b );
}
void __divtf3( float128_t& ret, uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   ret = f128_div( a, b );
}
void __negtf2( float128_t& ret, uint64_t la, uint64_t ha ) {
   ret = {{ la, (ha ^ (uint64_t)1 << 63) }};
}

// conversion long double
void __extendsftf2( float128_t& ret, float f ) {
   ret = f32_to_f128( to_softfloat32(f) );
}
void __extenddftf2( float128_t& ret, double d ) {
   ret = f64_to_f128( to_softfloat64(d) );
}
double __trunctfdf2( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return from_softfloat64(f128_to_f64( f ));
}
float __trunctfsf2( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return from_softfloat32(f128_to_f32( f ));
}
int32_t __fixtfsi( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return f128_to_i32( f, 0, false );
}
int64_t __fixtfdi( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return f128_to_i64( f, 0, false );
}
void __fixtfti( __int128& ret, uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   ret = ___fixtfti( f );
}
uint32_t __fixunstfsi( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return f128_to_ui32( f, 0, false );
}
uint64_t __fixunstfdi( uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   return f128_to_ui64( f, 0, false );
}
void __fixunstfti( unsigned __int128& ret, uint64_t l, uint64_t h ) {
   float128_t f = {{ l, h }};
   ret = ___fixunstfti( f );
}
void __fixsfti( __int128& ret, float a ) {
   ret = ___fixsfti( to_softfloat32(a).v );
}
void __fixdfti( __int128& ret, double a ) {
   ret = ___fixdfti( to_softfloat64(a).v );
}
void __fixunssfti( unsigned __int128& ret, float a ) {
   ret = ___fixunssfti( to_softfloat32(a).v );
}
void __fixunsdfti( unsigned __int128& ret, double a ) {
   ret = ___fixunsdfti( to_softfloat64(a).v );
}
double __floatsidf( int32_t i ) {
   return from_softfloat64(i32_to_f64(i));
}
void __floatsitf( float128_t& ret, int32_t i ) {
   ret = i32_to_f128(i);
}
void __floatditf( float128_t& ret, uint64_t a ) {
   ret = i64_to_f128( a );
}
void __floatunsitf( float128_t& ret, uint32_t i ) {
   ret = ui32_to_f128(i);
}
void __floatunditf( float128_t& ret, uint64_t a ) {
   ret = ui64_to_f128( a );
}
double __floattidf( uint64_t l, uint64_t h ) {
   uint128_t val = h;
   val <<= 64;
   val |= l;
   return ___floattidf( *(__int128*)&val );
}
double __floatuntidf( uint64_t l, uint64_t h ) {
   uint128_t v = h;
   v <<= 64;
   v |= l;
   return ___floatuntidf( (unsigned __int128)v );
}
int __unordtf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   if ( f128_is_nan(a) || f128_is_nan(b) )
      return 1;
   return 0;
}
int ___cmptf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb, int return_value_if_nan ) {
   float128_t a = {{ la, ha }};
   float128_t b = {{ lb, hb }};
   if ( __unordtf2(la, ha, lb, hb) )
      return return_value_if_nan;
   if ( f128_lt( a, b ) )
      return -1;
   if ( f128_eq( a, b ) )
      return 0;
   return 1;
}
int __eqtf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 1);
}
int __netf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 1);
}
int __getf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, -1);
}
int __gttf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 0);
}
int __letf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 1);
}
int __lttf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 0);
}
int __cmptf2( uint64_t la, uint64_t ha, uint64_t lb, uint64_t hb ) {
   return ___cmptf2(la, ha, lb, hb, 1);
}
}

