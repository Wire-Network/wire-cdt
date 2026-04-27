// Verifies that a contract using long double without --use-rt fails to link
// AND that the cdt-ld diagnostic mentions --use-rt as the resolution.
//
// long double on wasm32 is binary128. clang lowers ops on it to softfloat
// compiler-rt symbols (__multf3, __floatunditf, __trunctfdf2, ...). Those
// symbols live in libsf.a, which is opt-in via --use-rt. Without --use-rt,
// wasm-ld reports them as undefined and cdt-ld appends a hint pointing at
// --use-rt so the resolution is obvious.

#include <sysio/sysio.hpp>

extern "C" {
   void apply(uint64_t a, uint64_t, uint64_t) {
      // Force softfloat-backed ops:
      //   (long double)(uint64_t) -> __floatunditf
      //   long double * long double -> __multf3
      //   (double)(long double)    -> __trunctfdf2
      long double x = static_cast<long double>(a);
      long double y = static_cast<long double>(a + 1);
      long double z = x * y;
      uint64_t result = static_cast<uint64_t>(static_cast<double>(z));
      sysio::check(result != 0, "result was zero");
   }
}
