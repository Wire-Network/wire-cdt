/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <array>
#include <cstdint>
#include <vector>

#include <zpp_bits.h>

enum negative_enum : int64_t {
   zero = 0,
   negative = -1,
};

struct protobuf_negative_values {
   zpp::bits::vint64_t int32_value = {};
   negative_enum enum_value = zero;

   using serialize = zpp::bits::pb_members<2>;
   serialize use();
};

int main() {
   protobuf_negative_values value;
   value.int32_value = -1;
   value.enum_value = negative;

   std::vector<char> actual;
   zpp::bits::out out(actual, zpp::bits::size_varint{});
   auto result = out(value);
   if (result != std::errc{}) {
      return 1;
   }

   const std::array<unsigned char, 23> expected = {
      0x16,
      0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
      0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
   };

   if (actual.size() != expected.size()) {
      return 1;
   }

   for (std::size_t i = 0; i < expected.size(); ++i) {
      if (static_cast<unsigned char>(actual[i]) != expected[i]) {
         return 1;
      }
   }

   return 0;
}
