#include <sysio/sysio.hpp>
#include <sysio/kv_raw_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_raw_table_tests")]] kv_raw_table_tests : public contract {
public:
   using contract::contract;

   // ── Key/value types ──────────────────────────────────────────────────────

   struct uint_key {
      uint64_t k;
      SYSLIB_SERIALIZE(uint_key, (k))
   };
   struct uint_val {
      uint64_t v;
      SYSLIB_SERIALIZE(uint_val, (v))
   };

   kv::raw_table<uint_key, uint_val> uint_store;

   // ── Basic set/get/erase/contains ─────────────────────────────────────────

   [[sysio::action]]
   void setget() {
      uint_store.set({42}, {100});
      auto val = uint_store.get({42});
      check(val.has_value(), "get(42) should return value");
      check(val->v == 100, "value should be 100");

      // Non-existent
      auto no = uint_store.get({999});
      check(!no.has_value(), "get(999) should be empty");
   }

   [[sysio::action]]
   void erasetest() {
      uint_store.set({1}, {10});
      check(uint_store.contains({1}), "should contain key 1");

      uint_store.erase({1});
      check(!uint_store.contains({1}), "should not contain key 1 after erase");

      auto val = uint_store.get({1});
      check(!val.has_value(), "get should be empty after erase");
   }

   // ── Iteration ordering (unsigned) ────────────────────────────────────────

   [[sysio::action]]
   void uintorder() {
      uint_store.set({30}, {3});
      uint_store.set({10}, {1});
      uint_store.set({50}, {5});
      uint_store.set({20}, {2});
      uint_store.set({40}, {4});

      // Iteration should be in key order: 10, 20, 30, 40, 50
      uint64_t expected[] = {1, 2, 3, 4, 5};
      int idx = 0;
      for (auto it = uint_store.begin(); it != uint_store.end(); ++it) {
         check(idx < 5, "too many entries");
         check((*it).v == expected[idx], "wrong order in uint iteration");
         ++idx;
      }
      check(idx == 5, "should have 5 entries");
   }

   // ── Signed int64_t key ordering ──────────────────────────────────────────
   // be_key_stream must apply sign-bit flip so negatives sort before positives.

   struct i64_key {
      int64_t k;
      SYSLIB_SERIALIZE(i64_key, (k))
   };
   struct i64_val {
      int64_t original;
      SYSLIB_SERIALIZE(i64_val, (original))
   };

   kv::raw_table<i64_key, i64_val> i64_store;

   [[sysio::action]]
   void signedorder() {
      int64_t keys[] = {100, -50, 0, -100, 50, -1, 1};
      for (auto k : keys) {
         i64_store.set({k}, {k});
      }

      // Expected order: -100, -50, -1, 0, 1, 50, 100
      int64_t expected[] = {-100, -50, -1, 0, 1, 50, 100};
      int idx = 0;
      for (auto it = i64_store.begin(); it != i64_store.end(); ++it) {
         check(idx < 7, "too many entries");
         check((*it).original == expected[idx], "wrong signed key order");
         ++idx;
      }
      check(idx == 7, "should have 7 entries");
   }

   // ── Signed int32_t key ordering ──────────────────────────────────────────

   struct i32_key {
      int32_t k;
      SYSLIB_SERIALIZE(i32_key, (k))
   };
   struct i32_val {
      int32_t original;
      SYSLIB_SERIALIZE(i32_val, (original))
   };

   kv::raw_table<i32_key, i32_val> i32_store;

   [[sysio::action]]
   void signed32() {
      int32_t keys[] = {100, -50, 0, -100, 50};
      for (auto k : keys) {
         i32_store.set({k}, {k});
      }

      int32_t expected[] = {-100, -50, 0, 50, 100};
      int idx = 0;
      for (auto it = i32_store.begin(); it != i32_store.end(); ++it) {
         check(idx < 5, "too many entries");
         check((*it).original == expected[idx], "wrong int32 key order");
         ++idx;
      }
      check(idx == 5, "should have 5 entries");
   }

   // ── Signed int16_t key ordering ──────────────────────────────────────────

   struct i16_key {
      int16_t k;
      SYSLIB_SERIALIZE(i16_key, (k))
   };
   struct i16_val {
      int16_t original;
      SYSLIB_SERIALIZE(i16_val, (original))
   };

   kv::raw_table<i16_key, i16_val> i16_store;

   [[sysio::action]]
   void signedsm() {
      int16_t keys[] = {100, -50, 0, -100, 50};
      for (auto k : keys) {
         i16_store.set({k}, {k});
      }

      int16_t expected[] = {-100, -50, 0, 50, 100};
      int idx = 0;
      for (auto it = i16_store.begin(); it != i16_store.end(); ++it) {
         check(idx < 5, "too many entries");
         check((*it).original == expected[idx], "wrong int16 key order");
         ++idx;
      }
      check(idx == 5, "should have 5 entries");
   }

   // ── String key ordering ──────────────────────────────────────────────────

   struct str_key {
      std::string region;
      uint64_t    id;
      SYSLIB_SERIALIZE(str_key, (region)(id))
   };
   struct str_val {
      std::string payload;
      SYSLIB_SERIALIZE(str_val, (payload))
   };

   kv::raw_table<str_key, str_val> str_store;

   [[sysio::action]]
   void strorder() {
      str_store.set({"eu-west", 2}, {"ew2"});
      str_store.set({"us-east", 1}, {"ue1"});
      str_store.set({"eu-west", 1}, {"ew1"});
      str_store.set({"us-east", 2}, {"ue2"});

      // Expected order: eu-west/1, eu-west/2, us-east/1, us-east/2
      std::string expected[] = {"ew1", "ew2", "ue1", "ue2"};
      int idx = 0;
      for (auto it = str_store.begin(); it != str_store.end(); ++it) {
         check(idx < 4, "too many entries");
         check((*it).payload == expected[idx], "wrong string key order");
         ++idx;
      }
      check(idx == 4, "should have 4 entries");
   }

   // ── lower_bound / upper_bound ────────────────────────────────────────────

   [[sysio::action]]
   void bounds() {
      uint_store.set({10}, {1});
      uint_store.set({20}, {2});
      uint_store.set({30}, {3});

      // lower_bound(15) should find key=20
      auto lb = uint_store.lower_bound({15});
      check(lb != uint_store.end(), "lower_bound(15) should not be end");
      check((*lb).v == 2, "lower_bound(15) value should be 2");

      // lower_bound(20) should find key=20 (exact)
      lb = uint_store.lower_bound({20});
      check(lb != uint_store.end() && (*lb).v == 2, "lower_bound(20) exact match");

      // upper_bound(20) should find key=30
      auto ub = uint_store.upper_bound({20});
      check(ub != uint_store.end() && (*ub).v == 3, "upper_bound(20) should be 3");

      // upper_bound(30) should be end
      ub = uint_store.upper_bound({30});
      check(ub == uint_store.end(), "upper_bound(30) should be end");
   }

   // ── Empty map iteration ──────────────────────────────────────────────────

   [[sysio::action]]
   void emptyiter() {
      kv::raw_table<uint_key, uint_val> empty_store;
      check(empty_store.begin() == empty_store.end(), "empty map: begin==end");
   }

   // ── Overwrite existing key ───────────────────────────────────────────────

   [[sysio::action]]
   void overwrite() {
      uint_store.set({1}, {100});
      uint_store.set({1}, {200});

      auto val = uint_store.get({1});
      check(val.has_value() && val->v == 200, "overwrite should update value");
   }

   // ── Reverse iteration ────────────────────────────────────────────────────

   [[sysio::action]]
   void reviter() {
      uint_store.set({10}, {1});
      uint_store.set({20}, {2});
      uint_store.set({30}, {3});

      // Backward: should visit 30, 20, 10
      uint64_t expected[] = {3, 2, 1};
      int idx = 0;
      auto it = uint_store.end();
      while (it != uint_store.begin()) {
         --it;
         check(idx < 3, "too many entries in reverse");
         check((*it).v == expected[idx], "wrong reverse order");
         ++idx;
      }
      check(idx == 3, "should have 3 entries in reverse");
   }

   // ── Float key ordering ──────────────────────────────────────────────────

   struct flt_key {
      float k;
      SYSLIB_SERIALIZE(flt_key, (k))
   };
   struct flt_val {
      float original;
      SYSLIB_SERIALIZE(flt_val, (original))
   };

   kv::raw_table<flt_key, flt_val> flt_store;

   [[sysio::action]]
   void floatorder() {
      float keys[] = {1.5f, -0.5f, 0.0f, -1.5f, 0.5f, -0.0f};
      for (auto k : keys) {
         flt_store.set({k}, {k});
      }

      // -0.0 (0x80000000) and +0.0 (0x00000000) are different bit patterns,
      // so they are different keys. After sign-magnitude flip:
      //   -0.0 -> ~0x80000000 = 0x7FFFFFFF, +0.0 -> 0x80000000
      // So -0.0 sorts just before +0.0.
      // Expected order: -1.5, -0.5, -0.0, +0.0, 0.5, 1.5
      float expected[] = {-1.5f, -0.5f, -0.0f, 0.0f, 0.5f, 1.5f};
      int idx = 0;
      for (auto it = flt_store.begin(); it != flt_store.end(); ++it) {
         check(idx < 6, "too many float entries");
         // Use memcmp to distinguish -0.0 from +0.0 (they compare equal with ==)
         uint32_t actual_bits, expected_bits;
         float actual = (*it).original;
         __builtin_memcpy(&actual_bits, &actual, 4);
         __builtin_memcpy(&expected_bits, &expected[idx], 4);
         check(actual_bits == expected_bits, "wrong float key order");
         ++idx;
      }
      check(idx == 6, "should have 6 float entries");
   }

   // ── Double key ordering ─────────────────────────────────────────────────

   struct dbl_key {
      double k;
      SYSLIB_SERIALIZE(dbl_key, (k))
   };
   struct dbl_val {
      double original;
      SYSLIB_SERIALIZE(dbl_val, (original))
   };

   kv::raw_table<dbl_key, dbl_val> dbl_store;

   [[sysio::action]]
   void dblorder() {
      double keys[] = {1.5, -0.5, 0.0, -1.5, 0.5};
      for (auto k : keys) {
         dbl_store.set({k}, {k});
      }

      double expected[] = {-1.5, -0.5, 0.0, 0.5, 1.5};
      int idx = 0;
      for (auto it = dbl_store.begin(); it != dbl_store.end(); ++it) {
         check(idx < 5, "too many double entries");
         check((*it).original == expected[idx], "wrong double key order");
         ++idx;
      }
      check(idx == 5, "should have 5 double entries");
   }

   // ── int128_t key ordering ───────────────────────────────────────────────

   struct i128_key {
      int128_t k;
      SYSLIB_SERIALIZE(i128_key, (k))
   };
   struct i128_val {
      int128_t original;
      SYSLIB_SERIALIZE(i128_val, (original))
   };

   kv::raw_table<i128_key, i128_val> i128_store;

   [[sysio::action]]
   void signedi() {
      int128_t keys[] = {100, -50, 0, -100, 50};
      for (auto k : keys) {
         i128_store.set({k}, {k});
      }

      int128_t expected[] = {-100, -50, 0, 50, 100};
      int idx = 0;
      for (auto it = i128_store.begin(); it != i128_store.end(); ++it) {
         check(idx < 5, "too many int128 entries");
         check((*it).original == expected[idx], "wrong int128 key order");
         ++idx;
      }
      check(idx == 5, "should have 5 int128 entries");
   }

   // ── String with embedded NUL ────────────────────────────────────────────
   // NUL-escape encoding must sort strings with embedded 0x00 correctly.

   struct nullstr_key {
      std::string k;
      SYSLIB_SERIALIZE(nullstr_key, (k))
   };
   struct nullstr_val {
      std::string label;
      SYSLIB_SERIALIZE(nullstr_val, (label))
   };

   kv::raw_table<nullstr_key, nullstr_val> nullstr_store;

   [[sysio::action]]
   void strnul() {
      // "a\0b" (3 bytes) should sort after "a" (1 byte) and before "b" (1 byte)
      std::string a_nul_b({'a', '\0', 'b'});
      std::string a("a");
      std::string b("b");

      nullstr_store.set({b}, {"third"});
      nullstr_store.set({a_nul_b}, {"second"});
      nullstr_store.set({a}, {"first"});

      std::string expected[] = {"first", "second", "third"};
      int idx = 0;
      for (auto it = nullstr_store.begin(); it != nullstr_store.end(); ++it) {
         check(idx < 3, "too many nullstr entries");
         check((*it).label == expected[idx], "wrong nullstr sort order");
         ++idx;
      }
      check(idx == 3, "should have 3 nullstr entries");
   }

   // ── vector<char> key ────────────────────────────────────────────────────

   struct blob_key {
      std::vector<char> k;
      SYSLIB_SERIALIZE(blob_key, (k))
   };
   struct blob_val {
      uint32_t label;
      SYSLIB_SERIALIZE(blob_val, (label))
   };

   kv::raw_table<blob_key, blob_val> blob_store;

   [[sysio::action]]
   void blobkey() {
      std::vector<char> k1 = {'a', '\0'};          // "a\0"
      std::vector<char> k2 = {'a', '\0', '\x01'};  // "a\0\x01"
      std::vector<char> k3 = {'a', '\x01'};         // "a\x01"

      blob_store.set({k3}, {3});
      blob_store.set({k1}, {1});
      blob_store.set({k2}, {2});

      // Expected: "a\0" < "a\0\x01" < "a\x01"
      uint32_t expected[] = {1, 2, 3};
      int idx = 0;
      for (auto it = blob_store.begin(); it != blob_store.end(); ++it) {
         check(idx < 3, "too many blob entries");
         check((*it).label == expected[idx], "wrong blob key order");
         ++idx;
      }
      check(idx == 3, "should have 3 blob entries");
   }

   // ── Cross-contract read via code parameter (N3) ──────────────────────────

   [[sysio::action]]
   void crossread() {
      // Write data with default (self) table
      uint_store.set({77}, {770});

      // Read back with explicit code = get_self()
      kv::raw_table<uint_key, uint_val> reader(get_self());
      auto val = reader.get({77});
      check(val.has_value(), "crossread: should find key 77 via explicit code");
      check(val->v == 770, "crossread: value should be 770");
      check(reader.contains({77}), "crossread: contains should return true");

      // Iterate with explicit code
      int count = 0;
      for (auto it = reader.begin(); it != reader.end(); ++it) {
         ++count;
      }
      check(count >= 1, "crossread: should iterate at least 1 entry");
   }

   // ── Negative: erase non-existent key (T1) ────────────────────────────────

   [[sysio::action]]
   void erasebad() {
      // Erasing a key that was never set should abort
      uint_store.erase({99999});
   }

   // ── Edge: INT64_MIN, INT64_MAX, 0 ───────────────────────────────────────

   [[sysio::action]]
   void signededge() {
      int64_t keys[] = {
         std::numeric_limits<int64_t>::max(),
         0,
         std::numeric_limits<int64_t>::min(),
         1,
         -1
      };
      for (auto k : keys) {
         i64_store.set({k}, {k});
      }

      // Expected: INT64_MIN, -1, 0, 1, INT64_MAX
      int64_t expected[] = {
         std::numeric_limits<int64_t>::min(),
         -1, 0, 1,
         std::numeric_limits<int64_t>::max()
      };
      int idx = 0;
      for (auto it = i64_store.begin(); it != i64_store.end(); ++it) {
         check(idx < 5, "too many entries");
         check((*it).original == expected[idx], "wrong edge-case key order");
         ++idx;
      }
      check(idx == 5, "should have 5 entries");
   }

   // ── T5: Zero-length value ─────────────────────────────────────────────────

   struct tiny_val {
      uint8_t dummy = 0;
      SYSLIB_SERIALIZE(tiny_val, (dummy))
   };

   kv::raw_table<uint_key, tiny_val> tinyval_store;

   [[sysio::action]]
   void zeroval() {
      // Test with minimal 1-byte value (empty structs can't use SYSLIB_SERIALIZE)
      tinyval_store.set({1}, {0});
      check(tinyval_store.contains({1}), "zeroval: key should exist");
      auto val = tinyval_store.get({1});
      check(val.has_value(), "zeroval: get should return value");

      tinyval_store.erase({1});
      check(!tinyval_store.contains({1}), "zeroval: key should not exist after erase");
   }

   // ── T5: RAM delta from set/erase ─────────────────────────────────────────

   [[sysio::action]]
   void ramdelta() {
      int64_t delta_set = uint_store.set({500}, {5000});
      check(delta_set > 0, "ramdelta: set should return positive delta for new key");

      // Overwrite same key — delta may be 0 (same size) but should not be negative
      int64_t delta_overwrite = uint_store.set({500}, {9999});
      check(delta_overwrite == 0, "ramdelta: overwrite same-size value should return 0 delta");

      int64_t delta_erase = uint_store.erase({500});
      check(delta_erase < 0, "ramdelta: erase should return negative delta");
   }
};
