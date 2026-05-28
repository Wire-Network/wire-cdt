/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 */

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <vector>

#include <sysio/tester.hpp>
#include <zpp_bits.h>

enum negative_enum : int64_t {
   zero = 0,
   negative = -1,
};

struct protobuf_int32_value {
   zpp::bits::vint64_t value = {};

   using serialize = zpp::bits::pb_members<1>;
   serialize use();
};

struct protobuf_int64_value {
   zpp::bits::vint64_t value = {};

   using serialize = zpp::bits::pb_members<1>;
   serialize use();
};

struct protobuf_negative_values {
   zpp::bits::vint64_t int32_value = {};
   negative_enum enum_value = zero;

   using serialize = zpp::bits::pb_members<2>;
   serialize use();
};

static std::vector<char> encode(const auto& value) {
   std::vector<char> actual;
   zpp::bits::out out(actual, zpp::bits::size_varint{});
   CHECK_EQUAL(out(value), std::errc{})
   return actual;
}

static auto decode(const std::vector<char>& bytes, auto& value) {
   zpp::bits::in in(std::span(bytes.data(), bytes.size()), zpp::bits::size_varint{});
   return in(value);
}

template <std::size_t Size>
static void check_bytes(const std::vector<char>& actual, const std::array<unsigned char, Size>& expected) {
   CHECK_EQUAL(actual.size(), expected.size())

   const auto common_size = actual.size() < expected.size() ? actual.size() : expected.size();
   for (std::size_t i = 0; i < common_size; ++i) {
      const auto actual_byte = static_cast<unsigned char>(actual[i]);
      if (actual_byte != expected[i]) {
         const bool disable_output = ___disable_output;
         silence_output(false);
         sysio::print("protobuf byte ", static_cast<uint64_t>(i), ": expected ", static_cast<uint64_t>(expected[i]),
                      " got ", static_cast<uint64_t>(actual_byte), "\n");
         silence_output(disable_output);
      }
      CHECK_EQUAL(actual_byte, expected[i])
   }
}

template <typename Message, std::size_t Size>
static void check_int_value(int64_t value, const std::array<unsigned char, Size>& expected) {
   Message original;
   original.value = value;

   const auto actual = encode(original);
   check_bytes(actual, expected);

   Message decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(static_cast<int64_t>(decoded.value), value)
}

SYSIO_TEST_BEGIN(protobuf_int32_boundary_wire_test)
   check_int_value<protobuf_int32_value>(0, std::array<unsigned char, 3>{0x02, 0x08, 0x00});
   check_int_value<protobuf_int32_value>(1, std::array<unsigned char, 3>{0x02, 0x08, 0x01});
   check_int_value<protobuf_int32_value>(42, std::array<unsigned char, 3>{0x02, 0x08, 0x2a});
   check_int_value<protobuf_int32_value>(
      std::numeric_limits<int32_t>::max(),
      std::array<unsigned char, 7>{0x06, 0x08, 0xff, 0xff, 0xff, 0xff, 0x07});
   check_int_value<protobuf_int32_value>(
      -1,
      std::array<unsigned char, 12>{0x0b, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
   check_int_value<protobuf_int32_value>(
      std::numeric_limits<int32_t>::min(),
      std::array<unsigned char, 12>{0x0b, 0x08, 0x80, 0x80, 0x80, 0x80, 0xf8, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_int64_negative_wire_test)
   check_int_value<protobuf_int64_value>(
      -2,
      std::array<unsigned char, 12>{0x0b, 0x08, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_negative_enum_wire_test)
   protobuf_negative_values value;
   value.int32_value = -1;
   value.enum_value = negative;

   const auto actual = encode(value);

   const std::array<unsigned char, 23> expected = {
      0x16,
      0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
      0x10, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
   };

   check_bytes(actual, expected);

   protobuf_negative_values decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(static_cast<int64_t>(decoded.int32_value), -1)
   CHECK_EQUAL(decoded.enum_value, negative)
SYSIO_TEST_END

int main(int argc, char** argv) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(protobuf_int32_boundary_wire_test);
   SYSIO_TEST(protobuf_int64_negative_wire_test);
   SYSIO_TEST(protobuf_negative_enum_wire_test);
   return has_failed();
}
