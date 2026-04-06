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

BOOST_AUTO_TEST_SUITE(kv_singleton_tests)

BOOST_FIXTURE_TEST_CASE(kv_singleton_integration, TESTER) { try {
   produce_blocks(1);
   create_account( "kvsngl"_n );
   produce_blocks(1);
   set_code( "kvsngl"_n, contracts::kv_singleton_tests_wasm() );
   set_abi( "kvsngl"_n, contracts::kv_singleton_tests_abi().data() );
   produce_blocks(1);

   push_action( "kvsngl"_n, "setget"_n,      "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "getdefault"_n,  "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "getorcreate"_n, "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "removetest"_n,  "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "settwice"_n,    "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "scopetest"_n,   "kvsngl"_n, {} );
   push_action( "kvsngl"_n, "podsingleton"_n,"kvsngl"_n, {} );  // trivially-copyable fast path

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Negative test: get() on unset singleton should assert (T2)
BOOST_FIXTURE_TEST_CASE(kv_singleton_getunset, TESTER) { try {
   produce_blocks(1);
   create_account( "kvsngl2"_n );
   produce_blocks(1);
   set_code( "kvsngl2"_n, contracts::kv_singleton_tests_wasm() );
   set_abi( "kvsngl2"_n, contracts::kv_singleton_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvsngl2"_n, "getunset"_n, "kvsngl2"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is("singleton does not exist")
   );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
