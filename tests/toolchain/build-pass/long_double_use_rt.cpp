// The positive half of build-fail/long_double_no_use_rt: passing --use-rt must actually LINK.
//
// Only the negative case was covered, and the flag was broken through cdt-cpp for exactly as
// long. GetLdDefaults -- which turns --use-rt into -lsf -- runs only under ONLY_LD, i.e. inside
// cdt-ld. cdt-cpp parses the same options header, so it accepted --use-rt without complaint, but
// never forwarded it to the cdt-ld it invokes. A one-step build of a long double contract failed
// on librt's undefined f128_* symbols, with a diagnostic telling the user to pass the flag they
// had just passed. `cdt-ld --use-rt` on a pre-built object worked, which is what hid it.
//
// Same body as the negative fixture, so the pair differs only by the flag.

#include <sysio/sysio.hpp>

extern "C" {
   void apply(uint64_t a, uint64_t, uint64_t) {
      long double x = static_cast<long double>(a);
      long double y = static_cast<long double>(a + 1);
      long double z = x * y;
      uint64_t result = static_cast<uint64_t>(static_cast<double>(z));
      sysio::check(result != 0, "result was zero");
   }
}
