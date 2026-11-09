/**
 * The same checks as action_data_tests, through SYSIO_DISPATCH instead of the generated dispatcher, for
 * tests/integration/action_data_tests.cpp.
 */
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] action_data_macro_tests : public contract {
public:
   using contract::contract;

   [[sysio::action]] void args(name nm, uint64_t value) {
      check(nm == "alice"_n && value == 42, "unexpected arguments");
   }

   [[sysio::action]] void noargs() {}
};

SYSIO_DISPATCH(action_data_macro_tests, (args)(noargs))
