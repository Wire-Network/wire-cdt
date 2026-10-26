# Ed25519 verification

Include `<sysio/crypto.hpp>` for the C++ API or `<sysio/crypto.h>` for C:

```cpp
bool sysio::ed25519_verify(const char* entries, uint32_t entries_len, uint32_t count);
```

The C declaration has the same parameters and return type, without the namespace.
Both call the single `env.ed25519_verify` host intrinsic. The C++ wrapper forwards
one call without copying, unpacking, or looping over the batch. The host verifies
standard Ed25519 signatures over the exact message bytes: no hex encoding,
Ed25519ph prehashing, or `sysio::signature` / `sysio::public_key` variant prefix.

Each record contains these fields without padding:

| Field | Bytes |
| --- | --- |
| Message length, unsigned little-endian | 4 |
| Detached signature | 64 |
| Raw public key | 32 |
| Message | Length specified above |

Concatenate records for a batch and pass its exact byte length and record count.
Use a batch of one for a single signature. `sysio::pack(std::vector<...>)` is not
this format: its length prefixes and container serialization differ.

The host returns `true` only if every entry verifies and the buffer is consumed
exactly. It stops at the first invalid record and returns `false` for invalid
signatures or keys, malformed/truncated input, inconsistent counts, trailing bytes,
empty batches, or messages longer than **65,536 bytes each**. Correctly signed empty
messages and repeated messages or keys are permitted. Validator membership,
uniqueness, quorum, and replay protection remain contract responsibilities.

There is no separate batch-count cap. Normal transaction limits bound total work.
The host checks transaction time on entry and immediately before and after each
verification, including unsuccessful verification. Enforcement is cooperative:
a size-limited verification can finish after a deadline before the next check
aborts. Deadline/resource exhaustion and invalid WASM memory abort the transaction;
they do not become a `false` verification result.

## Packing a single message

```cpp
#include <sysio/crypto.hpp>
#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

constexpr uint32_t ed25519_max_message_size = 65'536;
constexpr size_t ed25519_length_size = 4;
constexpr size_t ed25519_signature_size = 64;
constexpr size_t ed25519_public_key_size = 32;
constexpr unsigned bits_per_byte = 8;
constexpr uint32_t byte_mask = 0xff;

/// Pack one detached signature and forward its unchanged message bytes to the host.
bool verify_message(const char* message, uint32_t message_len,
                    const std::array<char, ed25519_signature_size>& signature,
                    const std::array<char, ed25519_public_key_size>& public_key) {
   if (message_len > ed25519_max_message_size)
      return false;

   constexpr size_t header_size = ed25519_length_size + ed25519_signature_size + ed25519_public_key_size;
   std::vector<char> record(header_size + message_len);
   for (size_t i = 0; i < ed25519_length_size; ++i)
      record[i] = static_cast<char>((message_len >> (i * bits_per_byte)) & byte_mask);
   std::memcpy(record.data() + ed25519_length_size, signature.data(), signature.size());
   std::memcpy(record.data() + ed25519_length_size + signature.size(), public_key.data(), public_key.size());
   if (message_len != 0)
      std::memcpy(record.data() + header_size, message, message_len);
   return sysio::ed25519_verify(record.data(), static_cast<uint32_t>(record.size()), 1);
}
```

`message` must point to at least `message_len` readable bytes when the length is
nonzero. For batches, construct the packed buffer once and call the intrinsic once;
its verification loop and time checks execute in the host.

## Node compatibility

This API requires the companion wire-sysio Ed25519 intrinsic change. CDT headers
and libraries cannot activate a missing host import. The node change makes the
intrinsic available from genesis, including context-free actions; it does not
migrate the intrinsic whitelist in an existing chain state. Deploy only where the
host provides this import. Existing digest-based recovery APIs retain their behavior.

Native CDT tests can supply a host mock with
`sysio::native::intrinsics::set_intrinsic<sysio::native::intrinsics::ed25519_verify>`.
The native test runtime does not implement Ed25519 itself. Real verification is
covered by `ed25519_verify_tests` integration tests against wire-sysio.

## Tests

The existing `crypto_tests` native suite checks C/C++ signature types, unchanged
pointer/length/count forwarding, boolean results, and host abort propagation. The
`build-pass/ed25519_verify_0` toolchain case pins the linked WASM import signature.
The `ed25519_verify_tests` integration suite deploys a contract built from C and C++
translation units and checks both APIs against the actual host.

With integration tests configured against an Ed25519-enabled wire-sysio build:

```sh
ctest --test-dir build/debug -R '^crypto_tests$' --output-on-failure
build/debug/tools/toolchain-tester/toolchain-tester tests/toolchain \
  --cdt build/debug/bin -t build-pass/ed25519_verify_0
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm-jit
build/debug/tests/integration/integration_tests --run_test=ed25519_verify_tests -- --sys-vm-oc
```

The node repository separately tests host-loop deadline expiration, transaction
rollback, strict signature encodings, and WASM memory bounds.
