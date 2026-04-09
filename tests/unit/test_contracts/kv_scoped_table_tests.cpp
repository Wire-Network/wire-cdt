#include <sysio/sysio.hpp>
#include <sysio/kv_scoped_table.hpp>
#include <sysio/kv_multi_index.hpp>

using namespace sysio;

class [[sysio::contract("kv_scoped_table_tests")]] kv_scoped_table_tests : public contract {
public:
   using contract::contract;

   // ── Row types ────────────────────────────────────────────────────────────

   struct pk_key {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(pk_key, (id))
   };

   struct [[sysio::table("accounts")]] account {
      uint64_t balance;
      name     owner;
      uint64_t get_balance() const { return balance; }
      SYSLIB_SERIALIZE(account, (balance)(owner))
   };

   using accounts = kv::scoped_table<"accounts"_n, pk_key, account,
      kv::index<"byowner"_n,  kv::member_data<account, name, &account::owner>>,
      kv::index<"bybal"_n,    const_mem_fun<account, uint64_t, &account::get_balance>>
   >;

   // ── For kv_multi_index compat test ───────────────────────────────────────

   struct [[sysio::table("compat")]] compat_row {
      uint64_t primary_key() const { return id; }
      uint64_t id;
      uint64_t data;
   };
   using compat_mi = kv_multi_index<"compat"_n, compat_row>;

   struct compat_pk {
      uint64_t id;
      SYSLIB_SERIALIZE(compat_pk, (id))
   };
   struct [[sysio::table("compat2")]] compat_val {
      uint64_t id;
      uint64_t data;
      SYSLIB_SERIALIZE(compat_val, (id)(data))
   };
   using compat_st = kv::scoped_table<"compat"_n, compat_pk, compat_val>;

   // ── Actions ──────────────────────────────────────────────────────────────

   // Basic emplace + find within scope
   [[sysio::action]] void emplace() {
      accounts accts(get_self(), "alice"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{1000, "alice"_n});

      auto it = accts.find(pk_key{1});
      check(it != accts.end(), "should find emplaced row");
      check(it->balance == 1000, "balance mismatch");
      check(it->owner == "alice"_n, "owner mismatch");
      check(it.key().id == 1, "key mismatch");
   }

   // Scope isolation: same key in different scopes are independent
   [[sysio::action]] void scopeiso() {
      accounts alice(get_self(), "alice"_n.value);
      accounts bob(get_self(), "bob"_n.value);

      alice.emplace(get_self(), pk_key{1}, account{500, "alice"_n});
      bob.emplace(get_self(), pk_key{1}, account{700, "bob"_n});

      check(alice.get(pk_key{1}).balance == 500, "alice should have 500");
      check(bob.get(pk_key{1}).balance == 700, "bob should have 700");

      // Iteration is scoped
      int alice_count = 0;
      for (auto it = alice.begin(); it != alice.end(); ++it) alice_count++;
      check(alice_count == 1, "alice should have 1 row");

      int bob_count = 0;
      for (auto it = bob.begin(); it != bob.end(); ++it) bob_count++;
      check(bob_count == 1, "bob should have 1 row");

      // try_get / contains
      check(alice.contains(pk_key{1}), "alice contains 1");
      check(!alice.contains(pk_key{2}), "alice !contains 2");
      check(!alice.try_get(pk_key{2}).has_value(), "alice try_get 2 empty");
   }

   // Secondary index find within scope
   [[sysio::action]] void secfind() {
      accounts accts(get_self(), "scope1"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});

      auto idx = accts.get_index<"byowner"_n>();
      auto it = idx.find("alice"_n);
      check(it != idx.end(), "should find alice");
      check(it->balance == 100, "alice balance");
      check(it.key().id == 1, "alice key");

      it = idx.find("bob"_n);
      check(it != idx.end(), "should find bob");
      check(it->balance == 200, "bob balance");
   }

   // Secondary iteration stays within scope boundary
   [[sysio::action]] void seciter() {
      // Populate two scopes
      accounts s1(get_self(), 1);
      accounts s2(get_self(), 2);
      s1.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      s1.emplace(get_self(), pk_key{2}, account{200, "bob"_n});
      s2.emplace(get_self(), pk_key{1}, account{300, "carol"_n});

      // s1 secondary iteration should only see 2 entries
      auto idx = s1.get_index<"bybal"_n>();
      int count = 0;
      for (auto it = idx.begin(); it != idx.end(); ++it) count++;
      check(count == 2, "s1 sec iter should see 2");

      // s2 secondary iteration should only see 1 entry
      auto idx2 = s2.get_index<"bybal"_n>();
      int count2 = 0;
      for (auto it = idx2.begin(); it != idx2.end(); ++it) count2++;
      check(count2 == 1, "s2 sec iter should see 1");
   }

   // Reverse secondary iteration from end
   [[sysio::action]] void secrev() {
      accounts accts(get_self(), "rev"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});

      auto idx = accts.get_index<"bybal"_n>();
      auto it = idx.end();
      --it;
      check(it != idx.end(), "should find last");
      check(it->balance == 200, "last should be 200");
      --it;
      check(it->balance == 100, "prev should be 100");
   }

   // Modify via primary key
   [[sysio::action]] void modify() {
      accounts accts(get_self(), "mod"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});

      accts.modify(get_self(), pk_key{1}, [](account& a) {
         a.balance = 999;
      });
      check(accts.get(pk_key{1}).balance == 999, "modified balance");

      // Secondary should reflect new value
      auto idx = accts.get_index<"bybal"_n>();
      auto it = idx.find(uint64_t(999));
      check(it != idx.end(), "should find by new balance");
   }

   // Erase + secondary cleanup
   [[sysio::action]] void erasetest() {
      accounts accts(get_self(), "era"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});

      accts.erase(pk_key{1});
      check(!accts.contains(pk_key{1}), "should be erased");
      check(accts.contains(pk_key{2}), "bob should remain");

      // Secondary should not find erased
      auto idx = accts.get_index<"byowner"_n>();
      check(idx.find("alice"_n) == idx.end(), "alice sec gone");
      check(idx.find("bob"_n) != idx.end(), "bob sec remains");
   }

   // Key-only iteration within scope
   [[sysio::action]] void keyiter() {
      accounts accts(get_self(), "ki"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});

      auto idx = accts.get_index<"bybal"_n>();
      int count = 0;
      for (auto it = idx.key_begin(); it != idx.key_end(); ++it) {
         check(it->key.id > 0, "key should be valid");
         count++;
      }
      check(count == 2, "key_iter should see 2");
   }

   // available_primary_key per scope
   [[sysio::action]] void autopk() {
      accounts s1(get_self(), 1);
      accounts s2(get_self(), 2);

      check(s1.available_primary_key() == 0, "s1 empty -> 0");
      s1.emplace(get_self(), pk_key{5}, account{100, "alice"_n});
      check(s1.available_primary_key() == 6, "s1 after 5 -> 6");

      check(s2.available_primary_key() == 0, "s2 still empty -> 0");
      s2.emplace(get_self(), pk_key{10}, account{200, "bob"_n});
      check(s2.available_primary_key() == 11, "s2 after 10 -> 11");
   }

   // Write via kv_multi_index, read via scoped_table (byte-identical primary keys)
   [[sysio::action]] void kvcompat() {
      uint64_t scope = "compat"_n.value;

      // Write via kv_multi_index
      compat_mi mi(get_self(), scope);
      mi.emplace(get_self(), [&](auto& r) { r.id = 42; r.data = 1234; });

      // Read via scoped_table — primary key should be byte-identical
      compat_st st(get_self(), scope);
      auto val = st.try_get(compat_pk{42});
      check(val.has_value(), "scoped_table should find kv_multi_index row");
      check(val->data == 1234, "data should match");
   }

   // Scope iteration via scope_lower_bound / scope_end
   [[sysio::action]] void scopeiter() {
      // Populate 3 scopes
      accounts s1(get_self(), 10);
      accounts s2(get_self(), 20);
      accounts s3(get_self(), 30);
      s1.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      s2.emplace(get_self(), pk_key{1}, account{200, "bob"_n});
      s3.emplace(get_self(), pk_key{1}, account{300, "carol"_n});

      // Iterate all scopes from 0
      std::vector<uint64_t> scopes;
      for (auto it = accounts::scope_lower_bound(get_self(), 0);
           it != accounts::scope_end(); ++it) {
         scopes.push_back(*it);
      }
      check(scopes.size() == 3, "should find 3 scopes");
      check(scopes[0] == 10, "first scope 10");
      check(scopes[1] == 20, "second scope 20");
      check(scopes[2] == 30, "third scope 30");

      // Lower bound from 15 should skip scope 10
      scopes.clear();
      for (auto it = accounts::scope_lower_bound(get_self(), 15);
           it != accounts::scope_end(); ++it) {
         scopes.push_back(*it);
      }
      check(scopes.size() == 2, "should find 2 scopes from 15");
      check(scopes[0] == 20, "first from 15 is 20");
   }

   // get_scope accessor
   [[sysio::action]] void getscope() {
      accounts accts(get_self(), "test"_n.value);
      check(accts.get_scope() == "test"_n.value, "get_scope should return scope");
   }

   // upsert / set — insert-or-update
   [[sysio::action]] void upsert() {
      accounts accts(get_self(), "ups"_n.value);
      // Insert via upsert
      accts.upsert(get_self(), pk_key{1}, account{100, "alice"_n});
      check(accts.get(pk_key{1}).balance == 100, "upsert insert");
      // Update via set (alias)
      accts.set(get_self(), pk_key{1}, account{200, "alice"_n});
      check(accts.get(pk_key{1}).balance == 200, "set update");
      // Secondary should reflect update
      auto idx = accts.get_index<"bybal"_n>();
      check(idx.find(uint64_t(200)) != idx.end(), "sec finds 200");
      check(idx.find(uint64_t(100)) == idx.end(), "sec !finds old 100");
   }

   // Primary lower_bound / upper_bound
   [[sysio::action]] void bounds() {
      accounts accts(get_self(), "bnd"_n.value);
      accts.emplace(get_self(), pk_key{10}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{20}, account{200, "bob"_n});
      accts.emplace(get_self(), pk_key{30}, account{300, "carol"_n});

      // lower_bound(15) -> key 20
      auto it = accts.lower_bound(pk_key{15});
      check(it != accts.end(), "lb(15) should find");
      check(it.key().id == 20, "lb(15) -> 20");

      // lower_bound(20) -> key 20
      it = accts.lower_bound(pk_key{20});
      check(it.key().id == 20, "lb(20) -> 20");

      // upper_bound(20) -> key 30
      it = accts.upper_bound(pk_key{20});
      check(it != accts.end(), "ub(20) should find");
      check(it.key().id == 30, "ub(20) -> 30");

      // upper_bound(30) -> end
      it = accts.upper_bound(pk_key{30});
      check(it == accts.end(), "ub(30) -> end");

      // lower_bound(0) -> key 10 (first in scope)
      it = accts.lower_bound(pk_key{0});
      check(it.key().id == 10, "lb(0) -> 10");
   }

   // Secondary lower_bound / upper_bound
   [[sysio::action]] void secbounds() {
      accounts accts(get_self(), "sb"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});
      accts.emplace(get_self(), pk_key{3}, account{300, "carol"_n});

      auto idx = accts.get_index<"bybal"_n>();

      // lower_bound(150) -> balance 200
      auto it = idx.lower_bound(uint64_t(150));
      check(it != idx.end(), "sec lb(150) should find");
      check(it->balance == 200, "sec lb(150) -> 200");

      // upper_bound(200) -> balance 300
      it = idx.upper_bound(uint64_t(200));
      check(it != idx.end(), "sec ub(200) should find");
      check(it->balance == 300, "sec ub(200) -> 300");

      // upper_bound(300) -> end
      it = idx.upper_bound(uint64_t(300));
      check(it == idx.end(), "sec ub(300) -> end");

      // require_find
      it = idx.require_find(uint64_t(100), "should find 100");
      check(it->owner == "alice"_n, "require_find owner");
   }

   // Erase via secondary iterator
   [[sysio::action]] void secerase() {
      accounts accts(get_self(), "se"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});
      accts.emplace(get_self(), pk_key{3}, account{300, "carol"_n});

      auto idx = accts.get_index<"bybal"_n>();
      auto it = idx.find(uint64_t(200));
      check(it != idx.end(), "find bob");
      idx.erase(std::move(it)); // erase bob

      // bob gone from primary
      check(!accts.contains(pk_key{2}), "bob erased from primary");
      // bob gone from secondary
      check(idx.find(uint64_t(200)) == idx.end(), "bob erased from sec");
      // alice and carol remain
      check(accts.contains(pk_key{1}), "alice remains");
      check(accts.contains(pk_key{3}), "carol remains");
   }

   // Modify via secondary iterator
   [[sysio::action]] void secmod() {
      accounts accts(get_self(), "sm"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});

      auto idx = accts.get_index<"bybal"_n>();
      auto it = idx.find(uint64_t(100));
      check(it != idx.end(), "find alice");
      idx.modify(get_self(), it, account{999, "alice"_n});

      // Primary reflects change
      check(accts.get(pk_key{1}).balance == 999, "primary updated");
      // Old sec key gone, new one present
      check(idx.find(uint64_t(100)) == idx.end(), "old sec gone");
      check(idx.find(uint64_t(999)) != idx.end(), "new sec present");
   }

   // Lambda emplace
   [[sysio::action]] void lambdaempl() {
      accounts accts(get_self(), "le"_n.value);
      accts.emplace(get_self(), pk_key{1}, [](account& a) {
         a.balance = 42;
         a.owner = "alice"_n;
      });
      check(accts.get(pk_key{1}).balance == 42, "lambda balance");
      check(accts.get(pk_key{1}).owner == "alice"_n, "lambda owner");
   }

   // Reverse primary iteration
   [[sysio::action]] void prirev() {
      accounts accts(get_self(), "pr"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});
      accts.emplace(get_self(), pk_key{2}, account{200, "bob"_n});
      accts.emplace(get_self(), pk_key{3}, account{300, "carol"_n});

      auto it = accts.end();
      --it;
      check(it.key().id == 3, "last is 3");
      --it;
      check(it.key().id == 2, "prev is 2");
      --it;
      check(it.key().id == 1, "prev is 1");
   }

   // Cross-contract read (different code)
   [[sysio::action]] void crossread() {
      // Write to self
      accounts accts(get_self(), "xr"_n.value);
      accts.emplace(get_self(), pk_key{1}, account{100, "alice"_n});

      // Read from self via explicit code
      accounts reader(get_self(), "xr"_n.value);
      auto val = reader.try_get(pk_key{1});
      check(val.has_value(), "cross-read should find");
      check(val->balance == 100, "cross-read balance");
   }

   // Empty scope iteration
   [[sysio::action]] void emptyscope() {
      // No data emplaced — scope iteration should be empty
      int count = 0;
      for (auto it = accounts::scope_lower_bound(get_self(), 0);
           it != accounts::scope_end(); ++it) {
         count++;
      }
      check(count == 0, "empty table -> 0 scopes");
   }
};
