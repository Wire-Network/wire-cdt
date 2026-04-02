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

BOOST_AUTO_TEST_SUITE(kv_raw_table_tests)

// Helper: deploy kv_raw_table_tests contract to a fresh account and run one action
#define KV_RAW_TABLE_TEST(acct, action_name)                                         \
   BOOST_FIXTURE_TEST_CASE(kv_raw_table_##action_name, TESTER) { try {              \
      produce_blocks(1);                                                       \
      create_account( acct );                                                  \
      produce_blocks(1);                                                       \
      set_code( acct, contracts::kv_raw_table_tests_wasm() );                        \
      set_abi( acct, contracts::kv_raw_table_tests_abi().data() );                   \
      produce_blocks(1);                                                       \
      push_action( acct, #action_name ""_n, acct, {} );                        \
      BOOST_REQUIRE_EQUAL( validate(), true );                                 \
   } FC_LOG_AND_RETHROW() }

KV_RAW_TABLE_TEST( "kvmap1"_n, setget )
KV_RAW_TABLE_TEST( "kvmap2"_n, erasetest )
KV_RAW_TABLE_TEST( "kvmap3"_n, uintorder )
KV_RAW_TABLE_TEST( "kvmap4"_n, signedorder )
KV_RAW_TABLE_TEST( "kvmap5"_n, signed32 )
KV_RAW_TABLE_TEST( "kvmapa"_n, signedsm )
KV_RAW_TABLE_TEST( "kvmapb"_n, strorder )
KV_RAW_TABLE_TEST( "kvmapc"_n, bounds )
KV_RAW_TABLE_TEST( "kvmapd"_n, emptyiter )
KV_RAW_TABLE_TEST( "kvmape"_n, overwrite )
KV_RAW_TABLE_TEST( "kvmapf"_n, reviter )
KV_RAW_TABLE_TEST( "kvmapg"_n, signededge )
KV_RAW_TABLE_TEST( "kvmaph"_n, floatorder )
KV_RAW_TABLE_TEST( "kvmapi"_n, dblorder )
KV_RAW_TABLE_TEST( "kvmapj"_n, signedi )
KV_RAW_TABLE_TEST( "kvmapk"_n, strnul )
KV_RAW_TABLE_TEST( "kvmapl"_n, blobkey )
KV_RAW_TABLE_TEST( "kvmapn"_n, crossread )
KV_RAW_TABLE_TEST( "kvmapo"_n, zeroval )
KV_RAW_TABLE_TEST( "kvmapp"_n, ramdelta )
KV_RAW_TABLE_TEST( "kvmapq"_n, setpayer )

// Negative test: erase non-existent key should assert (T1/T7)
BOOST_FIXTURE_TEST_CASE(kv_raw_table_erasebad, TESTER) { try {
   produce_blocks(1);
   create_account( "kvmapm"_n );
   produce_blocks(1);
   set_code( "kvmapm"_n, contracts::kv_raw_table_tests_wasm() );
   set_abi( "kvmapm"_n, contracts::kv_raw_table_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvmapm"_n, "erasebad"_n, "kvmapm"_n, {} ),
      kv_key_not_found,
      fc_exception_message_is("KV key not found for erase")
   );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
