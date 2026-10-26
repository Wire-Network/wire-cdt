#include <sysio/crypto.h>

/** Verify through the public C declaration, including its C bool return type. */
bool verify_ed25519_c(const char* entries, uint32_t entries_len, uint32_t count) {
   return ed25519_verify(entries, entries_len, count);
}
