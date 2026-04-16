#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

// Test: _i literal table name appears correctly in ABI with table_id.
class [[sysio::contract("hash_id_table")]] hash_id_table : public contract {
public:
   using contract::contract;

   struct my_key {
      uint64_t id;
      SYSLIB_SERIALIZE(my_key, (id))
   };

   struct [[sysio::table("user_balance_history"), sysio::kv_key("my_key")]] my_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(my_val, (balance))
   };

   using my_table = kv::table<"user_balance_history"_i, my_key, my_val>;

   [[sysio::action]]
   void test() {
      my_table tbl(get_self());
      tbl.emplace(get_self(), {1}, {100});
   }
};
