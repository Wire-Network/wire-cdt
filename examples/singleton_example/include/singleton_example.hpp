#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
using namespace sysio;

class [[sysio::contract]] singleton_example : public contract {
   public:
      using contract::contract;
      singleton_example( name receiver, name code, datastream<const char*> ds ) :
         contract(receiver, code, ds),
         singleton_instance(receiver, receiver.value)
         {}

      [[sysio::action]]
      void set( name user, uint64_t value );
      [[sysio::action]]
      void get( );

      struct [[sysio::table]] testtable {
         name primary_value;
         uint64_t secondary_value;
         uint64_t primary_key() const { return primary_value.value; }
      } testtablerow;

      using singleton_type = sysio::singleton<"testtable"_n, testtable>;
      singleton_type singleton_instance;

      using set_action = action_wrapper<"set"_n, &singleton_example::set>;
      using get_action = action_wrapper<"get"_n, &singleton_example::get>;
};
