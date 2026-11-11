// Builds the generated apply() of a contract with no actions, notification handlers, and both
// dispatch hooks.
//
// A direct action on it goes to sysio::reject_undefined_action and then, for the chain's own actions on
// `sysio`, to post_dispatch. A notification no handler matches also goes to post_dispatch.
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] dispatch_notify_only_post_dispatch : public contract {
public:
   using contract::contract;

   [[sysio::on_notify("sysio.token::transfer")]] void on_transfer(name from, name to) {}
};

extern "C" bool pre_dispatch(name self, name original_receiver, name action) {
   return true;
}

extern "C" void post_dispatch(name self, name original_receiver, name action) {
}
