/// Compile the span API with both crypto headers and unqualified namespace lookup.
#include <sysio/crypto.h>
#include <sysio/crypto.hpp>
#include <sysio/crypto_ext.hpp>
#include <type_traits>

using namespace sysio;
using verify_function = int32_t (*)(std::span<const char>, std::span<const char>, std::span<const char>);
static_assert(std::is_same_v<decltype(&ed25519_verify), verify_function>);

/// Dynamic arguments retain the host call and exercise linking the inline wrapper.
extern "C" void apply(uint64_t signature, uint64_t key, uint64_t message) {
   constexpr uint32_t signature_size = 64;
   constexpr uint32_t key_size = 32;
   constexpr uint32_t message_size = 1;
   ed25519_verify({reinterpret_cast<const char*>(static_cast<uint32_t>(signature)), signature_size},
                  {reinterpret_cast<const char*>(static_cast<uint32_t>(key)), key_size},
                  {reinterpret_cast<const char*>(static_cast<uint32_t>(message)), message_size});
}
