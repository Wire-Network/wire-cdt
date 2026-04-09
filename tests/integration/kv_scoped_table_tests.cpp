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

BOOST_AUTO_TEST_SUITE(kv_scoped_table_tests)

#define KV_SCOPED_TABLE_TEST(acct, action_name)                                          \
   BOOST_FIXTURE_TEST_CASE(kv_scoped_table_##action_name, TESTER) { try {               \
      produce_blocks(1);                                                                 \
      create_account( acct );                                                            \
      produce_blocks(1);                                                                 \
      set_code( acct, contracts::kv_scoped_table_tests_wasm() );                         \
      set_abi( acct, contracts::kv_scoped_table_tests_abi().data() );                    \
      produce_blocks(1);                                                                 \
      push_action( acct, #action_name ""_n, acct, {} );                                  \
      BOOST_REQUIRE_EQUAL( validate(), true );                                           \
   } FC_LOG_AND_RETHROW() }

KV_SCOPED_TABLE_TEST( "kvscp1"_n, emplace )
KV_SCOPED_TABLE_TEST( "kvscp2"_n, scopeiso )
KV_SCOPED_TABLE_TEST( "kvscp3"_n, secfind )
KV_SCOPED_TABLE_TEST( "kvscp4"_n, seciter )
KV_SCOPED_TABLE_TEST( "kvscp5"_n, secrev )
KV_SCOPED_TABLE_TEST( "kvscp6"_n, modify )
KV_SCOPED_TABLE_TEST( "kvscp7"_n, erasetest )
KV_SCOPED_TABLE_TEST( "kvscp8"_n, keyiter )
KV_SCOPED_TABLE_TEST( "kvscp9"_n, autopk )
KV_SCOPED_TABLE_TEST( "kvscpa"_n, kvcompat )
KV_SCOPED_TABLE_TEST( "kvscpb"_n, scopeiter )
KV_SCOPED_TABLE_TEST( "kvscpc"_n, getscope )
KV_SCOPED_TABLE_TEST( "kvscpd"_n, upsert )
KV_SCOPED_TABLE_TEST( "kvscpe"_n, bounds )
KV_SCOPED_TABLE_TEST( "kvscpf"_n, secbounds )
KV_SCOPED_TABLE_TEST( "kvscpg"_n, secerase )
KV_SCOPED_TABLE_TEST( "kvscph"_n, secmod )
KV_SCOPED_TABLE_TEST( "kvscpi"_n, lambdaempl )
KV_SCOPED_TABLE_TEST( "kvscpj"_n, prirev )
KV_SCOPED_TABLE_TEST( "kvscpk"_n, crossread )
KV_SCOPED_TABLE_TEST( "kvscpl"_n, emptyscope )

BOOST_AUTO_TEST_SUITE_END()
