#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_table_tests")]] kv_table_tests : public contract {
public:
   using contract::contract;

   // ── Row type ─────────────────────────────────────────────────────────────

   struct [[sysio::table]] tbl_row {
      uint64_t    id;
      uint64_t    value;
      std::string label;

      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(tbl_row, (id)(value)(label))
   };

   using test_table = kv::table<"testtbl"_n, tbl_row>;

   // ── Basic CRUD ───────────────────────────────────────────────────────────

   [[sysio::action]]
   void emplacefind() {
      test_table t(get_self(), get_self().value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 100; r.label = "one"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 2; r.value = 200; r.label = "two"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 3; r.value = 300; r.label = "three"; });

      // find existing
      auto itr = t.find(2);
      check(itr != t.end(), "find(2) should succeed");
      check(itr->value == 200, "find(2) value mismatch");
      check(itr->label == "two", "find(2) label mismatch");

      // find non-existing
      check(t.find(99) == t.end(), "find(99) should be end");

      // contains
      check(t.contains(1), "contains(1) should be true");
      check(!t.contains(99), "contains(99) should be false");
   }

   [[sysio::action]]
   void modify() {
      test_table t(get_self(), "modify"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "orig"; });

      auto itr = t.find(1);
      check(itr != t.end(), "should find row");

      t.modify(itr, get_self(), [](tbl_row& r) {
         r.value = 42;
         r.label = "modified";
      });

      // Re-read and verify
      auto itr2 = t.find(1);
      check(itr2 != t.end(), "row should still exist");
      check(itr2->value == 42, "value should be 42");
      check(itr2->label == "modified", "label should be modified");
   }

   [[sysio::action]]
   void erase() {
      test_table t(get_self(), "erase"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "x"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 2; r.value = 20; r.label = "y"; });

      // Erase by object
      auto itr = t.find(1);
      check(itr != t.end(), "should find row 1");
      t.erase(itr);
      check(t.find(1) == t.end(), "row 1 should be gone");

      // Erase by pk
      t.erase(2);
      check(t.find(2) == t.end(), "row 2 should be gone");
   }

   // ── Iteration ────────────────────────────────────────────────────────────

   [[sysio::action]]
   void iterate() {
      test_table t(get_self(), "iter"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 30; r.value = 3; r.label = "c"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 10; r.value = 1; r.label = "a"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 20; r.value = 2; r.label = "b"; });

      // Forward iteration should be in primary key order
      uint64_t expected[] = {10, 20, 30};
      int idx = 0;
      for (auto it = t.begin(); it != t.end(); ++it) {
         check(idx < 3, "too many entries");
         check(it->id == expected[idx], "wrong order in forward iteration");
         ++idx;
      }
      check(idx == 3, "should have 3 entries");
   }

   [[sysio::action]]
   void lowerupper() {
      test_table t(get_self(), "bounds"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 10; r.value = 1; r.label = ""; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 20; r.value = 2; r.label = ""; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 30; r.value = 3; r.label = ""; });

      // lower_bound(15) should return pk=20
      auto lb = t.lower_bound(15);
      check(lb != t.end() && lb->id == 20, "lower_bound(15) should be pk 20");

      // lower_bound(20) should return pk=20 (exact match)
      lb = t.lower_bound(20);
      check(lb != t.end() && lb->id == 20, "lower_bound(20) should be pk 20");

      // upper_bound(20) should return pk=30
      auto ub = t.upper_bound(20);
      check(ub != t.end() && ub->id == 30, "upper_bound(20) should be pk 30");

      // upper_bound(30) should be end
      ub = t.upper_bound(30);
      check(ub == t.end(), "upper_bound(30) should be end");
   }

   // ── get() returns by value (no static aliasing) ──────────────────────────

   [[sysio::action]]
   void getbyval() {
      test_table t(get_self(), "getval"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 111; r.label = "first"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 2; r.value = 222; r.label = "second"; });

      // get() returns by value — two calls must not alias
      auto a = t.get(1);
      auto b = t.get(2);
      check(a.value == 111, "a should still be 111");
      check(b.value == 222, "b should be 222");
   }

   // ── require_find ─────────────────────────────────────────────────────────

   [[sysio::action]]
   void reqfind() {
      test_table t(get_self(), "reqfind"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 42; r.value = 999; r.label = ""; });

      auto itr = t.require_find(42);
      check(itr != t.end() && itr->value == 999, "require_find should succeed");
   }

   [[sysio::action]]
   void reqfindfail() {
      test_table t(get_self(), "reqffail"_n.value);
      // Should fail — table is empty
      t.require_find(1, "expected failure");
   }

   // ── available_primary_key ────────────────────────────────────────────────

   [[sysio::action]]
   void availpk() {
      test_table t(get_self(), "availpk"_n.value);

      // Empty table should return 0
      check(t.available_primary_key() == 0, "empty table should yield pk=0");

      t.emplace(get_self(), [](tbl_row& r) { r.id = 5; r.value = 0; r.label = ""; });
      check(t.available_primary_key() == 6, "after pk=5, next should be 6");

      t.emplace(get_self(), [](tbl_row& r) { r.id = 100; r.value = 0; r.label = ""; });
      check(t.available_primary_key() == 101, "after pk=100, next should be 101");
   }

   // ── Cross-scope iteration ────────────────────────────────────────────────

   [[sysio::action]]
   void crossscope() {
      // Write rows into two different scopes
      {
         test_table t1(get_self(), "scope1"_n.value);
         t1.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "s1"; });
      }
      {
         test_table t2(get_self(), "scope2"_n.value);
         t2.emplace(get_self(), [](tbl_row& r) { r.id = 2; r.value = 20; r.label = "s2"; });
      }

      // Cross-scope iteration should see both
      test_table t(get_self(), 0);
      uint32_t count = 0;
      for (auto it = t.begin_all_scopes(); it != t.end_all_scopes(); ++it) {
         ++count;
      }
      check(count == 2, "cross-scope should see exactly 2 rows");
   }

   // ── Empty table iteration ────────────────────────────────────────────────

   [[sysio::action]]
   void emptyiter() {
      test_table t(get_self(), "empty"_n.value);
      check(t.begin() == t.end(), "empty table: begin should equal end");
   }

   // ── cbegin / cend ─────────────────────────────────────────────────────────

   [[sysio::action]]
   void constiter() {
      test_table t(get_self(), "citer"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "a"; });
      t.emplace(get_self(), [](tbl_row& r) { r.id = 2; r.value = 20; r.label = "b"; });

      // cbegin/cend should work identically to begin/end
      auto cit = t.cbegin();
      check(cit != t.cend(), "cbegin should not equal cend");
      check(cit->id == 1, "cbegin should point to first row");
      ++cit;
      check(cit->id == 2, "second via cbegin");
      ++cit;
      check(cit == t.cend(), "should be cend after 2");
   }

   // ── erase by primary key ────────────────────────────────────────────────

   [[sysio::action]]
   void erasebypk() {
      test_table t(get_self(), "erbypk"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "x"; });
      check(t.contains(1), "should exist before erase");

      t.erase(1);
      check(!t.contains(1), "should be gone after erase by pk");
   }

   // ── set() convenience method ─────────────────────────────────────────────

   [[sysio::action]]
   void setmethod() {
      test_table t(get_self(), "setm"_n.value);

      tbl_row r{1, 42, "direct"};
      t.set(r.id, r);

      auto itr = t.find(1);
      check(itr != t.end(), "set row should exist");
      check(itr->value == 42, "set row value mismatch");
      check(itr->label == "direct", "set row label mismatch");

      // Overwrite
      tbl_row r2{1, 99, "updated"};
      t.set(r2.id, r2);
      auto itr2 = t.find(1);
      check(itr2 != t.end() && itr2->value == 99, "overwritten value mismatch");
   }

   // ── modify by object reference ───────────────────────────────────────────

   [[sysio::action]]
   void modifyobj() {
      test_table t(get_self(), "modobj"_n.value);

      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 10; r.label = "orig"; });

      auto obj = t.get(1);
      t.modify(obj, get_self(), [](tbl_row& r) {
         r.value = 77;
         r.label = "by_obj";
      });

      auto updated = t.get(1);
      check(updated.value == 77, "modifyobj: value should be 77");
      check(updated.label == "by_obj", "modifyobj: label should be by_obj");
   }

   // ── end_all_scopes explicit ──────────────────────────────────────────────

   // A separate table type for the end_all_scopes test so it starts empty
   struct [[sysio::table]] eas_row {
      uint64_t    id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(eas_row, (id))
   };
   using eas_table = kv::table<"eastbl"_n, eas_row>;

   // Negative: erase non-existent pk (T6)
   [[sysio::action]]
   void erasebadpk() {
      test_table t(get_self(), get_self().value);
      t.erase(99999); // should abort: key not found
   }

   // Negative: modify that changes primary key (T6)
   [[sysio::action]]
   void modifypk() {
      test_table t(get_self(), get_self().value);
      t.emplace(get_self(), [](tbl_row& r) { r.id = 1; r.value = 100; r.label = "orig"; });
      auto itr = t.find(1);
      t.modify(itr, get_self(), [](tbl_row& r) { r.id = 999; }); // should abort: cannot change pk
   }

   // ── Trivially-copyable value type (exercises is_fixed_serializable fast path) ──

   struct [[sysio::table]] pod_row {
      uint64_t id;
      uint64_t amount;
      uint64_t flags;  // uint64_t avoids trailing padding so sizeof==pack_size

      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(pod_row, (id)(amount)(flags))
   };
   using pod_table = kv::table<"podtbl"_n, pod_row>;

   [[sysio::action]]
   void podcrud() {
      pod_table t(get_self(), get_self().value);

      // Emplace
      t.emplace(get_self(), [](pod_row& r) { r.id = 1; r.amount = 1000; r.flags = 0x01; });
      t.emplace(get_self(), [](pod_row& r) { r.id = 2; r.amount = 2000; r.flags = 0x02; });

      // Get
      auto row = t.get(1);
      check(row.amount == 1000, "podcrud: get amount");
      check(row.flags == 0x01, "podcrud: get flags");

      // Find + dereference
      auto it = t.find(2);
      check(it != t.end(), "podcrud: find");
      check(it->amount == 2000, "podcrud: find amount");

      // Modify via iterator
      t.modify(it, get_self(), [](pod_row& r) { r.amount = 9999; r.flags = 0xFF; });
      auto updated = t.get(2);
      check(updated.amount == 9999, "podcrud: modify amount");
      check(updated.flags == 0xFF, "podcrud: modify flags");

      // Iterate
      uint32_t count = 0;
      for (auto i = t.begin(); i != t.end(); ++i) ++count;
      check(count == 2, "podcrud: iterate count");

      // Lower/upper bound
      auto lb = t.lower_bound(2);
      check(lb != t.end() && lb->id == 2, "podcrud: lower_bound");
      auto ub = t.upper_bound(1);
      check(ub != t.end() && ub->id == 2, "podcrud: upper_bound");

      // Contains
      check(t.contains(1), "podcrud: contains");
      check(!t.contains(999), "podcrud: !contains");

      // Erase
      t.erase(1);
      check(!t.contains(1), "podcrud: erase");

      // Available primary key
      check(t.available_primary_key() == 3, "podcrud: available_pk");
   }

   [[sysio::action]]
   void endallscope() {
      eas_table t(get_self(), 0);

      // Empty table — begin_all_scopes should equal end_all_scopes
      check(t.begin_all_scopes() == t.end_all_scopes(),
            "endallscope: empty should be begin==end");

      // Add data, then verify end is reachable
      {
         eas_table t1(get_self(), "eas"_n.value);
         t1.emplace(get_self(), [](eas_row& r) { r.id = 1; });
      }

      uint32_t count = 0;
      for (auto it = t.begin_all_scopes(); it != t.end_all_scopes(); ++it)
         ++count;
      check(count >= 1, "endallscope: should find at least 1 row");
   }
};

// static_assert after class — friend operators visible via ADL at this point
static_assert(sysio::kv::is_fixed_serializable_v<kv_table_tests::pod_row>,
              "pod_row must hit the zero-copy fast path");
