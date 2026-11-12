/**
 * A contract with a notification handler and no actions, for tests/integration/action_data_tests.cpp.
 */
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] action_data_notify_only : public contract {
public:
   using contract::contract;

   [[sysio::on_notify("*::notify")]] void on_notify(name to) {}
};
