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

BOOST_AUTO_TEST_SUITE(kv_indexed_table_tests)

// Helper: deploy kv_indexed_table_tests contract to a fresh account and run one action
#define KV_INDEXED_TABLE_TEST(acct, action_name)                                        \
   BOOST_FIXTURE_TEST_CASE(kv_indexed_table_##action_name, TESTER) { try {             \
      produce_blocks(1);                                                       \
      create_account( acct );                                                  \
      produce_blocks(1);                                                       \
      set_code( acct, contracts::kv_indexed_table_tests_wasm() );                       \
      set_abi( acct, contracts::kv_indexed_table_tests_abi().data() );                  \
      produce_blocks(1);                                                       \
      push_action( acct, #action_name ""_n, acct, {} );                        \
      BOOST_REQUIRE_EQUAL( validate(), true );                                 \
   } FC_LOG_AND_RETHROW() }

KV_INDEXED_TABLE_TEST( "kvidx1"_n, emplace )
KV_INDEXED_TABLE_TEST( "kvidx2"_n, emplpayer )
KV_INDEXED_TABLE_TEST( "kvidx3"_n, modify )
KV_INDEXED_TABLE_TEST( "kvidx4"_n, modpayer )
KV_INDEXED_TABLE_TEST( "kvidx5"_n, erasetest )
KV_INDEXED_TABLE_TEST( "kvidxa"_n, secfind )
KV_INDEXED_TABLE_TEST( "kvidxb"_n, seclbound )
KV_INDEXED_TABLE_TEST( "kvidxc"_n, seciter )
KV_INDEXED_TABLE_TEST( "kvidxd"_n, secmodify )
KV_INDEXED_TABLE_TEST( "kvidxe"_n, secerase )
KV_INDEXED_TABLE_TEST( "kvidxf"_n, keyiter )
KV_INDEXED_TABLE_TEST( "kvidxg"_n, reqfind )
KV_INDEXED_TABLE_TEST( "kvidxh"_n, dupkeys )
KV_INDEXED_TABLE_TEST( "kvidxi"_n, priiter )
KV_INDEXED_TABLE_TEST( "kvidxj"_n, gettest )
KV_INDEXED_TABLE_TEST( "kvidxk"_n, overwrite )
KV_INDEXED_TABLE_TEST( "kvidxw"_n, upsert )

// be_key_reader type coverage
KV_INDEXED_TABLE_TEST( "kvidxl"_n, signedkey )
KV_INDEXED_TABLE_TEST( "kvidxm"_n, strkey )
KV_INDEXED_TABLE_TEST( "kvidxn"_n, dblkey )
KV_INDEXED_TABLE_TEST( "kvidxo"_n, multikey )

// Edge cases
KV_INDEXED_TABLE_TEST( "kvidxp"_n, emptyiter )
KV_INDEXED_TABLE_TEST( "kvidxq"_n, reviter )
KV_INDEXED_TABLE_TEST( "kvidxr"_n, secrev )
KV_INDEXED_TABLE_TEST( "kvidxs"_n, ramdelta )
KV_INDEXED_TABLE_TEST( "kvidxx"_n, secubound )
KV_INDEXED_TABLE_TEST( "kvidxy"_n, zerocopy )
KV_INDEXED_TABLE_TEST( "kvidxz"_n, tpdeser )

// Negative tests: actions that should assert
// NOTE: erasend and modifyend are NOT dispatched (32-action limit); the methods
// still exist in the contract class but can only be tested if deployed as a
// separate contract with their own dispatch. Skipped here.

BOOST_FIXTURE_TEST_CASE(kv_indexed_table_reqmiss, TESTER) { try {
   produce_blocks(1);
   create_account( "kvidxv"_n );
   produce_blocks(1);
   set_code( "kvidxv"_n, contracts::kv_indexed_table_tests_wasm() );
   set_abi( "kvidxv"_n, contracts::kv_indexed_table_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvidxv"_n, "reqmiss"_n, "kvidxv"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is("expected to miss")
   );
} FC_LOG_AND_RETHROW() }

// New API tests (consolidated into single action to stay under 32-action limit)
KV_INDEXED_TABLE_TEST( "kvnewapi"_n, newapi )

// Negative test: emplace on duplicate key should assert
BOOST_FIXTURE_TEST_CASE(kv_indexed_table_dupempl, TESTER) { try {
   produce_blocks(1);
   create_account( "kvdupempl"_n );
   produce_blocks(1);
   set_code( "kvdupempl"_n, contracts::kv_indexed_table_tests_wasm() );
   set_abi( "kvdupempl"_n, contracts::kv_indexed_table_tests_abi().data() );
   produce_blocks(1);
   BOOST_CHECK_EXCEPTION(
      push_action( "kvdupempl"_n, "dupempl"_n, "kvdupempl"_n, {} ),
      sysio_assert_message_exception,
      sysio_assert_message_is("emplace: key already exists (use upsert for insert-or-update)")
   );
} FC_LOG_AND_RETHROW() }

BOOST_AUTO_TEST_SUITE_END()
