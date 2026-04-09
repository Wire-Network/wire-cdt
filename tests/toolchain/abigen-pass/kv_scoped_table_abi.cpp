#include <sysio/sysio.hpp>
#include <sysio/kv_scoped_table.hpp>

using namespace sysio;

struct acct_key {
   uint64_t sym_code;
   SYSLIB_SERIALIZE(acct_key, (sym_code))
};

struct [[sysio::table("accounts")]] acct_val {
   uint64_t balance;
   name     owner;
   uint64_t get_balance() const { return balance; }
   SYSLIB_SERIALIZE(acct_val, (balance)(owner))
};

class [[sysio::contract("kv_scoped_table_abi")]] kv_scoped_table_abi : public contract {
public:
   using contract::contract;
   using accounts = kv::scoped_table<"accounts"_n, acct_key, acct_val,
      kv::index<"bybal"_n, const_mem_fun<acct_val, uint64_t, &acct_val::get_balance>>
   >;

   [[sysio::action]]
   void test() {
      accounts accts(get_self(), "alice"_n.value);
      accts.emplace(get_self(), {1}, {100, "alice"_n});
   }
};
