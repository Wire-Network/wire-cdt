#include <sysio/sysio.hpp>
#include <sysio/privileged.hpp>

using namespace sysio;
extern "C" __attribute__((weak)) __attribute__((sysio_wasm_import)) void set_resource_limit(int64_t, int64_t, int64_t);

class [[sysio::contract]] warn_action_read_only : public contract {
  public:
      using contract::contract;
      
      [[sysio::action, sysio::read_only]]
      void test1( name user ) {
	      set_resource_limit(user.value, 0, 0);
      }
};
