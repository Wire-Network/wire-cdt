// Pins the WASM of a minimal contract, so any change to the generated dispatcher shows up as a diff here.
//
// The generated action wrapper fails an action whose data holds bytes after the arguments
// (sysio::check_action_data_consumed), and the generated apply() fails an action name the contract
// does not define (sysio::reject_undefined_action). Regenerate the expected blob with
// `xxd -p dispatch_pinned.wasm | tr -d '\n'` when the dispatcher changes on purpose.
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] dispatch_pinned : public contract {
public:
   using contract::contract;

   [[sysio::action]] void hi(name nm) {}
};
