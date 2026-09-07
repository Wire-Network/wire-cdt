// The same rule for the multi_index family, which shares add_table() with kv::global.
//
// Both spellings are covered because they reach it by different visitor branches: multi_index
// is the compatibility alias contract authors write, kv_multi_index the type it resolves to.
// Two distinct names, so a genuine table_id collision cannot mask the regression this pins.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("hash_id_multi_index")]] hash_id_multi_index : public contract {
public:
   using contract::contract;

   struct [[sysio::table("user_balance_history")]] balance_row {
      uint64_t id;
      uint64_t balance;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(balance_row, (id)(balance))
   };

   struct [[sysio::table("user_preferences_store")]] preference_row {
      uint64_t id;
      name     favourite;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(preference_row, (id)(favourite))
   };

   [[sysio::action]]
   void test() {
      multi_index<"user_balance_history"_i, balance_row> balances(get_self(), get_self().value);
      kv_multi_index<"user_preferences_store"_i, preference_row> prefs(get_self(), get_self().value);
      (void)balances;
      (void)prefs;
   }
};
