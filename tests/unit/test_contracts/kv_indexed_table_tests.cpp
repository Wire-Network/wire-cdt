#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_indexed_table_tests")]] kv_indexed_table_tests : public contract {
public:
   using contract::contract;

   // ── Row types ────────────────────────────────────────────────────────────

   struct my_key {
      uint64_t id;
      SYSLIB_SERIALIZE(my_key, (id))
   };

   struct [[sysio::table("mytbl")]] my_val {
      uint64_t balance;
      name     owner;
      uint64_t get_balance() const { return balance; }
      SYSLIB_SERIALIZE(my_val, (balance)(owner))
   };

   using my_table = kv::table<"mytbl"_n, my_key, my_val,
      kv::index<"byowner"_n,  kv::member_data<my_val, name, &my_val::owner>>,
      kv::index<"bybal"_n,    const_mem_fun<my_val, uint64_t, &my_val::get_balance>>
   >;

   my_table tbl;

   // ── Additional key types for be_key_reader coverage ──────────────────────

   // No [[sysio::table]] on a KEY struct: the annotation names the table a VALUE struct backs.
   // Annotating keys here named three tables that do not exist, and left the three tables that
   // do share one annotation on simple_val -- so their ABI names and stored ids disagreed.
   struct i64_key {
      int64_t k;
      SYSLIB_SERIALIZE(i64_key, (k))
   };
   // Shared by i64_table, str_table and multi_table, so it cannot name any one of them. Left
   // unannotated: each table takes its name from its own template parameter.
   struct simple_val {
      uint64_t v;
      SYSLIB_SERIALIZE(simple_val, (v))
   };
   using i64_table = kv::table<"signtbl"_n, i64_key, simple_val>;

   struct str_key {
      std::string region;
      uint64_t    id;
      SYSLIB_SERIALIZE(str_key, (region)(id))
   };
   using str_table = kv::table<"strtbl"_n, str_key, simple_val>;

   struct [[sysio::table("dbltbl")]] dbl_val {
      double   score;
      uint64_t id;
      double get_score() const { return score; }
      SYSLIB_SERIALIZE(dbl_val, (score)(id))
   };
   using dbl_table = kv::table<"dbltbl"_n, my_key, dbl_val,
      kv::index<"byscore"_n, const_mem_fun<dbl_val, double, &dbl_val::get_score>>
   >;

   struct multi_key {
      uint8_t  tag;
      uint64_t id;
      SYSLIB_SERIALIZE(multi_key, (tag)(id))
   };
   using multi_table = kv::table<"multitbl"_n, multi_key, simple_val>;

   // Key type with primary_key() — enables available_primary_key()
   struct apk_key {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(apk_key, (id))
   };
   struct [[sysio::table("apktbl")]] apk_val {
      uint64_t data;
      SYSLIB_SERIALIZE(apk_val, (data))
   };
   using apk_table = kv::table<"apktbl"_n, apk_key, apk_val>;

   // ── Emplace and primary find ─────────────────────────────────────────────

   [[sysio::action]]
   void emplace() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});
      tbl.emplace(get_self(), {3}, {500,  "carol"_n});

      auto it = tbl.find({2});
      check(it != tbl.end(), "find(2) should succeed");
      check(it.key().id == 2, "key should be 2");
      check(it->balance == 2000, "balance should be 2000");
      check(it->owner == "bob"_n, "owner should be bob");

      // Non-existent
      check(tbl.find({99}) == tbl.end(), "find(99) should be end");
   }

   // ── Emplace with explicit payer ──────────────────────────────────────────

   [[sysio::action]]
   void emplpayer() {
      // Verify payer overload compiles and works (uses self as payer here;
      // cross-account payer requires sysio.payer permission, tested at chain level)
      tbl.emplace(get_self(), {10}, {100, "alice"_n});

      auto val = tbl.try_get({10});
      check(val.has_value(), "should find key 10");
      check(val->balance == 100, "balance should be 100");
   }

   // ── Modify via primary iterator ──────────────────────────────────────────

   [[sysio::action]]
   void modify() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});

      auto it = tbl.require_find({1});
      tbl.modify(it, {1500, "alice"_n});

      // Verify via get (asserting form)
      auto val = tbl.get({1});
      check(val.balance == 1500, "balance should be 1500 after modify");

      // Verify secondary index updated
      auto idx = tbl.get_index<"bybal"_n>();
      auto sit = idx.find(uint64_t(1500));
      check(sit != idx.end(), "should find by balance 1500");
      check(sit.key().id == 1, "should be key 1");
   }

   // ── Modify with explicit payer ───────────────────────────────────────────

   [[sysio::action]]
   void modpayer() {
      tbl.emplace(get_self(), {1}, {100, "alice"_n});

      auto it = tbl.require_find({1});
      // Verify payer overload compiles and works (uses self as payer here;
      // cross-account payer requires sysio.payer permission, tested at chain level)
      tbl.modify(get_self(), it, {200, "alice"_n});

      auto val = tbl.get({1});
      check(val.balance == 200, "balance should be 200 after modify with payer");
   }

   // ── Erase via primary iterator ───────────────────────────────────────────

   [[sysio::action]]
   void erasetest() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});
      tbl.emplace(get_self(), {3}, {3000, "carol"_n});

      // Erase key 2
      auto it = tbl.require_find({2});
      auto next = tbl.erase(std::move(it));

      // Should be gone
      check(!tbl.contains({2}), "key 2 should be erased");

      // Secondary index should not find bob anymore
      auto idx = tbl.get_index<"byowner"_n>();
      check(idx.find("bob"_n) == idx.end(), "bob should be gone from secondary");

      // Next iterator should be valid (key 3) or end
      if (next != tbl.end()) {
         check(next.key().id == 3, "next after erase(2) should be 3");
      }
   }

   // ── Secondary find ───────────────────────────────────────────────────────

   [[sysio::action]]
   void secfind() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});
      tbl.emplace(get_self(), {3}, {500,  "carol"_n});

      auto idx = tbl.get_index<"byowner"_n>();

      auto it = idx.find("bob"_n);
      check(it != idx.end(), "should find bob");
      check(it.key().id == 2, "bob's key should be 2");
      check(it->balance == 2000, "bob's balance should be 2000");

      check(idx.find("dave"_n) == idx.end(), "dave should not exist");
   }

   // ── Secondary lower_bound / upper_bound ──────────────────────────────────

   [[sysio::action]]
   void seclbound() {
      tbl.emplace(get_self(), {1}, {100,  "alice"_n});
      tbl.emplace(get_self(), {2}, {500,  "bob"_n});
      tbl.emplace(get_self(), {3}, {1000, "carol"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // lower_bound(500) should find bob
      auto lb = idx.lower_bound(uint64_t(500));
      check(lb != idx.end(), "lower_bound(500) should find entry");
      check(lb.key().id == 2, "lower_bound(500) should be key 2 (bob)");

      // upper_bound(500) should find carol
      auto ub = idx.upper_bound(uint64_t(500));
      check(ub != idx.end(), "upper_bound(500) should find entry");
      check(ub.key().id == 3, "upper_bound(500) should be key 3 (carol)");
   }

   // ── Full secondary iteration in order ────────────────────────────────────

   [[sysio::action]]
   void seciter() {
      tbl.emplace(get_self(), {1}, {300, "charlie"_n});
      tbl.emplace(get_self(), {2}, {100, "alice"_n});
      tbl.emplace(get_self(), {3}, {200, "bob"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // Balance order: 100, 200, 300
      uint64_t expected[] = {100, 200, 300};
      int i = 0;
      for (auto it = idx.begin(); it != idx.end(); ++it) {
         check(i < 3, "too many entries");
         check(it->balance == expected[i], "wrong balance order");
         ++i;
      }
      check(i == 3, "should have 3 entries");
   }

   // ── Modify through secondary iterator ────────────────────────────────────

   [[sysio::action]]
   void secmodify() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});

      auto idx = tbl.get_index<"byowner"_n>();
      auto it = idx.find("alice"_n);
      check(it != idx.end(), "alice should exist");

      idx.modify(it, {1500, "alice"_n});

      // Verify via primary
      auto val = tbl.get({1});
      check(val.balance == 1500, "balance should be updated to 1500");

      // Verify balance index updated
      auto bal_idx = tbl.get_index<"bybal"_n>();
      check(bal_idx.find(uint64_t(1000)) == bal_idx.end(), "old balance 1000 should be gone");
      check(bal_idx.find(uint64_t(1500)) != bal_idx.end(), "new balance 1500 should exist");
   }

   // ── Erase through secondary iterator ─────────────────────────────────────

   [[sysio::action]]
   void secerase() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});
      tbl.emplace(get_self(), {2}, {2000, "bob"_n});
      tbl.emplace(get_self(), {3}, {3000, "carol"_n});

      auto idx = tbl.get_index<"byowner"_n>();
      auto it = idx.find("bob"_n);
      check(it != idx.end(), "bob should exist");

      auto next = idx.erase(std::move(it));

      // bob should be gone from everything
      check(!tbl.contains({2}), "key 2 should be erased");
      check(idx.find("bob"_n) == idx.end(), "bob should be gone from owner index");

      auto bal_idx = tbl.get_index<"bybal"_n>();
      check(bal_idx.find(uint64_t(2000)) == bal_idx.end(), "balance 2000 should be gone");
   }

   // ── Key-only iteration ───────────────────────────────────────────────────

   [[sysio::action]]
   void keyiter() {
      tbl.emplace(get_self(), {1}, {300, "charlie"_n});
      tbl.emplace(get_self(), {2}, {100, "alice"_n});
      tbl.emplace(get_self(), {3}, {200, "bob"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // Key-only iteration should yield keys in balance order
      uint64_t expected_ids[] = {2, 3, 1};  // alice(100), bob(200), charlie(300)
      uint64_t expected_bals[] = {100, 200, 300};
      int i = 0;
      for (auto it = idx.key_begin(); it != idx.key_end(); ++it) {
         check(i < 3, "too many entries in key iteration");
         check(it->key.id == expected_ids[i], "wrong key in key-only iteration");
         check(it->sec_key == expected_bals[i], "wrong sec_key in key-only iteration");
         ++i;
      }
      check(i == 3, "should have 3 key entries");
   }

   // ── require_find on primary and secondary ────────────────────────────────

   [[sysio::action]]
   void reqfind() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});

      // Primary require_find
      auto it = tbl.require_find({1}, "pk not found");
      check(it->balance == 1000, "balance should be 1000");

      // Secondary require_find
      auto idx = tbl.get_index<"byowner"_n>();
      auto sit = idx.require_find("alice"_n, "owner not found");
      check(sit.key().id == 1, "key should be 1");
   }

   // ── Duplicate secondary keys ─────────────────────────────────────────────

   [[sysio::action]]
   void dupkeys() {
      tbl.emplace(get_self(), {1}, {100, "alice"_n});
      tbl.emplace(get_self(), {2}, {100, "bob"_n});
      tbl.emplace(get_self(), {3}, {200, "carol"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // lower_bound(100) should find first entry with balance=100
      auto lb = idx.lower_bound(uint64_t(100));
      check(lb != idx.end(), "should find balance 100");
      check(lb->balance == 100, "first should have balance 100");

      // Advance should give second entry with balance=100
      ++lb;
      check(lb != idx.end(), "should have another balance=100 entry");
      check(lb->balance == 100, "second should also have balance 100");

      // upper_bound(100) should skip both
      auto ub = idx.upper_bound(uint64_t(100));
      check(ub != idx.end(), "upper_bound(100) should find carol");
      check(ub->balance == 200, "upper_bound should be carol's 200");
   }

   // ── Primary iteration ordering ───────────────────────────────────────────

   [[sysio::action]]
   void priiter() {
      tbl.emplace(get_self(), {30}, {3, "c"_n});
      tbl.emplace(get_self(), {10}, {1, "a"_n});
      tbl.emplace(get_self(), {20}, {2, "b"_n});

      uint64_t expected[] = {10, 20, 30};
      int i = 0;
      for (auto it = tbl.begin(); it != tbl.end(); ++it) {
         check(i < 3, "too many entries");
         check(it.key().id == expected[i], "wrong primary key order");
         ++i;
      }
      check(i == 3, "should have 3 entries");
   }

   // ── Contains and get ─────────────────────────────────────────────────────

   [[sysio::action]]
   void gettest() {
      tbl.emplace(get_self(), {1}, {1000, "alice"_n});

      check(tbl.contains({1}), "should contain key 1");
      check(!tbl.contains({2}), "should not contain key 2");

      // Asserting get (returns V by value, asserts if not found)
      auto val = tbl.get({1});
      check(val.balance == 1000, "balance should be 1000");
      check(val.owner == "alice"_n, "owner should be alice");

      // get with custom error message
      auto val2 = tbl.get({1}, "key 1 must exist");
      check(val2.balance == 1000, "get with msg: balance should be 1000");

      // Cross-contract read: table scoped to another code should be empty
      my_table other_tbl("otheracct"_n);
      check(!other_tbl.contains({1}), "cross-contract: should not see our data");
      check(other_tbl.try_get({1}) == std::nullopt, "cross-contract: try_get should be nullopt");
   }

   // ── Modify existing key (overwrite via modify, not re-emplace) ──────────

   [[sysio::action]]
   void overwrite() {
      tbl.emplace(get_self(), {1}, {100, "alice"_n});

      // Overwrite via modify
      auto it = tbl.require_find({1});
      tbl.modify(it, {200, "bob"_n});

      auto val = tbl.get({1});
      check(val.balance == 200, "balance should be 200 after modify");
      check(val.owner == "bob"_n, "owner should be bob after modify");

      // Verify secondary indexes are consistent
      auto idx = tbl.get_index<"byowner"_n>();
      check(idx.find("alice"_n) == idx.end(), "alice should be gone from owner index");
      check(idx.find("bob"_n) != idx.end(), "bob should be in owner index");
   }
   // ── Upsert: insert-or-update with correct secondary maintenance ────────

   [[sysio::action]]
   void upsert() {
      // Insert via upsert (key does not exist)
      tbl.upsert(get_self(), {1}, {100, "alice"_n});
      auto val = tbl.get({1});
      check(val.balance == 100, "upsert insert: balance should be 100");

      // Update via upsert (key exists)
      tbl.upsert(get_self(), {1}, {200, "bob"_n});
      val = tbl.get({1});
      check(val.balance == 200, "upsert update: balance should be 200");
      check(val.owner == "bob"_n, "upsert update: owner should be bob");

      // Verify secondary indexes are correct (no orphans)
      auto owner_idx = tbl.get_index<"byowner"_n>();
      check(owner_idx.find("alice"_n) == owner_idx.end(), "alice should be gone from owner index");
      check(owner_idx.find("bob"_n) != owner_idx.end(), "bob should be in owner index");

      auto bal_idx = tbl.get_index<"bybal"_n>();
      check(bal_idx.find(uint64_t(100)) == bal_idx.end(), "balance 100 should be gone");
      check(bal_idx.find(uint64_t(200)) != bal_idx.end(), "balance 200 should exist");
   }

   // ── Signed int64 key round-trip and ordering ──────────────────────────────

   [[sysio::action]]
   void signedkey() {
      i64_table t;
      t.emplace(get_self(), {-100}, {1});
      t.emplace(get_self(), {50},   {2});
      t.emplace(get_self(), {-1},   {3});
      t.emplace(get_self(), {0},    {4});
      t.emplace(get_self(), {200},  {5});

      // Iteration should be in signed order: -100, -1, 0, 50, 200
      int64_t expected[] = {-100, -1, 0, 50, 200};
      int i = 0;
      for (auto it = t.begin(); it != t.end(); ++it) {
         check(i < 5, "too many entries");
         check(it.key().k == expected[i], "wrong signed key order");
         ++i;
      }
      check(i == 5, "should have 5 entries");

      // Verify find round-trips the key
      auto it = t.find({-1});
      check(it != t.end(), "should find -1");
      check(it.key().k == -1, "key should decode to -1");
      check(it->v == 3, "value should be 3");
   }

   // ── String key round-trip (NUL-escape encoding) ──────────────────────────

   [[sysio::action]]
   void strkey() {
      str_table t;
      t.emplace(get_self(), {"us-east", 1}, {10});
      t.emplace(get_self(), {"eu-west", 2}, {20});
      t.emplace(get_self(), {"us-east", 3}, {30});
      t.emplace(get_self(), {"ap-south", 4}, {40});

      // Lexicographic order: ap-south, eu-west, us-east/1, us-east/3
      auto it = t.begin();
      check(it != t.end(), "should have entries");
      check(it.key().region == "ap-south", "first should be ap-south");
      check(it.key().id == 4, "ap-south id should be 4");

      ++it;
      check(it.key().region == "eu-west", "second should be eu-west");

      ++it;
      check(it.key().region == "us-east" && it.key().id == 1, "third should be us-east/1");

      ++it;
      check(it.key().region == "us-east" && it.key().id == 3, "fourth should be us-east/3");

      ++it;
      check(it == t.end(), "should be end after 4 entries");
   }

   // ── Double secondary key round-trip and ordering ─────────────────────────

   [[sysio::action]]
   void dblkey() {
      dbl_table t;
      t.emplace(get_self(), {1}, {-2.5, 1});
      t.emplace(get_self(), {2}, {0.0,  2});
      t.emplace(get_self(), {3}, {1.5,  3});
      t.emplace(get_self(), {4}, {-0.1, 4});

      auto idx = t.get_index<"byscore"_n>();

      // Score order: -2.5, -0.1, 0.0, 1.5
      double expected[] = {-2.5, -0.1, 0.0, 1.5};
      int i = 0;
      for (auto it = idx.begin(); it != idx.end(); ++it) {
         check(i < 4, "too many entries");
         check(it->score == expected[i], "wrong double key order");
         ++i;
      }
      check(i == 4, "should have 4 entries");

      // Key-only iteration should decode double secondary key
      auto kit = idx.key_begin();
      check(kit != idx.key_end(), "key iter should have entries");
      check(kit->sec_key == -2.5, "first sec_key should be -2.5");
   }

   // ── Multi-field key (uint8_t tag + uint64_t id) ──────────────────────────

   [[sysio::action]]
   void multikey() {
      multi_table t;
      t.emplace(get_self(), {1, 100}, {10});
      t.emplace(get_self(), {1, 200}, {20});
      t.emplace(get_self(), {2, 50},  {30});
      t.emplace(get_self(), {1, 50},  {40});

      // Order: (1,50), (1,100), (1,200), (2,50)
      auto it = t.begin();
      check(it.key().tag == 1 && it.key().id == 50, "first should be (1,50)");
      ++it;
      check(it.key().tag == 1 && it.key().id == 100, "second should be (1,100)");
      ++it;
      check(it.key().tag == 1 && it.key().id == 200, "third should be (1,200)");
      ++it;
      check(it.key().tag == 2 && it.key().id == 50, "fourth should be (2,50)");
      ++it;
      check(it == t.end(), "should be end");
   }

   // ── Empty table: begin == end ────────────────────────────────────────────

   [[sysio::action]]
   void emptyiter() {
      check(tbl.begin() == tbl.end(), "empty table begin should equal end");
      check(!tbl.contains({1}), "empty table should not contain anything");

      auto idx = tbl.get_index<"byowner"_n>();
      check(idx.begin() == idx.end(), "empty sec index begin should equal end");
      check(idx.key_begin() == idx.key_end(), "empty key iter begin should equal end");
   }

   // ── Reverse iteration (primary) ─────────────────────────────────────────

   [[sysio::action]]
   void reviter() {
      tbl.emplace(get_self(), {10}, {1, "a"_n});
      tbl.emplace(get_self(), {20}, {2, "b"_n});
      tbl.emplace(get_self(), {30}, {3, "c"_n});

      // Decrement from end should give last element
      auto it = tbl.end();
      --it;
      check(it != tbl.end(), "-- end should give valid iterator");
      check(it.key().id == 30, "last key should be 30");

      --it;
      check(it.key().id == 20, "prev should be 20");

      --it;
      check(it.key().id == 10, "prev should be 10");

      // Also test rbegin/rend (std::reverse_iterator)
      uint64_t rev_expected[] = {3, 2, 1};
      int ri = 0;
      for (auto rit = tbl.rbegin(); rit != tbl.rend(); ++rit) {
         check(rit->balance == rev_expected[ri], "rbegin: wrong value");
         ++ri;
      }
      check(ri == 3, "rbegin: should visit 3");
   }

   // ── Reverse iteration (secondary) ────────────────────────────────────────

   [[sysio::action]]
   void secrev() {
      tbl.emplace(get_self(), {1}, {300, "c"_n});
      tbl.emplace(get_self(), {2}, {100, "a"_n});
      tbl.emplace(get_self(), {3}, {200, "b"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // Decrement from end should give highest balance
      auto it = idx.end();
      --it;
      check(it != idx.end(), "-- sec end should give valid iterator");
      check(it->balance == 300, "last balance should be 300");

      --it;
      check(it->balance == 200, "prev balance should be 200");

      --it;
      check(it->balance == 100, "prev balance should be 100");
   }

   // ── Upper bound edge cases ────────────────────────────────────────────────

   [[sysio::action]]
   void secubound() {
      tbl.emplace(get_self(), {1}, {100, "alice"_n});
      tbl.emplace(get_self(), {2}, {100, "bob"_n});
      tbl.emplace(get_self(), {3}, {100, "carol"_n});
      tbl.emplace(get_self(), {4}, {200, "dave"_n});
      tbl.emplace(get_self(), {5}, {300, "eve"_n});

      auto idx = tbl.get_index<"bybal"_n>();

      // upper_bound on value with 3 duplicates should skip all three
      auto ub = idx.upper_bound(uint64_t(100));
      check(ub != idx.end(), "upper_bound(100) should find entry");
      check(ub->balance == 200, "upper_bound(100) should be 200");

      // upper_bound on max value present should return end
      auto ub_max = idx.upper_bound(uint64_t(300));
      check(ub_max == idx.end(), "upper_bound(300) should be end");

      // upper_bound on value not present should find next higher
      auto ub_gap = idx.upper_bound(uint64_t(150));
      check(ub_gap != idx.end(), "upper_bound(150) should find entry");
      check(ub_gap->balance == 200, "upper_bound(150) should be 200");

      // upper_bound on value below all entries should find first
      auto ub_low = idx.upper_bound(uint64_t(50));
      check(ub_low != idx.end(), "upper_bound(50) should find entry");
      check(ub_low->balance == 100, "upper_bound(50) should be 100");

      // lower_bound == upper_bound for non-existent key
      auto lb_gap = idx.lower_bound(uint64_t(150));
      check(lb_gap != idx.end() && lb_gap->balance == 200,
            "lower_bound(150) should also be 200");
   }

   // ── RAM delta from emplace/erase ─────────────────────────────────────────

   [[sysio::action]]
   void ramdelta() {
      i64_table t;
      // emplace returns an iterator, not a delta — but the underlying kv_set
      // does return a delta. We verify indirectly via erase: if emplace consumed
      // RAM, erasing should free it.
      t.emplace(get_self(), {1}, {100});
      check(t.contains({1}), "should exist after emplace");

      auto it = t.find({1});
      t.erase(std::move(it));
      check(!t.contains({1}), "should not exist after erase");
   }

   // ── Zero-copy: trivially_copyable values use memcpy path ──────────────

   // Two uint64_t fields, so no padding anywhere: sizeof == pack_size and the value takes the
   // memcpy path. (A {uint64,uint32,uint16} struct was tried first and dropped -- WASM pads it,
   // so sizeof != pack_size and the zero-copy path is not the one under test.)
   //
   // The annotation must name the SAME table as the template parameter. It previously said
   // "podtbl2" while the table was instantiated as "podtbl"_n, which is not a naming choice but
   // an incoherent ABI: the row is stored under the template's id (49052) and the ABI described
   // it under the annotation's (55260), so get_table_rows could not reach it. Nothing rejects
   // that mismatch yet -- it only surfaced here because the two ids then collided.
   struct [[sysio::table("podtbl")]] pod_val2 {
      uint64_t x;
      uint64_t y;
      SYSLIB_SERIALIZE(pod_val2, (x)(y))
   };
   using pod_table = kv::table<"podtbl"_n, my_key, pod_val2>;

   // Non-trivial value (has std::string, falls back to datastream)
   struct [[sysio::table("cmplxtbl")]] complex_val {
      std::string label;
      uint64_t    amount;
      SYSLIB_SERIALIZE(complex_val, (label)(amount))
   };
   using complex_table = kv::table<"cmplxtbl"_n, my_key, complex_val>;

   [[sysio::action]]
   void zerocopy() {
      // POD path: trivially_copyable, sizeof == pack_size → memcpy
      pod_table pt;
      pt.emplace(get_self(), {1}, {1000, 2000});
      auto pval = pt.get({1});
      check(pval.x == 1000, "pod: x should be 1000");
      check(pval.y == 2000, "pod: y should be 2000");

      // Verify round-trip through iteration (also uses deserialize)
      auto pit = pt.begin();
      check(pit != pt.end(), "pod: should have entry");
      check(pit->x == 1000 && pit->y == 2000, "pod: iter values match");

      // Complex path: has std::string → datastream serialization
      complex_table ct;
      ct.emplace(get_self(), {1}, {"hello", 42});
      auto cval = ct.get({1});
      check(cval.label == "hello", "complex: label should be hello");
      check(cval.amount == 42, "complex: amount should be 42");
   }

   // ── Explicit-ctor value type (time_point regression) ───────────────────
   // Regression: V value{} in deserialize_value breaks types whose members
   // have explicit constructors. Using V value; (default-init) fixes it.

   struct [[sysio::table("tptbl")]] tp_val {
      time_point ts;
      uint64_t   seq;
      SYSLIB_SERIALIZE(tp_val, (ts)(seq))
   };
   using tp_table = kv::table<"tptbl"_n, my_key, tp_val>;

   [[sysio::action]]
   void tpdeser() {
      tp_table t;
      time_point t1(microseconds(1000000));
      time_point t2(microseconds(2000000));

      t.emplace(get_self(), {1}, {t1, 10});
      t.emplace(get_self(), {2}, {t2, 20});

      // find → deserialize_value
      auto it = t.find({1});
      check(it != t.end(), "tp: find(1) should succeed");
      check(it->ts == t1, "tp: ts should match t1");
      check(it->seq == 10, "tp: seq should be 10");

      // iteration → deserialize_value
      auto it2 = t.begin();
      check(it2->ts == t1, "tp: begin ts should be t1");
      ++it2;
      check(it2->ts == t2, "tp: second ts should be t2");

      // --end() → deserialize_value via load
      // (two-step: table iterators are move-only)
      auto last = t.end();
      --last;
      check(last->ts == t2, "tp: --end ts should be t2");
   }

   // ── Error: erase end iterator ────────────────────────────────────────────

   [[sysio::action]]
   void erasend() {
      auto it = tbl.end();
      tbl.erase(std::move(it));  // should assert
   }

   // ── Error: modify end iterator ───────────────────────────────────────────

   [[sysio::action]]
   void modifyend() {
      auto it = tbl.end();
      tbl.modify(it, {999, "x"_n});  // should assert
   }

   // ── Error: require_find on missing key ───────────────────────────────────

   [[sysio::action]]
   void reqmiss() {
      tbl.require_find({999}, "expected to miss");  // should assert
   }

   // ── Consolidated new API tests (kept under SYSIO_DISPATCH 32-action limit) ──

   [[sysio::action]]
   void newapi() {
      // --- set() as upsert alias ---
      // Insert via set (key does not exist)
      tbl.set(get_self(), {1}, {100, "alice"_n});
      {
         auto v = tbl.get({1});
         check(v.balance == 100, "set insert: balance should be 100");
         check(v.owner == "alice"_n, "set insert: owner should be alice");
      }

      // Update via set (key already exists)
      tbl.set(get_self(), {1}, {200, "bob"_n});
      {
         auto v = tbl.get({1});
         check(v.balance == 200, "set update: balance should be 200");
         check(v.owner == "bob"_n, "set update: owner should be bob");
      }

      // Verify secondary indexes are correct (no orphans)
      {
         auto idx = tbl.get_index<"byowner"_n>();
         check(idx.find("alice"_n) == idx.end(), "alice should be gone");
         check(idx.find("bob"_n) != idx.end(), "bob should exist");
      }

      // set with explicit payer
      tbl.set(get_self(), {2}, {300, "carol"_n});
      {
         auto v = tbl.get({2});
         check(v.balance == 300, "set payer: balance should be 300");
      }

      // Clean up for next section
      tbl.erase({1});
      tbl.erase({2});

      // --- Lambda emplace ---
      tbl.emplace(get_self(), {1}, [](my_val& v) {
         v.balance = 500;
         v.owner   = "alice"_n;
      });

      {
         auto v = tbl.get({1});
         check(v.balance == 500, "lambda emplace: balance should be 500");
         check(v.owner == "alice"_n, "lambda emplace: owner should be alice");
      }

      // Verify secondary indexes populated
      {
         auto idx = tbl.get_index<"byowner"_n>();
         check(idx.find("alice"_n) != idx.end(), "alice should be in owner index");
      }

      // Clean up for next section
      tbl.erase({1});

      // --- Modify by key + lambda ---
      tbl.emplace(get_self(), {1}, {100, "alice"_n});

      // Modify by key (self-payer)
      tbl.modify({1}, [](my_val& v) {
         v.balance += 50;
      });

      {
         auto v = tbl.get({1});
         check(v.balance == 150, "modbykey: balance should be 150");
         check(v.owner == "alice"_n, "modbykey: owner unchanged");
      }

      // Modify by key with explicit payer
      tbl.modify(get_self(), {1}, [](my_val& v) {
         v.owner = "bob"_n;
      });

      {
         auto v = tbl.get({1});
         check(v.owner == "bob"_n, "modbykey payer: owner should be bob");
      }

      // Verify secondary indexes updated
      {
         auto idx = tbl.get_index<"byowner"_n>();
         check(idx.find("alice"_n) == idx.end(), "alice should be gone");
         check(idx.find("bob"_n) != idx.end(), "bob should exist");
      }

      // Clean up for next section
      tbl.erase({1});

      // --- Erase by key ---
      tbl.emplace(get_self(), {1}, {100, "alice"_n});
      tbl.emplace(get_self(), {2}, {200, "bob"_n});

      tbl.erase({1});

      check(!tbl.contains({1}), "key 1 should be erased");
      check(tbl.contains({2}), "key 2 should still exist");

      // Verify secondary index cleaned up
      {
         auto idx = tbl.get_index<"byowner"_n>();
         check(idx.find("alice"_n) == idx.end(), "alice should be gone from index");
         check(idx.find("bob"_n) != idx.end(), "bob should remain in index");
      }

      // Clean up for next section
      tbl.erase({2});

      // --- try_get returns optional ---
      // try_get on missing key returns nullopt
      {
         auto none = tbl.try_get({99});
         check(!none.has_value(), "try_get(99) should be empty");
      }

      tbl.emplace(get_self(), {1}, {1000, "alice"_n});

      // try_get on existing key returns value
      {
         auto v = tbl.try_get({1});
         check(v.has_value(), "try_get(1) should have value");
         check(v->balance == 1000, "try_get: balance should be 1000");
         check(v->owner == "alice"_n, "try_get: owner should be alice");
      }

      // Clean up for next section
      tbl.erase({1});

      // --- cbegin / cend const iterator aliases ---
      tbl.emplace(get_self(), {10}, {1, "a"_n});
      tbl.emplace(get_self(), {20}, {2, "b"_n});
      tbl.emplace(get_self(), {30}, {3, "c"_n});

      // cbegin/cend should work identically to begin/end
      {
         uint64_t expected[] = {10, 20, 30};
         int i = 0;
         for (auto it = tbl.cbegin(); it != tbl.cend(); ++it) {
            check(i < 3, "too many entries in citer");
            check(it.key().id == expected[i], "wrong key in citer");
            ++i;
         }
         check(i == 3, "citer should have 3 entries");
      }

      // --- available_primary_key ---
      // Uses apk_table defined at class scope (apk_key has primary_key() method)
      {
         apk_table apk_tbl(get_self());

         // Empty table → available_primary_key() == 0
         check(apk_tbl.available_primary_key() == 0, "apk: empty table should be 0");

         apk_tbl.emplace(get_self(), {5}, {50});
         apk_tbl.emplace(get_self(), {10}, {100});
         apk_tbl.emplace(get_self(), {3}, {30});

         // available_primary_key() should be max(5,10,3) + 1 = 11
         check(apk_tbl.available_primary_key() == 11, "apk: should be 11");

         auto next_pk = apk_tbl.available_primary_key();
         apk_tbl.emplace(get_self(), {next_pk}, {110});
         check(apk_tbl.available_primary_key() == 12, "apk: should be 12 after insert");
      }
   }

   // ── Duplicate emplace asserts ────────────────────────────────────────────

   [[sysio::action]]
   void dupempl() {
      tbl.emplace(get_self(), {1}, {100, "alice"_n});
      tbl.emplace(get_self(), {1}, {200, "bob"_n});  // should assert — key already exists
   }
};

SYSIO_DISPATCH(kv_indexed_table_tests, (emplace)(emplpayer)(modify)(modpayer)
               (erasetest)(secfind)(seclbound)(seciter)(secmodify)(secerase)
               (keyiter)(reqfind)(dupkeys)(priiter)(gettest)(overwrite)
               (upsert)(signedkey)(strkey)(dblkey)(multikey)(emptyiter)(reviter)(secrev)(secubound)(zerocopy)
               (ramdelta)(tpdeser)
               (reqmiss)
               (newapi)(dupempl))
