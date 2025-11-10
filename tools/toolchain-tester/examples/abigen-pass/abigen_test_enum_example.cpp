#include <sysio/sysio.hpp>
using namespace sysio;

enum abigen_test_enum : uint8_t {
   ABIGEN_TEST_ENUM_UNKNOWN = 0,
   ABIGEN_TEST_ENUM_1 = 1,
   ABIGEN_TEST_ENUM_2
};

CONTRACT abigen_test_enum_example : public contract {
   public:
      using contract::contract;
      abigen_test_enum_example( name receiver, name code, datastream<const char*> ds )
         : contract(receiver, code, ds) {}

      ACTION echo( name user, abigen_test_enum abigen_test_enum_value );

      using echo_action = action_wrapper<"echo"_n, &abigen_test_enum_example::echo>;
};

ACTION abigen_test_enum_example::echo( name user, abigen_test_enum abigen_test_enum_value ) {
  sysio::print_f("Echo : {%,%}\n", user, static_cast<uint8_t>(abigen_test_enum_value));
}

