/**
 *  @file
 *  @copyright defined in cdt/LICENSE
 */
#pragma once

#include "check.hpp"
#include "crypto_ext.hpp"
#include "hex.hpp"

#include <array>
#include <cstring>
#include <variant>

namespace sysio {

   /**
    *  Convert an Ethereum hex signature string to a sysio::signature (EM variant, index 3).
    *
    *  The input should be a 65-byte compact signature in hex format (130 or 132 chars with "0x" prefix).
    *  Layout: r (32 bytes) || s (32 bytes) || v (1 byte recovery id).
    *
    *  @ingroup crypto
    *  @param hex_sig - hex encoded compact signature string
    *  @return sysio::signature with EM variant (index 3)
    */
   inline sysio::signature eth_signature_from_hex(const std::string& hex_sig) {
      auto bytes = sysio::from_hex(hex_sig);
      check(bytes.size() == 65, "eth_signature_from_hex: expected 65-byte signature");

      ecc_signature sig;
      std::memcpy(sig.data(), bytes.data(), 65);

      // Construct signature variant at index 3 (EM)
      sysio::signature result;
      result.template emplace<3>(sig);
      return result;
   }

   /**
    *  Apply EIP-191 personal message prefix and keccak hash.
    *
    *  Computes: keccak256("\x19Ethereum Signed Message:\n" + len_str + payload)
    *
    *  This matches what MetaMask/wallets do when signing and what
    *  assert_recover_key does internally for EM signatures.
    *
    *  @ingroup crypto
    *  @param payload - raw bytes to wrap
    *  @param payload_len - number of bytes
    *  @return checksum256 - the EIP-191 wrapped keccak hash
    */
   inline sysio::checksum256 eip191_hash(const char* payload, uint32_t payload_len) {
      // "\x19Ethereum Signed Message:\n"
      const char prefix[] = "\x19" "Ethereum Signed Message:\n";
      constexpr uint32_t prefix_len = sizeof(prefix) - 1; // 26 bytes

      // Convert payload_len to decimal string
      char len_str[12];
      uint32_t len_str_len = 0;
      {
         uint32_t n = payload_len;
         if (n == 0) {
            len_str[0] = '0';
            len_str_len = 1;
         } else {
            char tmp[12];
            uint32_t tmp_len = 0;
            while (n > 0) {
               tmp[tmp_len++] = '0' + (n % 10);
               n /= 10;
            }
            for (uint32_t i = 0; i < tmp_len; ++i)
               len_str[i] = tmp[tmp_len - 1 - i];
            len_str_len = tmp_len;
         }
      }

      // Build: prefix + len_str + payload
      uint32_t total = prefix_len + len_str_len + payload_len;
      std::vector<char> buf(total);
      std::memcpy(buf.data(), prefix, prefix_len);
      std::memcpy(buf.data() + prefix_len, len_str, len_str_len);
      std::memcpy(buf.data() + prefix_len + len_str_len, payload, payload_len);

      return sysio::keccak(buf.data(), total);
   }

   /**
    *  Recover the uncompressed secp256k1 public key from a signature and digest.
    *
    *  Wraps the raw k1_recover intrinsic with sysio:: types.
    *  For EM signatures (index 3), applies EIP-191 wrapping to match assert_recover_key behavior.
    *
    *  @ingroup crypto
    *  @param sig - the signature (must be ecc_signature at variant index 0 or 3)
    *  @param digest - the message digest (raw, before any EIP-191 wrapping)
    *  @return std::array<char, 65> - uncompressed public key [0x04 | X(32) | Y(32)]
    */
   inline std::array<char, 65> k1_recover_uncompressed(const sysio::signature& sig, const sysio::checksum256& digest) {
      const char* sig_data = nullptr;
      uint32_t sig_len = 0;
      bool is_em = false;

      // k1_recover expects signature format: v(1) || r(32) || s(32)  (v in range 27-34)
      // K1 signatures (index 0) are already in this format.
      // EM signatures (index 3) are in Ethereum format: r(32) || s(32) || v(1)
      // and need reordering.
      std::array<char, 65> reordered_sig;

      switch (sig.index()) {
         case 0: { // K1 — already v || r || s
            const auto& raw = std::get<0>(sig);
            sig_data = raw.data();
            sig_len  = raw.size();
            break;
         }
         case 3: { // EM — r || s || v → reorder to v || r || s
            const auto& raw = std::get<3>(sig);
            reordered_sig[0] = raw[64]; // v
            std::memcpy(reordered_sig.data() + 1, raw.data(), 64); // r || s
            sig_data = reordered_sig.data();
            sig_len  = 65;
            is_em = true;
            break;
         }
         default:
            check(false, "k1_recover_uncompressed: signature must be K1 (index 0) or EM (index 3)");
      }

      // For EM signatures, apply EIP-191 wrapping to match assert_recover_key
      auto raw_digest_bytes = digest.extract_as_byte_array();
      std::array<uint8_t, 32> recover_digest;
      if (is_em) {
         auto eip191 = eip191_hash(
            reinterpret_cast<const char*>(raw_digest_bytes.data()), raw_digest_bytes.size());
         auto eip191_bytes = eip191.extract_as_byte_array();
         std::memcpy(recover_digest.data(), eip191_bytes.data(), 32);
      } else {
         std::memcpy(recover_digest.data(), raw_digest_bytes.data(), 32);
      }

      std::array<char, 65> pub{};
      auto rc = sysio::k1_recover(
         sig_data, sig_len,
         reinterpret_cast<const char*>(recover_digest.data()), 32,
         pub.data(), pub.size()
      );
      check(rc == 0, "k1_recover failed");
      return pub;
   }

} // namespace sysio
