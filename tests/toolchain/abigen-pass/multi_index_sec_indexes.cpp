#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

// multi_index with 3 secondary indices of different key types.
// abigen should emit secondary_indexes in the ABI table_def with positional table_ids.
struct [[sysio::table("users")]] user_obj {
   uint64_t    id;
   name        owner;
   uint64_t    balance;
   checksum256 hash;

   uint64_t    primary_key()  const { return id; }
   uint64_t    by_owner()     const { return owner.value; }
   uint64_t    by_balance()   const { return balance; }
   checksum256 by_hash()      const { return hash; }
};

class [[sysio::contract("multi_index_sec_indexes")]] multi_index_sec_indexes : public contract {
public:
   using contract::contract;

   using users = multi_index<"users"_n, user_obj,
      indexed_by<"byowner"_n, const_mem_fun<user_obj, uint64_t,    &user_obj::by_owner>>,
      indexed_by<"bybal"_n,   const_mem_fun<user_obj, uint64_t,    &user_obj::by_balance>>,
      indexed_by<"byhash"_n,  const_mem_fun<user_obj, checksum256, &user_obj::by_hash>>
   >;

   [[sysio::action]]
   void test() {
      users tbl(get_self(), get_self().value);
      tbl.emplace(get_self(), [&](auto& r) {
         r.id = 1;
         r.owner = "alice"_n;
         r.balance = 100;
      });
   }
};
