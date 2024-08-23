#include <sysio/sysio.hpp>
using namespace sysio;

class [[sysio::contract]] hello : public contract {
   public:
      using contract::contract;

      [[sysio::action]] 
      void hi( name nm );
      [[sysio::action]] 
      void check( name nm );
      [[sysio::action]]
      std::pair<int, std::string> checkwithrv( name nm );

      using hi_action = action_wrapper<"hi"_n, &hello::hi>;
      using check_action = action_wrapper<"check"_n, &hello::check>;
      using checkwithrv_action = action_wrapper<"checkwithrv"_n, &hello::checkwithrv>;
};
