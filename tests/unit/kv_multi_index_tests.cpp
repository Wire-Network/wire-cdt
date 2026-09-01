/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Native coverage for sysio::multi_index's mutation guards.
 *
 *  These assertions exist natively, rather than only in tests/integration, because
 *  ENABLE_INTEGRATION_TESTS defaults OFF and the CI workflow does not enable it -- an
 *  integration-only regression leaves required CI green when the guard is deleted. The
 *  equivalent on-chain cases live in tests/integration/multi_index_tests.cpp and remain the
 *  real-runtime coverage.
 *
 *  Two guards are under test, both restored to match upstream multi_index:
 *
 *    1. A duplicate primary key aborts. kv_set is an upsert, so without the check the row is
 *       silently overwritten and store_secondaries strands the previous mapping.
 *    2. Mutating through a handle opened on another account aborts. Reads take a `code`
 *       argument and honour it; kv_set, kv_erase and kv_idx_store have none and always land
 *       on the receiver. table_id derives from the table NAME alone, so a foreign-code
 *       mutation probes their table and writes the receiver's row of the same name.
 *
 *  The cases below never reach a write: each aborts first, so the mocked store is seeded
 *  directly rather than through emplace, and no iterator or secondary-index intrinsics are
 *  needed.
 */

#include <sysio/tester.hpp>
#include <sysio/multi_index.hpp>
#include <sysio/kv_constants.hpp>

#include <map>
#include <string>
#include <tuple>

using namespace sysio;
using namespace sysio::native;

// The generated dispatcher records the receiver here at the top of apply(); the native
// dispatch does not, which is what the fallback in receiving_account() is for. Driving it
// directly lets both branches be exercised.
extern "C" void sysio_set_contract_name(uint64_t n);

namespace {

struct record {
   uint64_t id;
   uint64_t sec;

   uint64_t primary_key() const { return id; }
   uint64_t get_secondary() const { return sec; }

   SYSLIB_SERIALIZE(record, (id)(sec))
};

using table_t = sysio::multi_index<"records"_n, record>;

constexpr uint32_t records_tid = sysio::kv::compute_table_id("records"_n.value);

// Mirrors the asymmetry under test: kv_contains honours `code`, writes have no such
// parameter. Only the read side is needed -- every case here aborts before writing.
struct mock_kv {
   using row_key = std::tuple<uint64_t, uint32_t, std::string>;   // code, table_id, key
   std::map<row_key, std::string> rows;
   uint64_t receiver = 0;
   uint32_t sets = 0;                 // must stay 0: a rejected mutation writes nothing

   void reset(uint64_t who) { rows.clear(); receiver = who; sets = 0; }
};

mock_kv& store() { static mock_kv inst; return inst; }

/// The 16-byte primary key multi_index builds: [scope:8B BE][pk:8B BE].
std::string pk_key(uint64_t scope, uint64_t pk) {
   std::string k(16, '\0');
   for (int i = 7; i >= 0; --i) { k[i]      = char(scope & 0xFF); scope >>= 8; }
   for (int i = 7; i >= 0; --i) { k[8 + i]  = char(pk    & 0xFF); pk    >>= 8; }
   return k;
}

void install_intrinsics() {
   intrinsics::set_intrinsic<intrinsics::current_receiver>(
      []() -> capi_name { return store().receiver; });

   intrinsics::set_intrinsic<intrinsics::kv_contains>(
      [](uint32_t table_id, capi_name code, const void* key, uint32_t key_size) -> int32_t {
         auto k = std::string(static_cast<const char*>(key), key_size);
         return store().rows.count(mock_kv::row_key{code, table_id, k}) ? 1 : 0;
      });

   // No code parameter: a write always lands on the receiver. Counted, never expected.
   intrinsics::set_intrinsic<intrinsics::kv_set>(
      [](uint32_t, uint64_t, const void*, uint32_t, const void*, uint32_t) -> int64_t {
         ++store().sets;
         return 0;
      });
}

/// Seed the receiver's own table with pk, and install the mocks.
/// @param dispatcher_sets_name mirrors the generated dispatcher recording the receiver in
///        the sysio_contract_name global; false leaves it 0, as SYSIO_DISPATCH and the
///        native dispatch do, exercising the current_receiver() fallback instead.
void arrange(uint64_t receiver, uint64_t scope, uint64_t pk, bool dispatcher_sets_name) {
   store().reset(receiver);
   store().rows[mock_kv::row_key{receiver, records_tid, pk_key(scope, pk)}] = "row";
   install_intrinsics();
   sysio_set_contract_name(dispatcher_sets_name ? receiver : 0);
}

} // namespace

// A duplicate primary key must abort rather than upsert.
SYSIO_TEST_BEGIN(duplicate_primary_key_rejected)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t t("alice"_n, "alice"_n.value);

      CHECK_ASSERT( "object with the same primary key already exists",
                    ([&]() { t.emplace("alice"_n, [](auto& r) { r.id = 1; r.sec = 7; }); }) )
      CHECK_EQUAL( store().sets, 0u )
   }
SYSIO_TEST_END

// Mutating through a foreign-code handle must abort. The receiver holds pk=1 and the foreign
// account does not, so the duplicate probe alone would pass -- this is precisely the case
// where the old code upserted the receiver's row.
SYSIO_TEST_BEGIN(foreign_code_handle_cannot_mutate)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t foreign("bob"_n, "alice"_n.value);
      record r{1, 7};

      CHECK_ASSERT( "cannot create objects in table of another contract",
                    ([&]() { foreign.emplace("alice"_n, [](auto& o) { o.id = 1; o.sec = 7; }); }) )
      CHECK_ASSERT( "cannot modify objects in table of another contract",
                    ([&]() { foreign.modify(r, "alice"_n, [](auto& o) { o.sec = 9; }); }) )
      CHECK_ASSERT( "cannot erase objects in table of another contract",
                    ([&]() { foreign.erase(r); }) )

      // The whole point: the receiver's row was never touched.
      CHECK_EQUAL( store().sets, 0u )
      CHECK_EQUAL( store().rows.count(mock_kv::row_key{"alice"_n.value, records_tid,
                                                       pk_key("alice"_n.value, 1)}), 1u )
   }
SYSIO_TEST_END

// A handle on the receiver's own table is unaffected by the guard.
SYSIO_TEST_BEGIN(own_table_handle_passes_the_guard)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t t("alice"_n, "alice"_n.value);

      // pk=2 is absent, so the guard and the duplicate probe both pass and the message,
      // if any, is not one of the three rejections.
      CHECK_ASSERT( "object with the same primary key already exists",
                    ([&]() { t.emplace("alice"_n, [](auto& o) { o.id = 1; o.sec = 7; }); }) )
   }
SYSIO_TEST_END

// The primary bounds stay callable as concrete overloads. A member template would break both
// of these: a braced list cannot be deduced, and a pointer cannot be formed to an undeduced
// template. Compile-time only -- neither expression is evaluated.
SYSIO_TEST_BEGIN(primary_bounds_accept_every_call_shape)
   using itr_t = table_t::const_iterator;

   // Bare address-taking, with NO cast. This is the case a cast would hide: an explicit
   // static_cast selects from an overload set and so passes even when the bare form does
   // not compile, which is exactly how the earlier two-overload revision looked correct.
   constexpr auto lb = &table_t::lower_bound;
   constexpr auto ub = &table_t::upper_bound;
   static_assert(lb != nullptr && ub != nullptr, "primary bounds must be bare-addressable");

   using by_u64   = decltype(std::declval<const table_t&>().lower_bound(uint64_t{42}));
   using braced   = decltype(std::declval<const table_t&>().lower_bound({42}));
   using by_name  = decltype(std::declval<const table_t&>().lower_bound("alice"_n));
   static_assert(std::is_same_v<by_u64,  itr_t>, "lower_bound must accept a uint64_t");
   static_assert(std::is_same_v<braced,  itr_t>, "lower_bound must accept a braced initializer");
   static_assert(std::is_same_v<by_name, itr_t>, "lower_bound must accept a name");
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   SYSIO_TEST(duplicate_primary_key_rejected)
   SYSIO_TEST(foreign_code_handle_cannot_mutate)
   SYSIO_TEST(own_table_handle_passes_the_guard)
   SYSIO_TEST(primary_bounds_accept_every_call_shape)
   return has_failed();
}
