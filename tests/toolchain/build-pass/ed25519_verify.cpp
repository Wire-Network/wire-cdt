/// Compile both Ed25519 extension APIs together and resolve their unqualified calls.
#include <sysio/crypto_ext.h>
#include <sysio/crypto_ext.hpp>
#include <type_traits>

using namespace sysio;
using verify_function = int32_t (*)(std::span<const char>, std::span<const char>, std::span<const char>);
static_assert(std::is_same_v<decltype(&sysio::ed25519_verify), verify_function>);
using c_verify_function = int32_t (*)(const char*, uint32_t, const char*, uint32_t, const char*, uint32_t);
static_assert(std::is_same_v<decltype(&::ed25519_verify), c_verify_function>);

/// Dynamic arguments retain both API calls and exercise their shared host import.
extern "C" void apply(uint64_t signature, uint64_t key, uint64_t message) {
   constexpr uint32_t signature_size = 64;
   constexpr uint32_t key_size = 32;
   constexpr uint32_t message_size = 1;
   const auto* sig = reinterpret_cast<const char*>(static_cast<uint32_t>(signature));
   const auto* pub = reinterpret_cast<const char*>(static_cast<uint32_t>(key));
   const auto* msg = reinterpret_cast<const char*>(static_cast<uint32_t>(message));
   ed25519_verify({sig, signature_size}, {pub, key_size}, {msg, message_size});
   ed25519_verify(sig, signature_size, pub, key_size, msg, message_size);
}
