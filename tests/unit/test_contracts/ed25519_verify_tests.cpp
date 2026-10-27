#include <sysio/crypto_ext.hpp>
#include <sysio/sysio.hpp>
#include <vector>

/// Action arguments, converted to non-owning views after action deserialization.
struct ed25519_input {
   std::vector<char> signature;
   std::vector<char> public_key;
   std::vector<char> message;
};

/// Deployable fixture for the C++ span API and the WASM batch helper.
class [[sysio::contract]] ed25519_verify_tests : public sysio::contract {
public:
   using contract::contract;

   /// Ordinary verification failure is an observable return code, not an action failure.
   [[sysio::action]]
   void verify(const ed25519_input& entry, int32_t expected) {
      sysio::check(sysio::ed25519_verify(entry.signature, entry.public_key, entry.message) == expected,
                    "unexpected Ed25519 verification result");
   }

   /// Borrow existing buffers: only the views are assembled, never a packed copy of their contents.
   [[sysio::action]]
   void batch(const std::vector<ed25519_input>& entries, int32_t expected) {
      std::vector<sysio::ed25519_signature_view> views;
      views.reserve(entries.size());
      for (const auto& entry : entries)
         views.push_back({entry.signature, entry.public_key, entry.message});
      sysio::check(sysio::ed25519_verify_batch(views) == expected,
                    "unexpected Ed25519 batch result");
   }
};
