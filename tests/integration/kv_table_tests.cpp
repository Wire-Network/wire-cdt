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

BOOST_AUTO_TEST_SUITE(kv_table_tests)

BOOST_FIXTURE_TEST_CASE(kv_table_integration, TESTER) { try {
   produce_blocks(1);
   create_account( "kvtest"_n );
   produce_blocks(1);
   set_code( "kvtest"_n, contracts::kv_table_tests_wasm() );
   set_abi( "kvtest"_n, contracts::kv_table_tests_abi().data() );
   produce_blocks(1);

   push_action( "kvtest"_n, "emplacefind"_n, "kvtest"_n, {} );
   push_action( "kvtest"_n, "modify"_n,      "kvtest"_n, {} );
   push_action( "kvtest"_n, "erase"_n,       "kvtest"_n, {} );
   push_action( "kvtest"_n, "iterate"_n,     "kvtest"_n, {} );
   push_action( "kvtest"_n, "lowerupper"_n,  "kvtest"_n, {} );
   push_action( "kvtest"_n, "getbyval"_n,    "kvtest"_n, {} );
   push_action( "kvtest"_n, "reqfind"_n,     "kvtest"_n, {} );
   push_action( "kvtest"_n, "availpk"_n,     "kvtest"_n, {} );
   // crossscope moved to its own test case with fresh account (below)
   push_action( "kvtest"_n, "emptyiter"_n,   "kvtest"_n, {} );
   push_action( "kvtest"_n, "constiter"_n,  "kvtest"_n, {} );
   push_action( "kvtest"_n, "erasebypk"_n,  "kvtest"_n, {} );
   push_action( "kvtest"_n, "setmethod"_n,   "kvtest"_n, {} );
   push_action( "kvtest"_n, "modifyobj"_n,  "kvtest"_n, {} );
   push_action( "kvtest"_n, "endallscope"_n, "kvtest"_n, {} );
   push_action( "kvtest"_n, "podcrud"_n,    "kvtest"_n, {} );  // trivially-copyable fast path

   BOOST_CHECK_EXCEPTION(
      push_action( "kvtest"_n, "reqfindfail"_n, "kvtest"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is( "expected failure" )
   );

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Cross-scope iteration on a fresh account (exact count check)
BOOST_FIXTURE_TEST_CASE(kv_table_crossscope, TESTER) { try {
   produce_blocks(1);
   create_account( "kvscope"_n );
   produce_blocks(1);
   set_code( "kvscope"_n, contracts::kv_table_tests_wasm() );
   set_abi( "kvscope"_n, contracts::kv_table_tests_abi().data() );
   produce_blocks(1);
   push_action( "kvscope"_n, "crossscope"_n, "kvscope"_n, {} );
   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Negative: erase non-existent pk (T6)
BOOST_FIXTURE_TEST_CASE(kv_table_erasebadpk, TESTER) { try {
   produce_blocks(1);
   create_account( "kvtest2"_n );
   produce_blocks(1);
   set_code( "kvtest2"_n, contracts::kv_table_tests_wasm() );
   set_abi( "kvtest2"_n, contracts::kv_table_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvtest2"_n, "erasebadpk"_n, "kvtest2"_n, {} ),
      kv_key_not_found,
      fc_exception_message_is( "KV key not found for erase" )
   );
} FC_LOG_AND_RETHROW() }

// Negative: modify that changes primary key (T6)
BOOST_FIXTURE_TEST_CASE(kv_table_modifypk, TESTER) { try {
   produce_blocks(1);
   create_account( "kvtest3"_n );
   produce_blocks(1);
   set_code( "kvtest3"_n, contracts::kv_table_tests_wasm() );
   set_abi( "kvtest3"_n, contracts::kv_table_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvtest3"_n, "modifypk"_n, "kvtest3"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is( "cannot modify primary key" )
   );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
