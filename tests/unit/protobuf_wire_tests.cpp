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

#include <sysio/pb.hpp>
#include <sysio/tester.hpp>

enum positive_enum : int32_t {
   zero = 0,
   positive = 42,
};

struct protobuf_int32_value {
   sysio::pb_int32 value = {};

   using serialize = sysio::pb_members<1>;
   serialize use();
};

struct protobuf_int64_value {
   zpp::bits::vint64_t value = {};

   using serialize = sysio::pb_members<1>;
   serialize use();
};

struct protobuf_negative_values {
   sysio::pb_int32 int32_value = {};
   positive_enum enum_value = zero;

   using serialize = sysio::pb_members<2>;
   serialize use();
};

struct protobuf_repeated_int32_values {
   std::vector<sysio::pb_int32> values = {};

   using serialize = sysio::pb_members<1>;
   serialize use();
};

struct protobuf_nested_int32_value {
   sysio::pb_int32 value = {};

   using serialize = sysio::pb_members<1>;
   serialize use();
};

struct protobuf_repeated_nested_values {
   std::vector<protobuf_nested_int32_value> values = {};

   using serialize = sysio::pb_members<1>;
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

template <std::size_t Size>
static void check_int32_value(int32_t value, const std::array<unsigned char, Size>& expected) {
   protobuf_int32_value original;
   original.value = value;

   const auto actual = encode(original);
   check_bytes(actual, expected);

   protobuf_int32_value decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(static_cast<int32_t>(decoded.value), value)
}

template <std::size_t Size>
static void check_int64_value(int64_t value, const std::array<unsigned char, Size>& expected) {
   protobuf_int64_value original;
   original.value = value;

   const auto actual = encode(original);
   check_bytes(actual, expected);

   protobuf_int64_value decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(static_cast<int64_t>(decoded.value), value)
}

SYSIO_TEST_BEGIN(protobuf_int32_boundary_wire_test)
   CHECK_EQUAL(sizeof(sysio::pb_int32), sizeof(int32_t))

   check_int32_value(0, std::array<unsigned char, 3>{0x02, 0x08, 0x00});
   check_int32_value(1, std::array<unsigned char, 3>{0x02, 0x08, 0x01});
   check_int32_value(42, std::array<unsigned char, 3>{0x02, 0x08, 0x2a});
   check_int32_value(
      std::numeric_limits<int32_t>::max(),
      std::array<unsigned char, 7>{0x06, 0x08, 0xff, 0xff, 0xff, 0xff, 0x07});
   check_int32_value(
      -1,
      std::array<unsigned char, 12>{0x0b, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
   check_int32_value(
      std::numeric_limits<int32_t>::min(),
      std::array<unsigned char, 12>{0x0b, 0x08, 0x80, 0x80, 0x80, 0x80, 0xf8, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_repeated_int32_wire_test)
   protobuf_repeated_int32_values value;
   value.values = {1, -1};

   const auto actual = encode(value);

   const std::array<unsigned char, 14> expected = {
      0x0d,
      0x0a, 0x0b, 0x01, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
   };

   check_bytes(actual, expected);

   protobuf_repeated_int32_values decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(decoded.values.size(), static_cast<std::size_t>(2))
   if (decoded.values.size() == 2) {
      CHECK_EQUAL(static_cast<int32_t>(decoded.values[0]), 1)
      CHECK_EQUAL(static_cast<int32_t>(decoded.values[1]), -1)
   }
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_repeated_nested_message_wire_test)
   protobuf_repeated_nested_values value;
   value.values = {{7}, {-1}};

   const auto actual = encode(value);

   const std::array<unsigned char, 18> expected = {
      0x11,
      0x0a, 0x02, 0x08, 0x07,
      0x0a, 0x0b, 0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
   };

   check_bytes(actual, expected);

   protobuf_repeated_nested_values decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(decoded.values.size(), static_cast<std::size_t>(2))
   if (decoded.values.size() == 2) {
      CHECK_EQUAL(static_cast<int32_t>(decoded.values[0].value), 7)
      CHECK_EQUAL(static_cast<int32_t>(decoded.values[1].value), -1)
   }
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_int64_negative_wire_test)
   check_int64_value(
      -2,
      std::array<unsigned char, 12>{0x0b, 0x08, 0xfe, 0xff, 0xff, 0xff, 0xff, 0xff,
                                    0xff, 0xff, 0xff, 0x01});
SYSIO_TEST_END

SYSIO_TEST_BEGIN(protobuf_int32_and_enum_wire_test)
   protobuf_negative_values value;
   value.int32_value = -1;
   value.enum_value = positive;

   const auto actual = encode(value);

   const std::array<unsigned char, 14> expected = {
      0x0d,
      0x08, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x01,
      0x10, 0x2a,
   };

   check_bytes(actual, expected);

   protobuf_negative_values decoded;
   CHECK_EQUAL(decode(actual, decoded), std::errc{})
   CHECK_EQUAL(static_cast<int64_t>(decoded.int32_value), -1)
   CHECK_EQUAL(decoded.enum_value, positive)
SYSIO_TEST_END

int main(int argc, char** argv) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(protobuf_int32_boundary_wire_test);
   SYSIO_TEST(protobuf_repeated_int32_wire_test);
   SYSIO_TEST(protobuf_repeated_nested_message_wire_test);
   SYSIO_TEST(protobuf_int64_negative_wire_test);
   SYSIO_TEST(protobuf_int32_and_enum_wire_test);
   return has_failed();
}
