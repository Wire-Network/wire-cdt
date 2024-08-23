#include <sysio/sysio.hpp>
#include <sysio/tester.hpp>

using namespace sysio::native;

EOSIO_TEST_BEGIN(print_test)
   CHECK_PRINT("27", [](){ sysio::print((uint8_t)27); });
   CHECK_PRINT("34", [](){ sysio::print((int)34); });
   CHECK_PRINT([](std::string s){return s[0] == 'a';},  [](){ sysio::print((char)'a'); });
   CHECK_PRINT([](std::string s){return s[0] == 'b';},  [](){ sysio::print((int8_t)'b'); });
   CHECK_PRINT("202", [](){ sysio::print((unsigned int)202); });
   CHECK_PRINT("-202", [](){ sysio::print((int)-202); });
   CHECK_PRINT("707", [](){ sysio::print((unsigned long)707); });
   CHECK_PRINT("-707", [](){ sysio::print((long)-707); });
   CHECK_PRINT("909", [](){ sysio::print((unsigned long long)909); });
   CHECK_PRINT("-909", [](){ sysio::print((long long)-909); });
   CHECK_PRINT("404", [](){ sysio::print((uint32_t)404); });
   CHECK_PRINT("-404", [](){ sysio::print((int32_t)-404); });
   CHECK_PRINT("404000000", [](){ sysio::print((uint64_t)404000000); });
   CHECK_PRINT("-404000000", [](){ sysio::print((int64_t)-404000000); });
   CHECK_PRINT("0x0066000000000000", [](){ sysio::print((uint128_t)102); });
   CHECK_PRINT("0xffffff9affffffffffffffffffffffff", [](){ sysio::print((int128_t)-102); });
EOSIO_TEST_END

int main(int argc, char** argv) {
   bool verbose = false;
   if( argc >= 2 && std::strcmp( argv[1], "-v" ) == 0 ) {
      verbose = true;
   }
   silence_output(!verbose);

   EOSIO_TEST(print_test);
   return has_failed();
}
