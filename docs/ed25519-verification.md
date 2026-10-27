# Ed25519 verification

Include `<sysio/crypto_ext.hpp>` for the C++ span API:

```cpp
int32_t sysio::ed25519_verify(std::span<const char> signature,
                             std::span<const char> public_key,
                             std::span<const char> message);
```

Include `<sysio/crypto_ext.h>` for the C pointer/length API:

```c
int32_t ed25519_verify(const char* sig, uint32_t sig_len,
                       const char* pub, uint32_t pub_len,
                       const char* msg, uint32_t msg_len);
```

Both APIs call `env.ed25519_verify` once. Pass a **64-byte detached signature**,
a **32-byte public key**, and **0–65,536 unchanged message bytes**. There is no packed
record, hex encoding, prehash, or `sysio::signature` / `sysio::public_key` variant
prefix. Correctly signed empty messages are supported.

The return value is **0 on success and -1 on failure**. Incorrect lengths,
signatures, or keys return -1. Check `result == 0`; treating the result as a
boolean reverses its meaning. Deadline/resource exhaustion and invalid WASM
memory abort the transaction. They are never converted into verification failures.
Context-free actions may use the import.

## Independent signatures in a WASM loop

`sysio::ed25519_verify_batch(std::span<const ed25519_signature_view> entries)`
iterates over borrowed buffers in WASM. It returns 0 only if every signature
verifies, stops at the first failure, and returns -1 for an empty batch.
**Each signature is verified independently.** This is not randomized or cofactored
batch verification, and it does not combine verification equations.

```cpp
#include <sysio/crypto_ext.hpp>
#include <array>

constexpr uint32_t signature_size = 64;
constexpr uint32_t public_key_size = 32;
constexpr int32_t verification_success = 0;

/// Verify raw bytes without allocating or packing a record.
bool verify_message(const char* message, uint32_t message_size,
                    const std::array<char, signature_size>& signature,
                    const std::array<char, public_key_size>& public_key) {
   return sysio::ed25519_verify(signature, public_key, {message, message_size}) == verification_success;
}

/// Verify two signatures over the same message using the caller's original buffers.
bool verify_pair(const char* message, uint32_t message_size,
                 const std::array<char, signature_size>& first_signature,
                 const std::array<char, public_key_size>& first_key,
                 const std::array<char, signature_size>& second_signature,
                 const std::array<char, public_key_size>& second_key) {
   const std::array<sysio::ed25519_signature_view, 2> entries{{
      {first_signature, first_key, {message, message_size}},
      {second_signature, second_key, {message, message_size}}
   }};
   return sysio::ed25519_verify_batch(entries) == verification_success;
}
```

Each view holds three `std::span<const char>` members: `signature`, `public_key`,
and `message`. Arrays and vectors convert directly, including on 64-bit native builds.
The views and referenced buffers must remain readable for the call's duration.
The helper does not allocate, copy, or pack their contents. Repeated messages,
keys, and signatures are permitted. Membership, signer uniqueness, weights,
quorum, source-chain message encoding, and replay protection belong to the caller.

The host performs one size-limited verification per call. It has no verification
loop or explicit `checktime()` calls. The WASM runtime's timer enforces deadlines
as execution resumes between calls. A verification already in progress may finish
after the deadline; the message-size limit bounds that uninterrupted host work.
There is no separate batch-count cap; normal transaction limits bound total work.

## Acceptance and node compatibility

The host uses the pinned libsodium verifier's cofactorless equation and encoding
rules, including `S < L`, rejection of small-order `R` and public keys, canonical
public keys, and comparison of the computed `R` encoding with the signature bytes.
Mixed-order public keys can verify; this API does not require prime-order subgroup
membership. See the [node acceptance rules](https://github.com/Wire-Network/wire-sysio/blob/master/docs/ed25519-verification.md)
for the exact consensus policy.

The node provides a six-argument import, with pointer/length pairs for signature,
public key, and message, returning an `i32` result. It is available from genesis.
Contracts using this API require a node that provides the import. Existing
digest-recovery APIs retain their behavior.

## Tests

`crypto_tests` checks C and C++ function types and borrowed-buffer forwarding, plus
C++ batches of 1/24/68 entries, early exit, empty batches, return codes, and abort propagation.
`build-pass/ed25519_verify_0` includes both `<sysio/crypto_ext.h>` and
`<sysio/crypto_ext.hpp>`, compiles and links unqualified calls to both APIs, and pins
their function types.
`ed25519_verify_tests` deploys a contract and exercises the single-message API and
the actual WASM batch helper against wire-sysio. The node validates the host import
signature when deploying the contract.

```sh
ctest --test-dir build/debug -R '^crypto_tests$' --output-on-failure
build/debug/tools/toolchain-tester/toolchain-tester tests/toolchain \
  --cdt build/debug/bin -t build-pass/ed25519_verify_0
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm-jit
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm-oc
```

The node suite additionally covers frozen acceptance vectors, memory bounds,
already-expired execution, and timeout rollback while verification loops in WASM.
