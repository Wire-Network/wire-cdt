#include <sysio/sysio.hpp>

using namespace sysio;

namespace test {
   using _Bool = int32_t;
}

using My_Bool = float;

class [[sysio::contract]] bool_template : public sysio::contract {
   public:
      using contract::contract;

      [[sysio::action]] void test1(std::optional<bool> a) {}
      [[sysio::action]] void test2(std::variant<uint64_t, bool> a) {}
      [[sysio::action]] void test3(bool a) {}

      [[sysio::action]] void test4(test::_Bool a) {}
      [[sysio::action]] void test5(My_Bool a) {}
};
