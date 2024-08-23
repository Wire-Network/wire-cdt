#include <sysio/sysio.hpp>
using namespace sysio;

class [[sysio::contract]] send_inline : public contract {
   public:
      using contract::contract;

      [[sysio::action]]
      void test( name user, name inline_code );

      using test_action = action_wrapper<"test"_n, &send_inline::test>;
};
