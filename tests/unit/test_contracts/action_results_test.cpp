#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] action_results_test : public contract {
   public:
   using contract::contract;

   [[sysio::action]]
   void action1() {}

   [[sysio::action]]
   uint32_t action2() { return 42; }

   [[sysio::action]]
   std::string action3() { return "foo"; }
};
