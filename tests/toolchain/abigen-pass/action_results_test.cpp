#include <sysio/sysio.hpp>
#include <vector>

using namespace sysio;

struct test_res {
   int a;
   float b;
   name  c;
};

class [[sysio::contract]] action_results_test : public contract {
   public:
   using contract::contract;

   [[sysio::action]]
   void action1() {}

   [[sysio::action]]
   uint32_t action2() { return 42; }

   [[sysio::action]]
   std::string action3() { return "foo"; }

   [[sysio::action]]
   std::vector<name> action4() { return {"dan"_n}; }

   [[sysio::action]]
   test_res action5() { return {4, 42.4f, "bucky"_n}; }
};
