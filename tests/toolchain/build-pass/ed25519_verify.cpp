/// Pin the C++ API and exact WASM import type: env.ed25519_verify(i32, i32, i32) -> i32.
#include <sysio/crypto.hpp>
#include <type_traits>

using verify_function = bool (*)(const char*, uint32_t, uint32_t);
static_assert(std::is_same_v<decltype(&sysio::ed25519_verify), verify_function>);

/// Dynamic arguments keep the call in the linked artifact for import-signature validation.
extern "C" void apply(uint64_t pointer, uint64_t length, uint64_t count) {
   const auto* entries = reinterpret_cast<const char*>(static_cast<uint32_t>(pointer));
   sysio::ed25519_verify(entries, static_cast<uint32_t>(length), static_cast<uint32_t>(count));
}
