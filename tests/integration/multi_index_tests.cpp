#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include <boost/test/unit_test.hpp>
#pragma GCC diagnostic pop

#include <sysio/testing/tester.hpp>

#include <contracts.hpp>

using namespace sysio;
using namespace sysio::testing;

#ifdef NON_VALIDATING_TEST
#define TESTER tester
#else
#define TESTER validating_tester
#endif

BOOST_AUTO_TEST_SUITE(multi_index_tests)

// this test is copied from Spring test_api_multi_index
BOOST_FIXTURE_TEST_CASE(main_multi_index_tests, TESTER) { try {
   produce_blocks(1);
   create_account( "testapi"_n );
   produce_blocks(1);
   set_code( "testapi"_n, contracts::test_multi_index_wasm() );
   set_abi( "testapi"_n, contracts::test_multi_index_abi().data() );
   produce_blocks(1);

   auto check_failure = [this]( action_name a, const char* expected_error_msg ) {
      BOOST_CHECK_EXCEPTION(  push_action( "testapi"_n, a, "testapi"_n, {} ),
                              sysio_assert_message_exception,
                              sysio_assert_message_is( expected_error_msg )
      );
   };

   push_action( "testapi"_n, "s1g"_n,  "testapi"_n, {} );        // idx64_general
   push_action( "testapi"_n, "s1namepk"_n, "testapi"_n, {} );    // name_pk_secondaries

   // Secondary keys must iterate in value order, which is a property of the ENCODING --
   // the chain's kv_idx_* intrinsics compare the stored key bytes with memcmp and have no
   // view of the C++ type. The action walks all five supported key types, with rows laid
   // out so that ascending id means descending key, and asserts the sequence, the
   // lower_bound landings, --end(), and that find() still matches. checksum256 is the case
   // with something to lose: it moved from a generic pack() to its own encoder.
   // tests/unit/kv_secondary_key_tests.cpp pins the encoders directly and does run in CI,
   // which this does not -- ENABLE_INTEGRATION_TESTS is off by default.
   push_action( "testapi"_n, "s1secord"_n, "testapi"_n, {} );    // secondary_key_ordering

   // A foreign-code handle cannot mutate: reads honour the handle's code but writes land on
   // the receiver, so without the guard this silently wrote the receiver's own row.
   check_failure( "s1foreign"_n, "cannot create objects in table of another contract" );

   // Duplicate primary key aborts instead of upserting. Without the guard this action
   // succeeded and left the previous secondary mapping stranded.
   check_failure( "s1dupidx"_n, "object with the same primary key already exists" );
   push_action( "testapi"_n, "s1store"_n,  "testapi"_n, {} );    // idx64_store_only
   push_action( "testapi"_n, "s1check"_n,  "testapi"_n, {} );    // idx64_check_without_storing
   push_action( "testapi"_n, "s2g"_n,  "testapi"_n, {} );        // idx128_general
   push_action( "testapi"_n, "s2store"_n,  "testapi"_n, {} );    // idx128_store_only
   push_action( "testapi"_n, "s2check"_n,  "testapi"_n, {} );    // idx128_check_without_storing
   push_action( "testapi"_n, "s2autoinc"_n,  "testapi"_n, {} );  // idx128_autoincrement_test
   push_action( "testapi"_n, "s2autoinc1"_n,  "testapi"_n, {} ); // idx128_autoincrement_test_part1
   push_action( "testapi"_n, "s2autoinc2"_n,  "testapi"_n, {} ); // idx128_autoincrement_test_part2
   check_failure( "s1pkend"_n, "cannot increment end iterator" );
   check_failure( "s1skend"_n, "cannot increment end iterator" );
   check_failure( "s1pkbegin"_n, "cannot decrement iterator at beginning of table" );
   check_failure( "s1skbegin"_n, "cannot decrement iterator at beginning of index" );
   check_failure( "s1pkref"_n, "object passed to iterator_to is not in multi_index" );
   // KV: secondary iterator_to populates cache via find, so cross-table
   // detection is not possible. Dev safety check only, no data impact.
   push_action( "testapi"_n, "s1skref"_n,   "testapi"_n, {} );
   check_failure( "s1pkitrto"_n, "dereferencing invalid iterator" );  // deref end fires before iterator_to
   check_failure( "s1pkmodify"_n, "cannot pass end iterator to modify" );
   check_failure( "s1pkerase"_n, "cannot pass end iterator to erase" );
   check_failure( "s1skitrto"_n, "deref invalid sec iter" );  // deref end fires before iterator_to
   check_failure( "s1skmodify"_n, "cannot pass end iterator to modify" );
   check_failure( "s1skerase"_n, "cannot pass end iterator to erase" );
   check_failure( "s1modpk"_n, "updater cannot change primary key when modifying an object" );
   // KV: autoincrement limit only triggers when max_key exists in storage,
   // not through in-memory cache path. Dev safety check only, no data impact.
   push_action( "testapi"_n, "s1exhaustpk"_n, "testapi"_n, {} );
   check_failure( "s1findfail1"_n, "unable to find key" ); // idx64_require_find_fail
   check_failure( "s1findfail2"_n, "unable to find primary key in require_find" );// idx64_require_find_fail_with_msg
   check_failure( "s1findfail3"_n, "unable to find secondary key" ); // idx64_require_find_sk_fail
   check_failure( "s1findfail4"_n, "unable to find sec key" ); // idx64_require_find_sk_fail_with_msg

   push_action( "testapi"_n, "s1skcache"_n,  "testapi"_n, {} ); // idx64_sk_cache_pk_lookup
   push_action( "testapi"_n, "s1pkcache"_n,  "testapi"_n, {} ); // idx64_pk_cache_sk_lookup

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Second contract — split from main_multi_index_tests to stay under tx net limit
BOOST_FIXTURE_TEST_CASE(multi_index_tests_part2, TESTER) { try {
   produce_blocks(1);
   create_account( "testapi2"_n );
   produce_blocks(1);
   set_code( "testapi2"_n, contracts::test_multi_index2_wasm() );
   set_abi( "testapi2"_n, contracts::test_multi_index2_abi().data() );
   produce_blocks(1);

   push_action( "testapi2"_n, "s3g"_n,  "testapi2"_n, {} );        // idx256_general
   push_action( "testapi2"_n, "sdg"_n,  "testapi2"_n, {} );        // idx_double_general
   push_action( "testapi2"_n, "sldg"_n,  "testapi2"_n, {} );       // idx_long_double_general

   // secondary iterator edge cases
   push_action( "testapi2"_n, "s1clone"_n,    "testapi2"_n, {} ); // sec iterator clone with duplicate keys
   push_action( "testapi2"_n, "s1secrb"_n,    "testapi2"_n, {} ); // sec rbegin/rend (uint64_t)
   push_action( "testapi2"_n, "s2secrb"_n,    "testapi2"_n, {} ); // sec rbegin/rend (uint128_t)
   push_action( "testapi2"_n, "namepk"_n,     "testapi2"_n, {} ); // name-typed primary key
   push_action( "testapi2"_n, "cbegincend"_n, "testapi2"_n, {} ); // cbegin/cend
   push_action( "testapi2"_n, "codescope"_n,  "testapi2"_n, {} ); // get_code/get_scope
   push_action( "testapi2"_n, "crbeginend"_n, "testapi2"_n, {} ); // crbegin/crend
   push_action( "testapi2"_n, "s1secupd"_n,  "testapi2"_n, {} ); // T3: kv_idx_update verification
   push_action( "testapi2"_n, "tpdeser"_n,  "testapi2"_n, {} ); // time_point explicit-ctor deserialize regression

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Cross-scope secondary index isolation tests (separate contract to stay under net limit)
BOOST_FIXTURE_TEST_CASE(cross_scope_secondary_index_tests, TESTER) { try {
   produce_blocks(1);
   create_account( "scopetest"_n );
   produce_blocks(1);
   set_code( "scopetest"_n, contracts::mi_scope_tests_wasm() );
   set_abi( "scopetest"_n, contracts::mi_scope_tests_abi().data() );
   produce_blocks(1);

   push_action( "scopetest"_n, "xscope"_n,      "scopetest"_n, {} ); // iteration isolated per scope
   push_action( "scopetest"_n, "xscopefind"_n,  "scopetest"_n, {} ); // find() respects scope
   push_action( "scopetest"_n, "xscopeerase"_n, "scopetest"_n, {} ); // erase in A doesn't affect B
   push_action( "scopetest"_n, "xscopeub"_n,    "scopetest"_n, {} ); // upper_bound stops at scope
   push_action( "scopetest"_n, "xscoperev"_n,   "scopetest"_n, {} ); // reverse iteration within scope

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
