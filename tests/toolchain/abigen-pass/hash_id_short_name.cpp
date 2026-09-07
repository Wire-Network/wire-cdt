// A SHORT _i name must advertise the table_id the runtime actually uses.
//
// Two derivations existed. The auto-detected entry took the table_id from the template
// parameter -- the DJB2 hash, which is what kv::global and kv::table compute _table_id from at
// runtime. The [[sysio::table]] entry guessed from the annotation STRING, reading a name of 13
// characters or fewer as an _n encoding, and the merge kept the guess. For _n, and for an _i
// name longer than 13 characters, the two coincide; for a SHORT _i name they diverge, so the
// row landed under one id while the ABI advertised another and get_table_rows by the readable
// name reached nothing.
//
// app_config: runtime 21489 (DJB2), string-derived 38424. user_table: 61956 vs 3509.
// Both kinds are covered because add_table and add_kv_table are separate paths into the merge.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_global.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("hash_id_short_name")]] hash_id_short_name : public contract {
public:
   using contract::contract;

   struct [[sysio::table("app_config")]] app_config {
      uint64_t threshold;
      SYSLIB_SERIALIZE(app_config, (threshold))
   };

   struct user_key {
      uint64_t id;
      SYSLIB_SERIALIZE(user_key, (id))
   };

   struct [[sysio::table("user_table"), sysio::kv_key("user_key")]] user_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(user_val, (balance))
   };

   [[sysio::action]]
   void test() {
      kv::global<"app_config"_i, app_config> cfg(get_self());
      cfg.set(app_config{1}, get_self());

      kv::table<"user_table"_i, user_key, user_val> users(get_self());
      users.emplace(get_self(), {1}, {100});
   }
};
