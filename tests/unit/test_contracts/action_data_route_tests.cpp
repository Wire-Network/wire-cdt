/**
 * A hand-written apply() that routes the contract's own action and a notification through sysio::execute_action,
 * for tests/integration/action_data_tests.cpp.
 */
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] action_data_route_tests : public contract {
public:
   using contract::contract;

   [[sysio::action]] void args(name nm, uint64_t value) {
      check(nm == "alice"_n && value == 42, "unexpected arguments");
   }

   /// Handles action_data_tests::notify, declaring only its first argument.
   void on_notify(name to) {
      check(to == get_self(), "notified the wrong account");
   }
};

extern "C" {
   [[sysio::wasm_entry]]
   void apply(uint64_t receiver, uint64_t code, uint64_t action) {
      if (code == receiver) {
         if (action == "args"_n.value) {
            execute_action(name(receiver), name(code), &action_data_route_tests::args);
         }
      } else if (action == "notify"_n.value) {
         execute_action(name(receiver), name(code), &action_data_route_tests::on_notify);
      }
   }
}
