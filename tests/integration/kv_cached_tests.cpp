/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  End-to-end coverage for kv::cached_global and sysio::cached_kv_singleton against a real
 *  chain, driving the kv_cached_tests contract.
 *
 *  This is the test that pins the actual defect. A contract that caches singleton state in a
 *  member and writes it back unconditionally makes every action issue a kv_set, because the
 *  generated dispatcher destroys the contract instance as soon as the action returns. Inside
 *  a read-only transaction the chain rejects that write with
 *
 *     cannot store a KV record when executing a readonly transaction  (table_operation_not_permitted)
 *
 *  and the failure only shows up on the SUCCESS path -- an action that aborts early via
 *  check() traps before the destructor runs and reports its own error instead. The cached
 *  types only write when an action actually mutated something, so a query action is legal
 *  read-only while a mutating one is still correctly refused.
 */

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <sysio/testing/tester.hpp>

#include <contracts.hpp>

using namespace sysio;
using namespace sysio::chain;
using namespace sysio::testing;

#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif

namespace {

constexpr auto cached_acct = "kvcach"_n;

/// Deploy the kv_cached_tests contract.
void deploy(TESTER& t) {
   t.produce_blocks(1);
   t.create_account(cached_acct);
   t.produce_blocks(1);
   t.set_code(cached_acct, contracts::kv_cached_contract_wasm());
   t.set_abi(cached_acct, contracts::kv_cached_contract_abi().data());
   t.produce_blocks(1);
}

/// Push \p act as a read-only transaction. Read-only transactions carry no authorizations,
/// which is why the actions under test do not call require_auth.
transaction_trace_ptr push_readonly(TESTER& t, action_name act) {
   signed_transaction trx;
   trx.actions.push_back(t.get_action(cached_acct, act, {}, {}));
   t.set_transaction_headers(trx);
   return t.push_transaction(trx, fc::time_point::maximum(), TESTER::DEFAULT_BILLED_CPU_TIME_US,
                             false, transaction_metadata::trx_type::read_only);
}

} // namespace

BOOST_AUTO_TEST_SUITE(kv_cached_tests)

/// A query action on a contract with member-cached singletons must succeed read-only.
BOOST_FIXTURE_TEST_CASE(kv_cached_readonly_query, TESTER) { try {
   deploy(*this);

   // Reads that MISS are still reads: no row exists yet, and no write may be attempted.
   push_readonly(*this, "roabsent"_n);

   push_action(cached_acct, "seed"_n, cached_acct, {});
   produce_blocks(1);

   // The regression itself: pure reads, contract instance destroyed on return, no kv_set.
   push_readonly(*this, "roread"_n);

   // Reading read-only must not have mutated anything -- a normal push still sees the seed.
   push_action(cached_acct, "roread"_n, cached_acct, {});

   BOOST_REQUIRE_EQUAL(validate(), true);
} FC_LOG_AND_RETHROW() }

/// A mutating action must still be refused inside a read-only transaction. Without this the
/// suite above could pass simply because writes stopped being enforced.
BOOST_FIXTURE_TEST_CASE(kv_cached_readonly_rejects_mutation, TESTER) { try {
   deploy(*this);
   push_action(cached_acct, "seed"_n, cached_acct, {});
   produce_blocks(1);

   BOOST_REQUIRE_THROW(push_readonly(*this, "bump"_n), table_operation_not_permitted);

   // The refused transaction must leave nothing behind.
   push_action(cached_acct, "roread"_n, cached_acct, {});

   BOOST_REQUIRE_EQUAL(validate(), true);
} FC_LOG_AND_RETHROW() }

/// Deferred writes actually persist, and repeated mutations inside one action collapse into a
/// single stored result.
BOOST_FIXTURE_TEST_CASE(kv_cached_deferred_writes_persist, TESTER) { try {
   deploy(*this);

   push_action(cached_acct, "seed"_n, cached_acct, {});
   produce_blocks(1);

   push_action(cached_acct, "bump"_n, cached_acct, {});
   produce_blocks(1);
   push_action(cached_acct, "chkbump"_n, cached_acct, {});

   // Five in-action mutations, one stored outcome: 43 + 5*10.
   push_action(cached_acct, "multibump"_n, cached_acct, {});
   produce_blocks(1);
   push_action(cached_acct, "chkmulti"_n, cached_acct, {});

   push_action(cached_acct, "rmall"_n, cached_acct, {});
   produce_blocks(1);

   // Erased rows stay erased across transactions, and reading them remains read-only legal.
   push_readonly(*this, "roabsent"_n);

   BOOST_REQUIRE_EQUAL(validate(), true);
} FC_LOG_AND_RETHROW() }

/// upsert() creates a row that was never seeded.
BOOST_FIXTURE_TEST_CASE(kv_cached_upsert_creates, TESTER) { try {
   deploy(*this);

   push_action(cached_acct, "upsertnew"_n, cached_acct, {});
   produce_blocks(1);
   push_action(cached_acct, "chkupsert"_n, cached_acct, {});

   BOOST_REQUIRE_EQUAL(validate(), true);
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
