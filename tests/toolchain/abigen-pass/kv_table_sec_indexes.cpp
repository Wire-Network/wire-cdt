#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

struct user_key {
   uint64_t id;
   SYSLIB_SERIALIZE(user_key, (id))
};

struct [[sysio::table("users")]] user_val {
   name        owner;
   uint64_t    balance;
   double      score;
   checksum256 hash;
   uint64_t    get_balance() const { return balance; }
   double      get_score()   const { return score; }
   checksum256 get_hash()    const { return hash; }
   SYSLIB_SERIALIZE(user_val, (owner)(balance)(score)(hash))
};

// Table with 4 secondary indices of different key types, including checksum256
// (which is a typedef for fixed_bytes<32>) — exercises canonical-RecordType
// translation path in abigen.
class [[sysio::contract("kv_table_sec_indexes")]] kv_table_sec_indexes : public contract {
public:
   using contract::contract;

   using users = kv::table<"users"_n, user_key, user_val,
      kv::index<"byowner"_n,  kv::member_data<user_val, name, &user_val::owner>>,
      kv::index<"bybal"_n,    sysio::const_mem_fun<user_val, uint64_t,    &user_val::get_balance>>,
      kv::index<"byscore"_n,  sysio::const_mem_fun<user_val, double,      &user_val::get_score>>,
      kv::index<"byhash"_n,   sysio::const_mem_fun<user_val, checksum256, &user_val::get_hash>>
   >;

   [[sysio::action]]
   void test() {
      users tbl(get_self());
      tbl.emplace(get_self(), {1}, {.owner="alice"_n, .balance=100, .score=9.5, .hash={}});
   }
};
