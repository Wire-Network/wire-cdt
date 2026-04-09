#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

class [[sysio::contract("hash_id_tests")]] hash_id_tests : public contract {
public:
   using contract::contract;

   // --- Compile-time tests ---

   // _i literal returns name::raw directly — works in constexpr
   static_assert(static_cast<uint64_t>("hello"_i) != static_cast<uint64_t>("world"_i),
      "different strings should differ");
   static_assert("hello"_i == "hello"_i, "same strings should match");
   static_assert(static_cast<uint64_t>("hello"_i) == hash_id::djbh_hash("hello"),
      "_i hash should match djbh_hash");

   // _i works directly as template param — no name::raw() wrapper needed
   struct my_key {
      uint64_t id;
      SYSLIB_SERIALIZE(my_key, (id))
   };

   // Value struct with [[sysio::table]] for ABI generation
   struct [[sysio::table("user_balance_history"), sysio::kv_key("my_key")]] my_val {
      uint64_t amount;
      name     owner;
      SYSLIB_SERIALIZE(my_val, (amount)(owner))
   };

   // Table with _i literal — no wrapper needed
   using long_table = kv::table<"user_balance_history"_i, my_key, my_val>;

   // Table with _n literal for comparison
   using short_table = kv::table<"balances"_n, my_key, my_val>;

   // Global with _i literal
   struct [[sysio::table("app_config")]] config {
      uint64_t rate;
      uint32_t flags;
      SYSLIB_SERIALIZE(config, (rate)(flags))
   };

   using my_config = kv::global<"app_config"_i, config>;

   // --- Runtime tests ---

   [[sysio::action]]
   void hashbasic() {
      // hash_id struct works at runtime
      auto h = hash_id("test_name");
      check(h.id != 0, "hash should be non-zero");
      check(h.id == hash_id::djbh_hash("test_name"), "runtime hash mismatch");

      // _i literal produces name::raw with correct hash value
      constexpr auto h2 = "other_name"_i;
      check(static_cast<uint64_t>(h2) != 0, "literal hash non-zero");
   }

   [[sysio::action]]
   void longname() {
      // kv::table with _i literal — CRUD
      long_table tbl(get_self());
      tbl.emplace(get_self(), {1}, {100, "alice"_n});
      tbl.emplace(get_self(), {2}, {200, "bob"_n});

      auto v1 = tbl.get({1});
      check(v1.amount == 100, "amount should be 100");
      check(v1.owner == "alice"_n, "owner should be alice");

      check(tbl.contains({2}), "key 2 should exist");
      tbl.erase({1});
      check(!tbl.contains({1}), "key 1 should be erased");
   }

   [[sysio::action]]
   void iglobal() {
      my_config cfg(get_self());
      cfg.set({42, 7}, get_self());

      auto val = cfg.get();
      check(val.rate == 42, "rate should be 42");
      check(val.flags == 7, "flags should be 7");
   }

   [[sysio::action]]
   void isolation() {
      // _i and _n tables are isolated (different table_ids from different hashes)
      long_table ltbl(get_self());
      short_table stbl(get_self());

      ltbl.emplace(get_self(), {1}, {999, "long"_n});
      stbl.emplace(get_self(), {1}, {111, "short"_n});

      auto lv = ltbl.get({1});
      auto sv = stbl.get({1});
      check(lv.amount == 999, "long table value wrong");
      check(sv.amount == 111, "short table value wrong");
   }
};

SYSIO_DISPATCH(hash_id_tests, (hashbasic)(longname)(iglobal)(isolation))
