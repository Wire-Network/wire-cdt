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

BOOST_AUTO_TEST_SUITE(hash_id_tests)

#define HASH_ID_TEST(ACCT, ACTION) \
BOOST_FIXTURE_TEST_CASE(hash_id_##ACTION, TESTER) { try { \
   produce_blocks(1); \
   create_account( ACCT ); \
   produce_blocks(1); \
   set_code( ACCT, contracts::hash_id_tests_wasm() ); \
   set_abi( ACCT, contracts::hash_id_tests_abi().data() ); \
   produce_blocks(1); \
   push_action( ACCT, #ACTION##_n, ACCT, {} ); \
} FC_LOG_AND_RETHROW() }

HASH_ID_TEST( "hashbasic"_n,  hashbasic )
HASH_ID_TEST( "hashlongn"_n,  longname )
HASH_ID_TEST( "hashglob"_n,   iglobal )
HASH_ID_TEST( "hashisol"_n,   isolation )

BOOST_AUTO_TEST_SUITE_END()
