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
    *  Recover the uncompressed secp256k1 public key from a signature and digest.
    *
    *  Wraps the raw k1_recover intrinsic with sysio:: types.
    *  Extracts raw signature bytes from the ecc_signature variant (index 0 for K1, 3 for EM).
    *
    *  @ingroup crypto
    *  @param sig - the signature (must be ecc_signature at variant index 0 or 3)
    *  @param digest - the message digest
    *  @return std::array<char, 65> - uncompressed public key [0x04 | X(32) | Y(32)]
    */
   inline std::array<char, 65> k1_recover_uncompressed(const sysio::signature& sig, const sysio::checksum256& digest) {
      // Extract raw signature bytes from the variant
      const char* sig_data = nullptr;
      uint32_t sig_len = 0;

      switch (sig.index()) {
         case 0: { // K1
            const auto& raw = std::get<0>(sig);
            sig_data = raw.data();
            sig_len  = raw.size();
            break;
         }
         case 3: { // EM
            const auto& raw = std::get<3>(sig);
            sig_data = raw.data();
            sig_len  = raw.size();
            break;
         }
         default:
            check(false, "k1_recover_uncompressed: signature must be K1 (index 0) or EM (index 3)");
      }

      // Extract digest bytes
      auto digest_bytes = digest.extract_as_byte_array();

      std::array<char, 65> pub{};
      auto rc = sysio::k1_recover(
         sig_data, sig_len,
         reinterpret_cast<const char*>(digest_bytes.data()), digest_bytes.size(),
         pub.data(), pub.size()
      );
      check(rc == 0, "k1_recover failed");
      return pub;
   }

} // namespace sysio
