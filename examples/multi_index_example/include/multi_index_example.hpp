#include <sysio/sysio.hpp>
using namespace sysio;

class [[sysio::contract]] multi_index_example : public contract {
   public:
      using contract::contract;
      multi_index_example( name receiver, name code, datastream<const char*> ds )
         : contract(receiver, code, ds), testtab(receiver, receiver.value) {}

      struct [[sysio::table]] test_table {
         name test_primary;
         name secondary;
         uint64_t datum;
         uint64_t primary_key()const { return test_primary.value; }
         uint64_t by_secondary()const { return secondary.value; }
      };

      typedef sysio::multi_index<"testtaba"_n, test_table, 
         sysio::indexed_by<"secid"_n, 
         sysio::const_mem_fun<test_table, uint64_t, &test_table::by_secondary>>> 
         test_tables;

      test_tables testtab;

      [[sysio::action]] 
      void set(name user);
      [[sysio::action]] 
      void print( name user );
      [[sysio::action]] 
      void bysec( name secid );
      [[sysio::action]] 
      void mod( name user, uint32_t n );
      [[sysio::action]] 
      void del( name user );

      using set_action = action_wrapper<"set"_n, &multi_index_example::set>;
      using print_action = action_wrapper<"print"_n, &multi_index_example::print>;
      using bysec_action = action_wrapper<"bysec"_n, &multi_index_example::bysec>;
      using mod_action = action_wrapper<"mod"_n, &multi_index_example::mod>;
      using del_action = action_wrapper<"del"_n, &multi_index_example::del>;
};
