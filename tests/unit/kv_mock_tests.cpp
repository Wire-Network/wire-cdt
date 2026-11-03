/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Pins the checks kv_mock.hpp makes before it reaches a row, which are what lets a wrapper test
 *  fail for the reason the chain would fail it. Each case names a rule in wire-sysio's
 *  apply_context.cpp, kv_context.hpp or kv_database.cpp, and the text is the host's own.
 *
 *  tests/unit/kv_table_tests.cpp and kv_multi_index_tests.cpp cover what the mock does with a row.
 */

#include <sysio/tester.hpp>

#include "kv_mock.hpp"

#include <cstring>
#include <string>
#include <vector>

using namespace sysio;
using kv_mock::host;

namespace {

constexpr uint64_t owner   = "alice"_n.value;
constexpr uint64_t other   = "bob"_n.value;
constexpr uint32_t table   = 42;
constexpr uint32_t too_big = kv_mock::max_table_id + 1;

/// Reset the mock on the receiver and install it.
void begin_case() {
   host().reset(owner);
   kv_mock::install();
}

int32_t put(const std::string& key, const std::string& value, uint64_t payer = owner, kv_set_mode mode = kv_set_upsert) {
   return ::kv_set(table, payer, key.data(), key.size(), value.data(), value.size(), mode);
}

int32_t put_entry(const std::string& pri, const std::string& sec, uint64_t payer = owner) {
   return ::kv_idx_store(table, payer, pri.data(), pri.size(), sec.data(), sec.size());
}

int32_t open_primary(uint32_t table_id = table) { return ::kv_it_create(table_id, owner, nullptr, 0, nullptr, 0); }
int32_t open_secondary(uint32_t table_id = table) { return ::kv_idx_create(table_id, owner, nullptr, 0, nullptr, 0); }

/// Read a handle of either family, discarding what comes back.
int32_t read_primary(int32_t handle) {
   uint32_t key_size = 0;
   uint32_t value_size = 0;
   return ::kv_it_read(handle, 0, nullptr, 0, nullptr, 0, &key_size, &value_size);
}

int32_t read_secondary(int32_t handle) {
   uint32_t sec_size = 0;
   uint32_t pri_size = 0;
   return ::kv_idx_read(handle, nullptr, 0, nullptr, 0, &sec_size, &pri_size);
}

/// The payer on record for a row of the receiver.
uint64_t row_payer(const std::string& key) {
   return host().row_payers.at(kv_mock::store::row_key{owner, table, key});
}

/// The payer on record for an index entry of the receiver.
uint64_t entry_payer(const std::string& sec, const std::string& pri) {
   return host().entries.at(kv_mock::store::idx_key{owner, table, sec, pri});
}

} // namespace

// Every write refuses a read-only transaction before anything else, each in its own words.
SYSIO_TEST_BEGIN(read_only_refuses_all_five_writes)
   begin_case();
   CHECK_EQUAL(put("k", "v"), kv_status_ok)
   CHECK_EQUAL(put_entry("k", "s"), kv_status_ok)
   const std::string key = "k";
   const std::string sec = "s";
   host().read_only = true;

   CHECK_ASSERT("cannot store a KV record when executing a readonly transaction", ([&]() { put("k", "v"); }))
   CHECK_ASSERT("cannot erase a KV record when executing a readonly transaction",
                ([&]() { ::kv_erase(table, key.data(), key.size()); }))
   CHECK_ASSERT("cannot store a KV index when executing a readonly transaction", ([&]() { put_entry("k", "s"); }))
   CHECK_ASSERT("cannot remove a KV index when executing a readonly transaction",
                ([&]() { ::kv_idx_erase(table, key.data(), key.size(), sec.data(), sec.size()); }))
   CHECK_ASSERT("cannot update a KV index when executing a readonly transaction", ([&]() {
      ::kv_idx_update(table, owner, key.data(), key.size(), sec.data(), sec.size(), sec.data(), sec.size());
   }))
SYSIO_TEST_END

// A miss makes no difference: the refusal comes before the lookup. The wrappers rely on knowing this.
SYSIO_TEST_BEGIN(read_only_refuses_a_write_that_would_miss)
   begin_case();
   host().read_only = true;
   const std::string key = "absent";
   CHECK_ASSERT("cannot erase a KV record when executing a readonly transaction",
                ([&]() { ::kv_erase(table, key.data(), key.size()); }))
SYSIO_TEST_END

// Reads carry on.
SYSIO_TEST_BEGIN(read_only_allows_reads)
   begin_case();
   CHECK_EQUAL(put("k", "v"), kv_status_ok)
   host().read_only = true;
   const std::string key = "k";
   CHECK_EQUAL(::kv_get(table, owner, key.data(), key.size(), 0, nullptr, 0), 1)
   const int32_t handle = open_primary();
   CHECK_EQUAL(read_primary(handle), kv_status_ok)
   ::kv_it_destroy(handle);
SYSIO_TEST_END

// No row has an empty key, so every call that takes a key refuses one.
SYSIO_TEST_BEGIN(empty_keys_abort_where_the_host_checks)
   begin_case();
   CHECK_ASSERT("KV key must not be empty", ([&]() { put("", "v"); }))
   CHECK_ASSERT("KV key must not be empty", ([&]() { ::kv_erase(table, nullptr, 0); }))
   CHECK_ASSERT("KV secondary key must not be empty", ([&]() { put_entry("k", ""); }))
   CHECK_ASSERT("KV primary key must not be empty", ([&]() { put_entry("", "s"); }))
   const std::string pri = "k";
   const std::string sec = "s";
   CHECK_EQUAL(put_entry(pri, sec), kv_status_ok)
   CHECK_ASSERT("KV secondary key must not be empty", ([&]() {
      ::kv_idx_update(table, owner, pri.data(), pri.size(), sec.data(), sec.size(), nullptr, 0);
   }))
   CHECK_ASSERT("KV secondary key must not be empty", ([&]() {
      ::kv_idx_update(table, owner, pri.data(), pri.size(), nullptr, 0, sec.data(), sec.size());
   }))
   CHECK_ASSERT("KV primary key must not be empty", ([&]() {
      ::kv_idx_update(table, owner, nullptr, 0, sec.data(), sec.size(), sec.data(), sec.size());
   }))
   CHECK_ASSERT("KV key must not be empty", ([&]() { ::kv_get(table, owner, nullptr, 0, 0, nullptr, 0); }))
   CHECK_ASSERT("KV primary key must not be empty", ([&]() {
      ::kv_idx_erase(table, nullptr, 0, sec.data(), sec.size());
   }))
   CHECK_ASSERT("KV secondary key must not be empty", ([&]() {
      ::kv_idx_erase(table, pri.data(), pri.size(), nullptr, 0);
   }))
   CHECK_ASSERT("KV secondary key must not be empty", ([&]() { ::kv_idx_find(table, owner, nullptr, 0, nullptr, 0); }))
SYSIO_TEST_END

// A find's key has to start with its prefix, however long the key is.
SYSIO_TEST_BEGIN(idx_find_refuses_a_key_outside_its_prefix)
   begin_case();
   const std::string pri = "k";
   const std::string prefix = "ab";
   const std::string inside = "abc";
   const std::string outside = "ac";
   const std::string shorter = "a";
   CHECK_EQUAL(put_entry(pri, inside), kv_status_ok)

   const int32_t handle = ::kv_idx_find(table, owner, prefix.data(), prefix.size(), inside.data(), inside.size());
   CHECK_EQUAL(handle >= 0, true)
   ::kv_idx_destroy(handle);
   CHECK_EQUAL(::kv_idx_find(table, owner, prefix.data(), prefix.size(), prefix.data(), prefix.size()), -1)
   CHECK_ASSERT("KV secondary key does not start with the prefix", ([&]() {
      ::kv_idx_find(table, owner, prefix.data(), prefix.size(), outside.data(), outside.size());
   }))
   CHECK_ASSERT("KV secondary key does not start with the prefix", ([&]() {
      ::kv_idx_find(table, owner, prefix.data(), prefix.size(), shorter.data(), shorter.size());
   }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(sizes_stop_at_the_limits)
   begin_case();
   const std::string longest_key(kv_mock::default_max_key_size, 'k');
   const std::string long_key(kv_mock::default_max_key_size + 1, 'k');
   const std::string largest_value(kv_mock::default_max_value_size, 'v');
   const std::string large_value(kv_mock::default_max_value_size + 1, 'v');
   const std::string longest_sec(kv_mock::default_max_secondary_key_size, 's');
   const std::string long_sec(kv_mock::default_max_secondary_key_size + 1, 's');

   CHECK_EQUAL(put(longest_key, largest_value), kv_status_ok)
   CHECK_ASSERT("KV key size 257 exceeds maximum 256", ([&]() { put(long_key, "v"); }))
   CHECK_ASSERT("KV value size 262145 exceeds maximum 262144", ([&]() { put("k", large_value); }))

   CHECK_EQUAL(put_entry(longest_key, longest_sec), kv_status_ok)
   CHECK_ASSERT("KV secondary key size 257 exceeds maximum 256", ([&]() { put_entry("k", long_sec); }))
   CHECK_ASSERT("KV primary key size 257 exceeds maximum 256", ([&]() { put_entry(long_key, "s"); }))
   CHECK_ASSERT("KV secondary key size 257 exceeds maximum 256", ([&]() {
      ::kv_idx_update(table, owner, longest_key.data(), longest_key.size(), longest_sec.data(), longest_sec.size(),
                      long_sec.data(), long_sec.size());
   }))
SYSIO_TEST_END

// The limits are fields, so a case can run at the raised values governance allows.
SYSIO_TEST_BEGIN(raised_limits_apply)
   begin_case();
   host().max_key_size = sysio::kv::kv_key_max_bytes;
   CHECK_EQUAL(put(std::string(sysio::kv::kv_key_max_bytes, 'k'), "v"), kv_status_ok)
   CHECK_ASSERT("KV key size 513 exceeds maximum 512",
                ([&]() { put(std::string(sysio::kv::kv_key_max_bytes + 1, 'k'), "v"); }))
SYSIO_TEST_END

// The prefix has a fixed cap that does not move with the limits, and a seek has none at all.
SYSIO_TEST_BEGIN(prefix_stops_at_its_cap_and_a_seek_does_not)
   begin_case();
   const std::string longest(kv_mock::max_prefix_size, 'p');
   const std::string too_long(kv_mock::max_prefix_size + 1, 'p');

   const int32_t primary = ::kv_it_create(table, owner, longest.data(), longest.size(), nullptr, 0);
   ::kv_it_destroy(primary);
   const int32_t secondary = ::kv_idx_create(table, owner, longest.data(), longest.size(), nullptr, 0);
   ::kv_idx_destroy(secondary);

   CHECK_ASSERT("KV iterator prefix size 513 exceeds limit 512",
                ([&]() { ::kv_it_create(table, owner, too_long.data(), too_long.size(), nullptr, 0); }))
   CHECK_ASSERT("KV iterator prefix size 513 exceeds limit 512",
                ([&]() { ::kv_idx_create(table, owner, too_long.data(), too_long.size(), nullptr, 0); }))
   CHECK_ASSERT("KV iterator prefix size 513 exceeds limit 512", ([&]() {
      ::kv_idx_find(table, owner, too_long.data(), too_long.size(), too_long.data(), too_long.size());
   }))

   const int32_t seeking = ::kv_it_create(table, owner, nullptr, 0, too_long.data(), too_long.size());
   CHECK_EQUAL(read_primary(seeking), kv_status_end)
   ::kv_it_destroy(seeking);
SYSIO_TEST_END

// Each pool has its own 1024 slots, and a slot given back is the next one out.
SYSIO_TEST_BEGIN(iterator_limit_applies_per_pool)
   begin_case();
   std::vector<int32_t> primaries;
   for (uint32_t i = 0; i < kv_mock::max_iterators; ++i) primaries.push_back(open_primary());
   CHECK_ASSERT("exceeded maximum number of KV primary iterators (1024)", ([&]() { open_primary(); }))

   const int32_t secondary = open_secondary();
   CHECK_EQUAL(secondary >= 0, true)
   ::kv_idx_destroy(secondary);

   ::kv_it_destroy(primaries[7]);
   CHECK_EQUAL(open_primary(), primaries[7])

   std::vector<int32_t> secondaries;
   for (uint32_t i = 0; i < kv_mock::max_iterators; ++i) secondaries.push_back(open_secondary());
   CHECK_ASSERT("exceeded maximum number of KV secondary iterators (1024)", ([&]() { open_secondary(); }))
SYSIO_TEST_END

// A secondary handle carries bit 16, so the two pools can never pass for each other.
SYSIO_TEST_BEGIN(handles_carry_their_pool)
   begin_case();
   CHECK_EQUAL(put("k", "v"), kv_status_ok)
   CHECK_EQUAL(put_entry("k", "s"), kv_status_ok)
   const int32_t primary = open_primary();
   const int32_t secondary = open_secondary();
   CHECK_EQUAL(primary, 0)
   CHECK_EQUAL(secondary, static_cast<int32_t>(kv_mock::secondary_handle_tag))

   CHECK_ASSERT("kv_it_read called on secondary iterator handle", ([&]() { read_primary(secondary); }))
   CHECK_ASSERT("kv_idx_read called on primary iterator handle", ([&]() { read_secondary(primary); }))
   CHECK_ASSERT("kv_it_destroy called on secondary iterator handle", ([&]() { ::kv_it_destroy(secondary); }))
   CHECK_ASSERT("kv_idx_destroy called on primary iterator handle", ([&]() { ::kv_idx_destroy(primary); }))
SYSIO_TEST_END

// A handle with a bit outside its layout is refused before anything looks at the slot, and -1 is one.
SYSIO_TEST_BEGIN(fabricated_handles_are_refused)
   begin_case();
   const int32_t primary = open_primary();
   const int32_t reserved = primary | static_cast<int32_t>(1 << 10);
   CHECK_ASSERT("KV iterator handle has reserved bits set: 1024", ([&]() { read_primary(reserved); }))
   CHECK_ASSERT("KV iterator handle has reserved bits set: 4294967295", ([&]() { ::kv_it_destroy(-1); }))
   CHECK_ASSERT("KV iterator handle has reserved bits set: 4294967295", ([&]() { ::kv_idx_destroy(-1); }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(destroyed_handles_are_refused)
   begin_case();
   const int32_t primary = open_primary();
   ::kv_it_destroy(primary);
   CHECK_ASSERT("invalid KV primary iterator slot 0", ([&]() { ::kv_it_destroy(primary); }))
   const int32_t secondary = open_secondary();
   ::kv_idx_destroy(secondary);
   CHECK_ASSERT("invalid KV secondary iterator slot 0", ([&]() { read_secondary(secondary); }))
SYSIO_TEST_END

// table_id arrives as a uint32_t and the chain stores 16 bits of it. Every call that takes one checks.
SYSIO_TEST_BEGIN(table_id_above_16_bits_aborts_on_every_call)
   begin_case();
   const std::string key = "k";
   const char* expected = "table_id 65536 exceeds maximum 65535";
   const auto refuses = [&](auto call) {
      CHECK_ASSERT(([&](std::string_view message) { return message == expected; }), call)
   };
   refuses([&]() { ::kv_set(too_big, owner, key.data(), key.size(), key.data(), key.size(), kv_set_upsert); });
   refuses([&]() { ::kv_erase(too_big, key.data(), key.size()); });
   refuses([&]() { ::kv_get(too_big, owner, key.data(), key.size(), 0, nullptr, 0); });
   refuses([&]() { ::kv_idx_store(too_big, owner, key.data(), key.size(), key.data(), key.size()); });
   refuses([&]() { ::kv_idx_erase(too_big, key.data(), key.size(), key.data(), key.size()); });
   refuses([&]() {
      ::kv_idx_update(too_big, owner, key.data(), key.size(), key.data(), key.size(), key.data(), key.size());
   });
   refuses([&]() { open_primary(too_big); });
   refuses([&]() { open_secondary(too_big); });
   refuses([&]() { ::kv_idx_find(too_big, owner, nullptr, 0, key.data(), key.size()); });

   CHECK_EQUAL(::kv_set(kv_mock::max_table_id, owner, key.data(), key.size(), key.data(), key.size(), kv_set_upsert),
               kv_status_ok)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(kv_set_refuses_an_unknown_mode)
   begin_case();
   const std::string key = "k";
   CHECK_ASSERT("kv_set mode 3 is not upsert, insert or update",
                ([&]() { ::kv_set(table, owner, key.data(), key.size(), key.data(), key.size(), 3); }))
SYSIO_TEST_END

// Payer 0 fails only when it would create. On an existing entry the host reports `exists` instead.
SYSIO_TEST_BEGIN(kv_idx_store_payer_zero_order)
   begin_case();
   CHECK_EQUAL(put_entry("k", "s"), kv_status_ok)
   CHECK_EQUAL(put_entry("k", "s", 0), kv_status_exists)
   CHECK_ASSERT("must specify a valid account to pay for new record", ([&]() { put_entry("k", "t", 0); }))
SYSIO_TEST_END

// kv_set has the same order on insert, and payer 0 on an update keeps who pays.
SYSIO_TEST_BEGIN(kv_set_payer_rules)
   begin_case();
   CHECK_EQUAL(put("k", "v", other), kv_status_ok)
   CHECK_EQUAL(row_payer("k"), other)

   CHECK_EQUAL(put("k", "w", 0, kv_set_insert), kv_status_exists)
   CHECK_ASSERT("must specify a valid account to pay for new record", ([&]() { put("n", "v", 0, kv_set_insert); }))
   CHECK_ASSERT("must specify a valid account to pay for new record", ([&]() { put("n", "v", 0); }))
   CHECK_EQUAL(put("n", "v", 0, kv_set_update), kv_status_no_row)

   CHECK_EQUAL(put("k", "w", 0), kv_status_ok)
   CHECK_EQUAL(row_payer("k"), other)
   CHECK_EQUAL(put("k", "x", 0, kv_set_update), kv_status_ok)
   CHECK_EQUAL(row_payer("k"), other)

   CHECK_EQUAL(put("k", "y", owner), kv_status_ok)
   CHECK_EQUAL(row_payer("k"), owner)
SYSIO_TEST_END

// An erased row takes its payer with it, so a new row at the key starts clean.
SYSIO_TEST_BEGIN(erase_drops_the_row_payer)
   begin_case();
   const std::string key = "k";
   CHECK_EQUAL(put(key, "v", other), kv_status_ok)
   CHECK_EQUAL(::kv_erase(table, key.data(), key.size()), kv_status_ok)
   CHECK_EQUAL(host().row_payers.count(kv_mock::store::row_key{owner, table, key}), 0u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(kv_idx_update_payer_rules)
   begin_case();
   const std::string pri = "k";
   const std::string first = "s";
   const std::string second = "t";
   CHECK_EQUAL(put_entry(pri, first, other), kv_status_ok)
   CHECK_EQUAL(::kv_idx_update(table, 0, pri.data(), pri.size(), first.data(), first.size(), second.data(),
                               second.size()), kv_status_ok)
   CHECK_EQUAL(entry_payer(second, pri), other)
   CHECK_EQUAL(::kv_idx_update(table, owner, pri.data(), pri.size(), second.data(), second.size(), first.data(),
                               first.size()), kv_status_ok)
   CHECK_EQUAL(entry_payer(first, pri), owner)
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(read_only_refuses_all_five_writes);
   SYSIO_TEST(read_only_refuses_a_write_that_would_miss);
   SYSIO_TEST(read_only_allows_reads);
   SYSIO_TEST(empty_keys_abort_where_the_host_checks);
   SYSIO_TEST(idx_find_refuses_a_key_outside_its_prefix);
   SYSIO_TEST(sizes_stop_at_the_limits);
   SYSIO_TEST(raised_limits_apply);
   SYSIO_TEST(prefix_stops_at_its_cap_and_a_seek_does_not);
   SYSIO_TEST(iterator_limit_applies_per_pool);
   SYSIO_TEST(handles_carry_their_pool);
   SYSIO_TEST(fabricated_handles_are_refused);
   SYSIO_TEST(destroyed_handles_are_refused);
   SYSIO_TEST(table_id_above_16_bits_aborts_on_every_call);
   SYSIO_TEST(kv_set_refuses_an_unknown_mode);
   SYSIO_TEST(kv_idx_store_payer_zero_order);
   SYSIO_TEST(kv_set_payer_rules);
   SYSIO_TEST(erase_drops_the_row_payer);
   SYSIO_TEST(kv_idx_update_payer_rules);
   return has_failed();
}
