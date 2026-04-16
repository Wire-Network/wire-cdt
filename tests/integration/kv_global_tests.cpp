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

BOOST_AUTO_TEST_SUITE(kv_global_tests)

BOOST_FIXTURE_TEST_CASE(kv_global_integration, TESTER) { try {
   produce_blocks(1);
   create_account( "kvglob"_n );
   produce_blocks(1);
   set_code( "kvglob"_n, contracts::kv_global_tests_wasm() );
   set_abi( "kvglob"_n, contracts::kv_global_tests_abi().data() );
   produce_blocks(1);

   // POD (trivially-copyable) path
   push_action( "kvglob"_n, "podsetget"_n,   "kvglob"_n, {} );
   push_action( "kvglob"_n, "getdefault"_n,  "kvglob"_n, {} );
   push_action( "kvglob"_n, "getorcreate"_n, "kvglob"_n, {} );
   push_action( "kvglob"_n, "twonames"_n,    "kvglob"_n, {} );

   // String (non-trivially-copyable) path
   push_action( "kvglob"_n, "strsetget"_n,   "kvglob"_n, {} );
   push_action( "kvglob"_n, "strdefault"_n,  "kvglob"_n, {} );

   BOOST_REQUIRE_EQUAL( validate(), true );
} FC_LOG_AND_RETHROW() }

// Negative: get on unset should assert
BOOST_FIXTURE_TEST_CASE(kv_global_getunset, TESTER) { try {
   produce_blocks(1);
   create_account( "kvglob2"_n );
   produce_blocks(1);
   set_code( "kvglob2"_n, contracts::kv_global_tests_wasm() );
   set_abi( "kvglob2"_n, contracts::kv_global_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvglob2"_n, "getunset"_n, "kvglob2"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is("global does not exist")
   );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
