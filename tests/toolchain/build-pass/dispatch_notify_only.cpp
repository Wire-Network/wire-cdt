// Builds the generated apply() of a contract that has notification handlers and no actions.
//
// Such a contract's apply() has no action switch, and a direct action on it goes straight to
// sysio::reject_undefined_action, which fails it unless the chain handles that action on `sysio`.
// tests/integration/action_data_tests.cpp checks the behavior on a chain.
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] dispatch_notify_only : public contract {
public:
   using contract::contract;

   [[sysio::on_notify("*::transfer")]] void on_transfer(name from, name to) {}
};
