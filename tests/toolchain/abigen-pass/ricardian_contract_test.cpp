#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] ricardian_contract_test : public contract {
  public:
      using contract::contract;
      
      [[sysio::action]]
      void test() {
      }
};