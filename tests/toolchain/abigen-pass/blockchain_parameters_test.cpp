// Pins the ABI of sysio::blockchain_parameters: its fields in the order wire-sysio's chain_config packs them, ending
// with notify_cpu_stipend. An action taking the struct puts it in the ABI.
#include <sysio/privileged.hpp>
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] blockchain_parameters_test : public contract {
   public:
   using contract::contract;

   [[sysio::action]]
   void setparams( const blockchain_parameters& params ) {}
};
