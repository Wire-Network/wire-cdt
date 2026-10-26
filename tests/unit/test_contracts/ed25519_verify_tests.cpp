#include <sysio/crypto.hpp>
#include <sysio/sysio.hpp>

#include <vector>

/// Exercise the C header from a separately compiled C translation unit.
extern "C" bool verify_ed25519_c(const char* entries, uint32_t entries_len, uint32_t count);

/// Deployable fixture for the installed CDT C and C++ Ed25519 APIs.
class [[sysio::contract]] ed25519_verify_tests : public sysio::contract {
 public:
   using contract::contract;

   /// Check the exact host boolean without treating ordinary invalid signatures as action failures.
   [[sysio::action]]
   void verify(const std::vector<char>& entries, uint32_t count, bool expected, bool use_c) {
      constexpr auto unexpected_result = "unexpected Ed25519 verification result";
      const bool actual = use_c ? verify_ed25519_c(entries.data(), entries.size(), count)
                                : sysio::ed25519_verify(entries.data(), entries.size(), count);
      sysio::check(actual == expected, unexpected_result);
   }
};
