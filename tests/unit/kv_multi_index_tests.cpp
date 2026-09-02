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
 *  The cases come in two kinds. The REJECTING ones abort before any write, so their store is
 *  seeded directly rather than through emplace. The ALLOWING ones -- an owned handle doing
 *  emplace, modify and erase -- run the mutation through, so the mock also serves kv_set,
 *  kv_erase and the iterator reads that emplace's closing find() performs. Both kinds matter:
 *  without the allowing ones, a guard that rejected everything would satisfy the suite.
 *
 *  No secondary-index intrinsic is needed: the table under test declares no indices, so
 *  store/remove/update_secondaries fold to nothing.
 */

#include <sysio/tester.hpp>
#include <sysio/multi_index.hpp>
#include <sysio/kv_constants.hpp>

#include <map>
#include <type_traits>
#include <utility>
#include <cstring>
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

/// A caller's key wrapper. find/get/require_find take uint64_t and so accept one of these
/// through a single user-defined conversion; the bounds must not be narrower than they are.
struct wrapped_key {
   uint64_t v;
   constexpr operator uint64_t() const { return v; }   // NOLINT(google-explicit-constructor)
};

/// Convertible to BOTH parameter types. Against a single uint64_t parameter this selected the
/// uint64_t conversion; against the overload pair it is ambiguous. Pinned so the documented
/// cost stays a documented cost rather than being rediscovered as a surprise.
struct dual_key {
   constexpr operator uint64_t() const { return 3; }   // NOLINT(google-explicit-constructor)
   operator name() const { return "alice"_n; }         // NOLINT(google-explicit-constructor)
};

/// Is `t.lower_bound(A)` well-formed?
template<typename A, typename = void>
struct callable_with : std::false_type {};
template<typename A>
struct callable_with<A, std::void_t<decltype(std::declval<const table_t&>().lower_bound(std::declval<A>()))>>
   : std::true_type {};

constexpr uint32_t records_tid = sysio::kv::compute_table_id("records"_n.value);

// Mirrors the asymmetry under test: kv_contains and kv_get honour `code`, while kv_set and
// kv_erase have no such parameter and always land on store().receiver. Rows are therefore
// keyed by the account that holds them, which is how a misdirected write is made visible.
struct mock_kv {
   using row_key = std::tuple<uint64_t, uint32_t, std::string>;   // code, table_id, key
   std::map<row_key, std::string> rows;
   uint64_t receiver = 0;
   uint32_t sets = 0;                 // 0 unless a case expects the write to be allowed
   std::string it_key;                // key the one live iterator was positioned at
   uint32_t erases = 0;

   void reset(uint64_t who) { rows.clear(); receiver = who; sets = 0; erases = 0; it_key.clear(); }
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

   // No code parameter: a write always lands on the receiver, never on the handle's code.
   // Recorded under store().receiver so a misdirected write is observable as a row under the
   // wrong account, not merely as a count.
   intrinsics::set_intrinsic<intrinsics::kv_set>(
      [](uint32_t table_id, uint64_t, const void* key, uint32_t key_size,
         const void* val, uint32_t val_size) -> int64_t {
         ++store().sets;
         store().rows[mock_kv::row_key{store().receiver, table_id,
                                       std::string(static_cast<const char*>(key), key_size)}] =
            std::string(static_cast<const char*>(val), val_size);
         return 0;
      });

   // No code parameter here either: an erase always lands on the receiver.
   intrinsics::set_intrinsic<intrinsics::kv_erase>(
      [](uint32_t table_id, const void* key, uint32_t key_size) -> int64_t {
         ++store().erases;
         store().rows.erase(mock_kv::row_key{store().receiver, table_id,
                                             std::string(static_cast<const char*>(key), key_size)});
         return 0;
      });

   // emplace() returns find(pk), so a write that is allowed through walks the iterator path.
   // Exactly one iterator is ever live in these cases, so remembering the key it was
   // positioned at is enough to serve a real key/value pair rather than a stub -- the
   // returned iterator is genuinely valid, not merely non-crashing.
   intrinsics::set_intrinsic<intrinsics::kv_it_create>(
      [](uint32_t, capi_name, const void*, uint32_t) -> uint32_t { return 1; });
   intrinsics::set_intrinsic<intrinsics::kv_it_destroy>([](uint32_t) {});
   intrinsics::set_intrinsic<intrinsics::kv_it_status>([](uint32_t) -> int32_t { return 0; });
   intrinsics::set_intrinsic<intrinsics::kv_it_lower_bound>(
      [](uint32_t, const void* key, uint32_t key_size) -> int32_t {
         store().it_key.assign(static_cast<const char*>(key), key_size);
         return 0;
      });

   // Serve from the store, so a key or value the contract never wrote cannot be read back.
   auto serve = [](const std::string& src, uint32_t offset, void* dest, uint32_t dest_size,
                   uint32_t* actual_size) -> int32_t {
      if (offset > src.size()) return -1;
      *actual_size = static_cast<uint32_t>(src.size() - offset);
      const uint32_t n = *actual_size < dest_size ? *actual_size : dest_size;
      std::memcpy(dest, src.data() + offset, n);
      return 0;
   };
   intrinsics::set_intrinsic<intrinsics::kv_it_key>(
      [serve](uint32_t, uint32_t off, void* d, uint32_t ds, uint32_t* as) -> int32_t {
         return serve(store().it_key, off, d, ds, as);
      });
   // Reached: emplace's closing find() constructs an iterator, whose load_current() reads the
   // key and then the value. Serving from the store rather than stubbing means a row that
   // landed under the wrong key cannot be read back as if it were correct.
   intrinsics::set_intrinsic<intrinsics::kv_it_value>(
      [serve](uint32_t, uint32_t off, void* d, uint32_t ds, uint32_t* as) -> int32_t {
         auto it = store().rows.find(mock_kv::row_key{store().receiver, records_tid,
                                                      store().it_key});
         if (it == store().rows.end()) return -1;
         return serve(it->second, off, d, ds, as);
      });
}

/// The account whose table is under test is never this one. When the dispatcher-global path
/// is being exercised, the current_receiver intrinsic is pointed here instead, so the two
/// branches of receiving_account() cannot return the same answer.
constexpr uint64_t decoy_receiver = "carol"_n.value;

/// Seed `owner`'s table with pk, and install the mocks.
///
/// @param dispatcher_sets_name mirrors the generated dispatcher recording the receiver in
///        the sysio_contract_name global. When true, the mocked current_receiver
///        deliberately returns decoy_receiver rather than `owner`: a guard that consulted
///        the intrinsic instead of the global would then get the wrong account and the case
///        would fail. Pointing both at `owner` -- as this did originally -- makes the two
///        branches indistinguishable, and deleting the global fast path leaves the suite
///        green. When false the global is 0, as SYSIO_DISPATCH and the native dispatch leave
///        it, and the intrinsic is the only source.
void arrange(uint64_t owner, uint64_t scope, uint64_t pk, bool dispatcher_sets_name) {
   store().reset(dispatcher_sets_name ? decoy_receiver : owner);
   store().rows[mock_kv::row_key{owner, records_tid, pk_key(scope, pk)}] = "row";
   install_intrinsics();
   sysio_set_contract_name(dispatcher_sets_name ? owner : 0);
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

      // The whole point: the receiver's row was never touched. The VALUE comparison is what
      // carries that -- a misdirected emplace overwrites the row under the same key, so the
      // count() below would still be 1 and proves nothing on its own here, where nothing
      // erases. It is kept as a precondition for the .at().
      CHECK_EQUAL( store().sets, 0u )
      const auto seeded = mock_kv::row_key{"alice"_n.value, records_tid,
                                           pk_key("alice"_n.value, 1)};
      CHECK_EQUAL( store().rows.count(seeded), 1u )
      CHECK_EQUAL( store().rows.at(seeded), std::string("row") )
   }
SYSIO_TEST_END

// A handle on the receiver's own table is unaffected by the guard.
SYSIO_TEST_BEGIN(own_table_handle_passes_the_guard)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t t("alice"_n, "alice"_n.value);

      // pk=2 is absent, so neither the receiver guard nor the duplicate probe fires and the
      // write goes through. Without a case that SUCCEEDS, a guard that rejected every
      // mutation would satisfy the entire suite.
      //
      // The write lands under store().receiver, which the mock deliberately makes the decoy
      // account on the global-path iteration -- that is the asymmetry under test, and it is
      // also why emplace's closing find() returns end() there: it probes _code, which the
      // decoy is not. Nothing here depends on the returned iterator.
      t.emplace("alice"_n, [](auto& o) { o.id = 2; o.sec = 7; });
      CHECK_EQUAL( store().sets, 1u )
      const auto written = mock_kv::row_key{store().receiver, records_tid,
                                            pk_key("alice"_n.value, 2)};
      CHECK_EQUAL( store().rows.count(written), 1u )
      // Not merely present: the row must be the one this emplace serialized, so a write
      // that landed with the wrong key or wrong contents is not mistaken for success.
      CHECK_EQUAL( store().rows.at(written).empty(), false )
      CHECK_EQUAL( store().rows.at(written) == std::string("row"), false )
   }
SYSIO_TEST_END

// modify and erase must also SUCCEED on an owned handle. Without these, an inverted or
// unconditional guard on either would satisfy every required test: the foreign-code case proves
// only that they reject, and their allowed paths ran solely in the opt-in integration suite.
SYSIO_TEST_BEGIN(own_table_handle_can_modify_and_erase)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t t("alice"_n, "alice"_n.value);

      // The mock seeds the row under the table's OWNER; writes land under the receiver, which
      // on the global-path iteration is the decoy. Address each by the account that holds it.
      const auto owned   = mock_kv::row_key{"alice"_n.value, records_tid, pk_key("alice"_n.value, 1)};
      const auto written = mock_kv::row_key{store().receiver, records_tid, pk_key("alice"_n.value, 1)};

      record r{1, 7};
      t.modify(r, "alice"_n, [](auto& o) { o.sec = 9; });
      CHECK_EQUAL( store().sets, 1u )
      // Serialized, not the seeded placeholder -- so a modify that wrote nothing, or wrote the
      // wrong key, is not read as success.
      CHECK_EQUAL( store().rows.count(written), 1u )
      CHECK_EQUAL( store().rows.at(written) == std::string("row"), false )

      // erase() removes the row it addresses. It is keyed the same way kv_set is, so it lands
      // on the receiver too.
      t.erase(r);
      CHECK_EQUAL( store().erases, 1u )
      CHECK_EQUAL( store().rows.count(written), 0u )
      // The owner's seeded row is untouched on the decoy iteration, gone on the fallback one
      // where receiver == owner -- either way the erase hit exactly the namespace it wrote to.
      CHECK_EQUAL( store().rows.count(owned), via_global ? 1u : 0u )
   }
SYSIO_TEST_END

// The primary bounds take a `name` as well as a uint64_t, matching the two-overload shape
// find/require_find/get have always used. Compile-time only -- nothing here is evaluated.
SYSIO_TEST_BEGIN(primary_bounds_accept_uint64_and_name)
   using itr_t = table_t::const_iterator;

   // Pin BOTH overloads by exact signature. A named static_cast resolves an overload set, so
   // these fail to compile if either parameter type changes -- which is what would happen if
   // the uint64_t parameter were ever swapped for a converting proxy again. That also
   // demonstrates the documented escape hatch for taking a member pointer.
   constexpr auto lb_u64  = static_cast<itr_t (table_t::*)(uint64_t) const>(&table_t::lower_bound);
   constexpr auto lb_name = static_cast<itr_t (table_t::*)(name) const>(&table_t::lower_bound);
   constexpr auto ub_u64  = static_cast<itr_t (table_t::*)(uint64_t) const>(&table_t::upper_bound);
   constexpr auto ub_name = static_cast<itr_t (table_t::*)(name) const>(&table_t::upper_bound);
   static_assert(lb_u64 && lb_name && ub_u64 && ub_name, "both bound overloads must exist");

   // A real uint64_t parameter, so every conversion the base performed is unchanged. The
   // braced forms in particular must stay unambiguous: name's uint64_t constructor is
   // explicit, so name is never viable for a braced integer.
   // declval, not a dereferenced null: these appear only in unevaluated operands, and the
   // test body itself must stay well-defined at run time.
#define LB(expr) decltype(std::declval<const table_t&>().lower_bound expr)
#define UB(expr) decltype(std::declval<const table_t&>().upper_bound expr)
   static_assert(std::is_same_v<LB((uint64_t{42})),    itr_t>, "uint64_t");
   static_assert(std::is_same_v<LB(("alice"_n)),       itr_t>, "a name");
   // The braced-literal case is the point of the assertion, so the diagnostic it provokes is
   // suppressed rather than avoided.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wbraced-scalar-init"
   static_assert(std::is_same_v<LB(({42})),            itr_t>, "braced literal");
#pragma clang diagnostic pop
   static_assert(std::is_same_v<LB(({})),              itr_t>, "empty brace, key zero");
   static_assert(std::is_same_v<LB((wrapped_key{7})),  itr_t>, "uint64-convertible wrapper");
   static_assert(std::is_same_v<UB(("alice"_n)),       itr_t>, "a name");

   // The documented costs. A dual-convertible wrapper is ambiguous here, as it already was
   // for find/get/require_find -- consistent with the siblings, but a source break against
   // the single uint64_t parameter, so it is pinned rather than left to be rediscovered.
   static_assert(callable_with<uint64_t>::value && callable_with<name>::value &&
                 callable_with<wrapped_key>::value, "the accepted domain must stay callable");
   static_assert(!callable_with<dual_key>::value,
                 "a uint64_t-and-name-convertible wrapper is ambiguous, as it is for find()");
#undef LB
#undef UB
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   SYSIO_TEST(duplicate_primary_key_rejected)
   SYSIO_TEST(foreign_code_handle_cannot_mutate)
   SYSIO_TEST(own_table_handle_passes_the_guard)
   SYSIO_TEST(own_table_handle_can_modify_and_erase)
   SYSIO_TEST(primary_bounds_accept_uint64_and_name)
   return has_failed();
}
