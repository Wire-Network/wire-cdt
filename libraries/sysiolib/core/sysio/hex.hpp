/**
 *  @file
 *  @copyright defined in cdt/LICENSE
 */
#pragma once

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <sysio/check.hpp>

namespace sysio {

   /**
    *  Convert a single hex character to its nibble value (0-15).
    *
    *  @ingroup crypto
    *  @param c - hex character ('0'-'9', 'a'-'f', 'A'-'F')
    *  @return uint8_t nibble value
    */
   inline uint8_t from_hex_char(char c) {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      sysio::check(false, std::string("Invalid hex char: ") + c);
   }

   /**
    *  Convert binary data to a lowercase hex string.
    *
    *  @ingroup crypto
    *  @param data - pointer to binary data
    *  @param size - number of bytes
    *  @return std::string - hex encoded string
    */
   inline std::string to_hex(const char* data, uint32_t size) {
      static const char hex_chars[] = "0123456789abcdef";
      std::string result;
      result.resize(size * 2);
      for (uint32_t i = 0; i < size; ++i) {
         auto byte = static_cast<uint8_t>(data[i]);
         result[i * 2]     = hex_chars[byte >> 4];
         result[i * 2 + 1] = hex_chars[byte & 0x0f];
      }
      return result;
   }

   /**
    *  Convert binary data (uint8_t) to a lowercase hex string.
    *
    *  @ingroup crypto
    *  @param data - pointer to binary data
    *  @param size - number of bytes
    *  @return std::string - hex encoded string
    */
   inline std::string to_hex(const uint8_t* data, uint32_t size) {
      return to_hex(reinterpret_cast<const char*>(data), size);
   }

   /**
    *  Decode a hex string into a byte vector. Strips optional "0x" prefix.
    *
    *  @ingroup crypto
    *  @param hex_str - hex encoded string
    *  @return std::vector<uint8_t> - decoded bytes
    */
   inline size_t from_hex(const std::string& hex_str, char* out_data, size_t out_data_len) {
      auto start = hex_str.data();
      auto len   = hex_str.size();

      // Strip optional 0x prefix
      if (len >= 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
         start += 2;
         len   -= 2;
      }

      size_t decoded_len = (len + 1) / 2;
      size_t write_len = decoded_len < out_data_len ? decoded_len : out_data_len;
      size_t written = 0;
      size_t i = 0;

      if (len % 2 != 0) {
         if (written < write_len) {
            out_data[written++] = static_cast<char>(from_hex_char(start[0]));
         }
         i = 1;
      }
      for (; i < len && written < write_len; i += 2) {
         out_data[written++] = static_cast<char>(
            (from_hex_char(start[i]) << 4) | from_hex_char(start[i + 1])
         );
      }
      return written;
   }

   /**
    *  Decode a hex string into a byte vector. Strips optional "0x" prefix.
    *
    *  @ingroup crypto
    *  @param hex_str - hex encoded string
    *  @return std::vector<uint8_t> - decoded bytes
    */
   inline std::vector<uint8_t> from_hex(const std::string& hex_str) {
      auto start = hex_str.data();
      auto len   = hex_str.size();

      // Strip optional 0x prefix
      if (len >= 2 && start[0] == '0' && (start[1] == 'x' || start[1] == 'X')) {
         len -= 2;
      }

      size_t decoded_len = (len + 1) / 2;
      std::vector<uint8_t> out(decoded_len);
      from_hex(hex_str, reinterpret_cast<char*>(out.data()), decoded_len);
      return out;
   }

} // namespace sysio
