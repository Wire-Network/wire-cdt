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
 *    1. A duplicate primary key aborts. emplace writes in kv_set's insert mode, which refuses it. An
 *       upsert would silently overwrite the row and store_secondaries would strand the old mapping.
 *    2. Mutating through a handle opened on another account aborts. Reads take a `code`
 *       argument and honour it; kv_set, kv_erase and kv_idx_store have none and always land
 *       on the receiver. table_id derives from the table NAME alone, so a foreign-code
 *       mutation probes their table and writes the receiver's row of the same name.
 *
 *  The cases come in two kinds. The REJECTING ones abort before any write, so their store is
 *  seeded directly rather than through emplace. The ALLOWING ones -- an owned handle doing
 *  emplace, modify and erase -- run the mutation through. Both kinds matter: without the allowing
 *  ones, a guard that rejected everything would satisfy the suite.
 *
 *  The mock in kv_mock.hpp keeps the asymmetry under test: kv_get honours `code`, while kv_set and
 *  kv_erase have no such parameter and always land on the receiver. Rows are keyed by the account
 *  that holds them, which is how a misdirected write is made visible.
 *
 *  The later cases pin what the shim costs, that iterators and references into its row cache stay
 *  good across a modify and a reverse read, as upstream's do, and what an erase leaves behind.
 */

#include <sysio/tester.hpp>
#include <sysio/multi_index.hpp>
#include <sysio/kv_constants.hpp>

#include "kv_mock.hpp"

#include <iterator>
#include <limits>
#include <map>
#include <type_traits>
#include <utility>
#include <cstring>
#include <string>
#include <tuple>

using namespace sysio;
using namespace sysio::native;
using kv_mock::host;
using row_key_t = kv_mock::store::row_key;

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

/// The 16-byte primary key multi_index builds: [scope:8B BE][pk:8B BE].
std::string pk_key(uint64_t scope, uint64_t pk) {
   std::string k(16, '\0');
   for (int i = 7; i >= 0; --i) { k[i]      = char(scope & 0xFF); scope >>= 8; }
   for (int i = 7; i >= 0; --i) { k[8 + i]  = char(pk    & 0xFF); pk    >>= 8; }
   return k;
}

/// The account whose table is under test is never this one. When the dispatcher-global path
/// is being exercised, the current_receiver intrinsic is pointed here instead, so the two
/// branches of receiving_account() cannot return the same answer.
constexpr uint64_t decoy_receiver = "carol"_n.value;

/// What using an iterator after erasing its row raises: kv::msg::iterator_erased, as the array
/// CHECK_ASSERT measures.
constexpr char row_erased[] = "cannot use an iterator after erasing its row";

/// What stepping back from a default-constructed iterator raises: kv::msg::no_table, likewise.
constexpr char no_table[] = "cannot position a default-constructed iterator";

/// Overwrite the stack below the caller, so a reference into a dead frame reads something else.
[[gnu::noinline]] void clobber_stack() {
   volatile char junk[4096];
   for (auto& byte : junk) byte = 0x5A;
}

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
   host().reset(dispatcher_sets_name ? decoy_receiver : owner);
   host().seed(owner, records_tid, pk_key(scope, pk), "row");
   kv_mock::install();
   sysio_set_contract_name(dispatcher_sets_name ? owner : 0);
}

} // namespace

// A duplicate primary key must abort rather than upsert. kv_set's insert mode is what refuses it,
// so the call is made and the row must come through unchanged. An insert looks at the receiver's
// table, which on the global-path iteration is the decoy, so the row is seeded there as well.
SYSIO_TEST_BEGIN(duplicate_primary_key_rejected)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      const auto written = row_key_t{host().receiver, records_tid, pk_key("alice"_n.value, 1)};
      host().rows[written] = "row";
      table_t t("alice"_n, "alice"_n.value);

      CHECK_ASSERT( "object with the same primary key already exists",
                    ([&]() { t.emplace("alice"_n, [](auto& r) { r.id = 1; r.sec = 7; }); }) )
      CHECK_EQUAL( host().rows.at(written), std::string("row") )
      CHECK_EQUAL( host().calls("kv_set"), 1u )
   }
SYSIO_TEST_END

// Mutating through a foreign-code handle must abort. The receiver holds pk=1 and the foreign
// account does not, so nothing but the guard stands between these calls and the receiver's row.
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
      CHECK_EQUAL( host().calls("kv_set"), 0u )
      const auto seeded = row_key_t{"alice"_n.value, records_tid,
                                           pk_key("alice"_n.value, 1)};
      CHECK_EQUAL( host().rows.count(seeded), 1u )
      CHECK_EQUAL( host().rows.at(seeded), std::string("row") )
   }
SYSIO_TEST_END

// A handle on the receiver's own table is unaffected by the guard.
SYSIO_TEST_BEGIN(own_table_handle_passes_the_guard)
   for (bool via_global : {true, false}) {
      arrange("alice"_n.value, "alice"_n.value, 1, via_global);
      table_t t("alice"_n, "alice"_n.value);

      // pk=2 is absent, so neither the receiver guard nor insert mode refuses it and the
      // write goes through. Without a case that SUCCEEDS, a guard that rejected every
      // mutation would satisfy the entire suite.
      //
      // The write lands under host().receiver, which the mock deliberately makes the decoy
      // account on the global-path iteration -- that is the asymmetry under test, and it is
      // also why emplace's closing find() returns end() there: it reads _code, which the decoy
      // is not. Nothing here depends on the returned iterator.
      t.emplace("alice"_n, [](auto& o) { o.id = 2; o.sec = 7; });
      CHECK_EQUAL( host().calls("kv_set"), 1u )
      const auto written = row_key_t{host().receiver, records_tid,
                                            pk_key("alice"_n.value, 2)};
      CHECK_EQUAL( host().rows.count(written), 1u )
      // Not merely present: the row must be the one this emplace serialized, so a write
      // that landed with the wrong key or wrong contents is not mistaken for success.
      CHECK_EQUAL( host().rows.at(written).empty(), false )
      CHECK_EQUAL( host().rows.at(written) == std::string("row"), false )
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
      // modify needs its row under the receiver, so on that iteration it is seeded there as well.
      const auto owned   = row_key_t{"alice"_n.value, records_tid, pk_key("alice"_n.value, 1)};
      const auto written = row_key_t{host().receiver, records_tid, pk_key("alice"_n.value, 1)};
      host().rows[written] = "row";
      const auto packed = sysio::pack(record{1, 7});
      host().rows[owned].assign(packed.begin(), packed.end());

      const record& r = t.get(1);
      t.modify(r, "alice"_n, [](auto& o) { o.sec = 9; });
      CHECK_EQUAL( host().calls("kv_set"), 1u )
      // Serialized, not the seeded placeholder -- so a modify that wrote nothing, or wrote the
      // wrong key, is not read as success.
      CHECK_EQUAL( host().rows.count(written), 1u )
      CHECK_EQUAL( host().rows.at(written) == std::string("row"), false )

      // erase() removes the row it addresses. It is keyed the same way kv_set is, so it lands
      // on the receiver too.
      t.erase(r);
      CHECK_EQUAL( host().calls("kv_erase"), 1u )
      CHECK_EQUAL( host().rows.count(written), 0u )
      // The owner's seeded row is untouched on the decoy iteration, gone on the fallback one
      // where receiver == owner -- either way the erase hit exactly the namespace it wrote to.
      CHECK_EQUAL( host().rows.count(owned), via_global ? 1u : 0u )
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

// Costs on the new interface. multi_index is the compatibility shim and takes simple wins only, so
// these pin what it pays rather than what the kv::table family pays.
SYSIO_TEST_BEGIN(primary_scan_reads_key_in_the_step)
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   host().seed("alice"_n.value, records_tid, pk_key("alice"_n.value, 2), std::string(16, 'a'));
   host().seed("alice"_n.value, records_tid, pk_key("alice"_n.value, 3), std::string(16, 'b'));
   host().rows.erase(row_key_t{"alice"_n.value, records_tid, pk_key("alice"_n.value, 1)});
   table_t t("alice"_n, "alice"_n.value);

   auto it = t.begin();
   host().clear_counts();
   ++it;
   CHECK_EQUAL( host().calls("kv_it_next"), 1u )
   CHECK_EQUAL( host().calls("kv_get"), 1u )
   CHECK_EQUAL( host().total(), 2u )
SYSIO_TEST_END

// find keeps its existence probe and its handle, unlike kv::table. docs/kv-multi-index.md quotes this.
SYSIO_TEST_BEGIN(find_hit_costs_four_host_calls)
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   host().rows.begin()->second = std::string(16, '\0');
   table_t t("alice"_n, "alice"_n.value);
   auto it = t.find(1);
   CHECK_EQUAL( it != t.end(), true )
   CHECK_EQUAL( host().calls("kv_get"), 2u )
   CHECK_EQUAL( host().calls("kv_it_create"), 1u )
   CHECK_EQUAL( host().calls("kv_it_read"), 1u )
   CHECK_EQUAL( host().total(), 4u )
SYSIO_TEST_END

// emplace pays one kv_set and no contains probe, and reads nothing back: it holds the row it wrote,
// so the iterator it returns costs a create and a read.
SYSIO_TEST_BEGIN(emplace_makes_one_write_and_no_probe)
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   table_t t("alice"_n, "alice"_n.value);
   {
      auto it = t.emplace("alice"_n, [](auto& r) { r.id = 2; r.sec = 7; });
      CHECK_EQUAL( it->id, 2u )
   }
   CHECK_EQUAL( host().calls("kv_set"), 1u )
   CHECK_EQUAL( host().calls("kv_get"), 0u )
   CHECK_EQUAL( host().calls("kv_it_create"), 1u )
   CHECK_EQUAL( host().open_handles(), 0u )
SYSIO_TEST_END

// --end() seeks past every key, so a row at the largest primary key is the one it reaches. The hand
// built [scope][0xFF...] this replaced landed ON that row, and prev then skipped it.
SYSIO_TEST_BEGIN(end_decrement_reaches_the_largest_primary_key)
   const uint64_t max_pk = std::numeric_limits<uint64_t>::max();
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   table_t t("alice"_n, "alice"_n.value);
   t.emplace("alice"_n, [&](auto& r) { r.id = max_pk; r.sec = 7; });

   auto it = t.end();
   --it;
   CHECK_EQUAL( it->id, max_pk )
SYSIO_TEST_END

// available_primary_key on the new calls: an empty scope, the normal case, and the limit.
SYSIO_TEST_BEGIN(available_primary_key_paths)
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   host().rows.clear();
   table_t empty("alice"_n, "alice"_n.value);
   CHECK_EQUAL( empty.available_primary_key(), 0u )

   arrange("alice"_n.value, "alice"_n.value, 4, false);
   table_t used("alice"_n, "alice"_n.value);
   host().rows.begin()->second = std::string(16, '\0');
   CHECK_EQUAL( used.available_primary_key(), 5u )
   CHECK_EQUAL( host().open_handles(), 0u )

   arrange("alice"_n.value, "alice"_n.value, std::numeric_limits<uint64_t>::max(), false);
   table_t full("alice"_n, "alice"_n.value);
   CHECK_ASSERT( "next primary key in table is at autoincrement limit",
                 ([&]() { full.available_primary_key(); }) )
SYSIO_TEST_END

// Open, step back, destroy. The step back reads the last key, so no separate read looks for the
// largest one first.
SYSIO_TEST_BEGIN(available_primary_key_is_three_host_calls)
   arrange("alice"_n.value, "alice"_n.value, 4, false);
   host().rows.begin()->second = std::string(16, '\0');
   table_t t("alice"_n, "alice"_n.value);
   host().clear_counts();
   CHECK_EQUAL( t.available_primary_key(), 5u )
   CHECK_EQUAL( host().calls("kv_it_create"), 1u )
   CHECK_EQUAL( host().calls("kv_it_prev"), 1u )
   CHECK_EQUAL( host().calls("kv_it_destroy"), 1u )
   CHECK_EQUAL( host().total(), 3u )

   // Asked again, the answer comes from the handle's cache.
   host().clear_counts();
   CHECK_EQUAL( t.available_primary_key(), 5u )
   CHECK_EQUAL( host().total(), 0u )
SYSIO_TEST_END

// An abort at the limit leaves nothing cached, so asking again aborts again.
SYSIO_TEST_BEGIN(available_primary_key_at_the_limit_caches_nothing)
   arrange("alice"_n.value, "alice"_n.value, std::numeric_limits<uint64_t>::max(), false);
   table_t full("alice"_n, "alice"_n.value);
   for (int attempt = 0; attempt < 2; ++attempt) {
      CHECK_ASSERT( "next primary key in table is at autoincrement limit",
                    ([&]() { full.available_primary_key(); }) )
   }
SYSIO_TEST_END

// A second handle walks a secondary index inside one scope and stops at its edge: the scope is the
// prefix, so the wrapper no longer reads the key after each step to check.
using indexed_t = sysio::multi_index<"records"_n, record,
   sysio::indexed_by<"bysec"_n, sysio::const_mem_fun<record, uint64_t, &record::get_secondary>>>;

static_assert(std::is_same_v<std::iterator_traits<table_t::const_reverse_iterator>::iterator_category,
                             std::bidirectional_iterator_tag>);

// const_reverse_iterator is its own class, so declaring a std::reverse_iterator does not compile, and
// the kv-multi-index.md divergence list says so. The other direction is what lets the result of
// std::make_reverse_iterator compare against rend().
static_assert(!std::is_convertible_v<table_t::const_reverse_iterator, std::reverse_iterator<table_t::const_iterator>>);
static_assert(std::is_convertible_v<std::reverse_iterator<table_t::const_iterator>, table_t::const_reverse_iterator>);

/// A receiver's table of the given rows, cached by the handle that wrote them.
template<typename Table>
void add_rows(Table& t, std::initializer_list<record> rows) {
   for (const record& row : rows) t.emplace("alice"_n, [&](auto& r) { r = row; });
   host().clear_counts();
}

/// Start a case on an empty table of the receiver, scope 1.
void empty_scope() {
   arrange("alice"_n.value, 1, 0, false);
   host().rows.clear();
}

SYSIO_TEST_BEGIN(reverse_iteration_visits_every_row)
   empty_scope();
   table_t t("alice"_n, 1);
   add_rows(t, {{1, 7}, {2, 7}, {3, 7}});

   std::vector<uint64_t> seen;
   for (auto rit = t.rbegin(); rit != t.rend(); ++rit) seen.push_back(rit->id);
   CHECK_EQUAL( seen.size(), 3u )
   CHECK_EQUAL( seen[0], 3u )
   CHECK_EQUAL( seen[1], 2u )
   CHECK_EQUAL( seen[2], 1u )
   CHECK_EQUAL( std::distance(t.crbegin(), t.crend()), 3 )
SYSIO_TEST_END

// With the rows cached, a reverse step is the one host call that moves the handle.
SYSIO_TEST_BEGIN(reverse_scan_is_one_host_call_a_row_when_cached)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}, {4, 40}});
   {
      auto rit = t.rbegin();
      CHECK_EQUAL( host().calls("kv_it_create"), 1u )
      CHECK_EQUAL( host().calls("kv_it_prev"), 1u )
      CHECK_EQUAL( host().total(), 2u )

      host().clear_counts();
      uint32_t rows = 0;
      for (; rit != t.rend(); ++rit) rows += rit->id != 0;
      CHECK_EQUAL( rows, 4u )
      CHECK_EQUAL( host().calls("kv_it_prev"), 4u )
      CHECK_EQUAL( host().total(), 4u )
   }
   {
      auto idx = t.get_index<"bysec"_n>();
      auto rit = idx.rbegin();
      host().clear_counts();
      uint32_t rows = 0;
      for (; rit != idx.rend(); ++rit) rows += rit->id != 0;
      CHECK_EQUAL( rows, 4u )
      CHECK_EQUAL( host().calls("kv_idx_prev"), 4u )
      CHECK_EQUAL( host().total(), 4u )
   }
   CHECK_EQUAL( host().open_handles(), 0u )
SYSIO_TEST_END

// A reference from a secondary reverse iterator is the cached row, so it lasts as that row does.
SYSIO_TEST_BEGIN(secondary_reverse_reference_is_the_cached_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});
   auto idx = t.get_index<"bysec"_n>();

   auto rit = idx.rbegin();
   const record& last = *rit;
   clobber_stack();
   CHECK_EQUAL( last.id, 2u )
   CHECK_EQUAL( rit->id, 2u )
   CHECK_EQUAL( &last == &t.get(2), true )
SYSIO_TEST_END

// Upstream's reverse walk ends at rend, and the forward iterator's abort on -- must not reach it.
SYSIO_TEST_BEGIN(reverse_step_off_the_first_row_reaches_rend)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});
   auto idx = t.get_index<"bysec"_n>();

   auto rit = t.rbegin();
   ++rit;
   CHECK_EQUAL( rit == t.rend(), true )
   --rit;
   CHECK_EQUAL( rit->id, 1u )

   auto sec = idx.rbegin();
   ++sec;
   CHECK_EQUAL( sec == idx.rend(), true )
   --sec;
   CHECK_EQUAL( sec->id, 1u )

   // The forward iterators still abort, with the text a port expects.
   auto first = t.begin();
   CHECK_ASSERT( "cannot decrement iterator at beginning of table", ([&]() { --first; }) )
   auto first_entry = idx.begin();
   CHECK_ASSERT( "cannot decrement iterator at beginning of index", ([&]() { --first_entry; }) )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_iterator_edges_abort)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});
   auto idx = t.get_index<"bysec"_n>();

   auto past = t.rend();
   CHECK_ASSERT( "cannot increment reverse iterator at rend", ([&]() { ++past; }) )
   auto past_entry = idx.rend();
   CHECK_ASSERT( "cannot increment reverse iterator at rend", ([&]() { ++past_entry; }) )
   auto first = t.rbegin();
   CHECK_ASSERT( "cannot decrement reverse iterator at rbegin", ([&]() { --first; }) )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_iteration_of_an_empty_table)
   empty_scope();
   indexed_t t("alice"_n, 1);
   auto idx = t.get_index<"bysec"_n>();
   CHECK_EQUAL( t.rbegin() == t.rend(), true )
   CHECK_EQUAL( idx.rbegin() == idx.rend(), true )
   CHECK_EQUAL( t.rend().base() == t.end(), true )
   CHECK_EQUAL( host().open_handles(), 0u )
SYSIO_TEST_END

// base() is one row past the position. ibc.cpp modifies through it.
SYSIO_TEST_BEGIN(reverse_iterator_base_is_one_row_past)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
   auto idx = t.get_index<"bysec"_n>();

   CHECK_EQUAL( t.rbegin().base() == t.end(), true )
   CHECK_EQUAL( t.rend().base() == t.begin(), true )
   CHECK_EQUAL( idx.rbegin().base() == idx.end(), true )
   CHECK_EQUAL( idx.rend().base()->id, 1u )

   auto rit = t.rbegin();
   ++rit;
   auto forward = rit.base();
   CHECK_EQUAL( forward->id, 3u )
   --forward;
   CHECK_EQUAL( forward->id, 2u )
   auto next = t.erase(forward);
   CHECK_EQUAL( next->id, 3u )

   // Postfix forms stay, as a port's reverse loops use them.
   auto walk = t.rbegin();
   auto old = walk++;
   CHECK_EQUAL( old->id, 3u )
   CHECK_EQUAL( walk->id, 1u )
SYSIO_TEST_END

// Upstream's own tests walk back from a found row with std::make_reverse_iterator and stop at rend().
SYSIO_TEST_BEGIN(std_reverse_iterator_compares_against_rend)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
   auto idx = t.get_index<"bysec"_n>();

   std::vector<uint64_t> seen;
   for (auto it = --std::make_reverse_iterator(idx.find(uint64_t{20})); it != idx.rend(); ++it) seen.push_back(it->id);
   CHECK_EQUAL( seen.size(), 2u )
   CHECK_EQUAL( seen[0], 2u )
   CHECK_EQUAL( seen[1], 1u )

   seen.clear();
   for (auto it = --std::make_reverse_iterator(t.find(2)); it != t.rend(); ++it) seen.push_back(it->id);
   CHECK_EQUAL( seen.size(), 2u )
   CHECK_EQUAL( seen[1], 1u )
SYSIO_TEST_END

// To carry on from an erase, rebuild the iterator from the result. It reads the row before with no
// step of its own. Nothing else moves a reverse iterator, which holds the row it reads.
SYSIO_TEST_BEGIN(reverse_iterator_rebuilds_from_the_erase_result)
   empty_scope();
   {
      indexed_t t("alice"_n, 1);
      add_rows(t, {{1, 10}, {2, 20}, {3, 30}, {4, 40}, {5, 50}});
      using reverse = indexed_t::const_reverse_iterator;

      // Out of the middle, with a row left on each side.
      auto rit = t.rbegin();
      ++rit;
      ++rit;
      CHECK_EQUAL( rit->id, 3u )
      rit = reverse(t.erase(--rit.base()));
      CHECK_EQUAL( rit->id, 2u )

      // The rest, from the last row down to rend.
      rit = t.rbegin();
      for (uint64_t id : {uint64_t{5}, uint64_t{4}, uint64_t{2}, uint64_t{1}}) {
         CHECK_EQUAL( rit->id, id )
         rit = reverse(t.erase(--rit.base()));
      }
      CHECK_EQUAL( rit == t.rend(), true )
      CHECK_EQUAL( host().rows.size(), 0u )
      CHECK_EQUAL( host().entries.size(), 0u )
   }
   CHECK_EQUAL( host().open_handles(), 0u )

   // The same over a secondary index, through its own erase.
   {
      indexed_t t("alice"_n, 1);
      add_rows(t, {{1, 30}, {2, 20}, {3, 10}});
      auto idx = t.get_index<"bysec"_n>();
      using reverse = decltype(idx)::const_reverse_iterator;

      auto rit = idx.rbegin();
      for (uint64_t id : {uint64_t{1}, uint64_t{2}, uint64_t{3}}) {
         CHECK_EQUAL( rit->id, id )
         rit = reverse(idx.erase(--rit.base()));
      }
      CHECK_EQUAL( rit == idx.rend(), true )
      CHECK_EQUAL( host().rows.size(), 0u )
   }
   CHECK_EQUAL( host().open_handles(), 0u )
SYSIO_TEST_END

// The loop upstream's iterator allows. Nothing moves this one, so its second pass reads the row it
// just erased, and says so rather than reporting the missing key or index entry that follows.
SYSIO_TEST_BEGIN(reverse_loop_with_no_step_aborts)
   empty_scope();
   {
      table_t t("alice"_n, 1);
      add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
      auto rit = t.rbegin();
      CHECK_ASSERT( row_erased, ([&]() { while (rit != t.rend()) t.erase(*rit); }) )
      CHECK_EQUAL( host().rows.size(), 2u )
      CHECK_EQUAL( host().calls("kv_erase"), 1u )
   }
   empty_scope();
   {
      indexed_t t("alice"_n, 1);
      add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
      auto rit = t.rbegin();
      CHECK_ASSERT( row_erased, ([&]() { while (rit != t.rend()) t.erase(*rit); }) )
      CHECK_EQUAL( host().rows.size(), 2u )
   }
   empty_scope();
   {
      indexed_t t("alice"_n, 1);
      add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
      auto idx = t.get_index<"bysec"_n>();
      auto rit = idx.rbegin();
      CHECK_ASSERT( row_erased, ([&]() { while (rit != idx.rend()) t.erase(*rit); }) )
      CHECK_EQUAL( host().rows.size(), 2u )
   }
SYSIO_TEST_END

// After an erase, what needs the row aborts: a read, and a copy, which base() and rit++ make. What
// needs only the position carries on. ++ reaches the row before, which upstream's iterator skipped.
SYSIO_TEST_BEGIN(reverse_iterator_on_an_erased_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}, {4, 40}});
   auto idx = t.get_index<"bysec"_n>();

   auto rit = t.rbegin();
   auto entry = idx.rbegin();
   t.erase(*rit);
   CHECK_EQUAL( rit != t.rend(), true )
   CHECK_ASSERT( row_erased, ([&]() { (void)*rit; }) )
   CHECK_ASSERT( row_erased, ([&]() { (void)rit->id; }) )
   CHECK_ASSERT( row_erased, ([&]() { rit.base(); }) )
   CHECK_ASSERT( row_erased, ([&]() { rit++; }) )
   CHECK_ASSERT( row_erased, ([&]() { (void)entry->id; }) )
   CHECK_ASSERT( row_erased, ([&]() { entry.base(); }) )
   ++rit;
   CHECK_EQUAL( rit->id, 3u )
   ++entry;
   CHECK_EQUAL( entry->id, 3u )

   // -- from an erased row reaches the row after it.
   ++rit;
   CHECK_EQUAL( rit->id, 2u )
   t.erase(*rit);
   --rit;
   CHECK_EQUAL( rit->id, 3u )
SYSIO_TEST_END

// It reads the row it holds, so a row written past the position waits for a new rbegin().
SYSIO_TEST_BEGIN(reverse_iterator_does_not_see_a_later_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});

   auto rit = t.rbegin();
   t.emplace("alice"_n, [](auto& r) { r.id = 3; r.sec = 30; });
   CHECK_EQUAL( rit->id, 2u )
   CHECK_EQUAL( t.rbegin()->id, 3u )
   ++rit;
   CHECK_EQUAL( rit->id, 1u )
SYSIO_TEST_END

// On an empty table rbegin() is rend(), so -- has nowhere to go from either.
SYSIO_TEST_BEGIN(reverse_decrement_of_an_empty_table_aborts)
   empty_scope();
   indexed_t t("alice"_n, 1);
   auto idx = t.get_index<"bysec"_n>();

   auto first = t.rbegin();
   CHECK_ASSERT( "cannot decrement reverse iterator at rbegin", ([&]() { --first; }) )
   auto past = t.rend();
   CHECK_ASSERT( "cannot decrement reverse iterator at rbegin", ([&]() { --past; }) )
   auto first_entry = idx.rbegin();
   CHECK_ASSERT( "cannot decrement reverse iterator at rbegin", ([&]() { --first_entry; }) )
   auto past_entry = idx.rend();
   CHECK_ASSERT( "cannot decrement reverse iterator at rbegin", ([&]() { --past_entry; }) )
SYSIO_TEST_END

// A default-constructed iterator has no table to seek, whichever path reaches the step back.
SYSIO_TEST_BEGIN(default_iterator_has_no_table_to_step_back_in)
   empty_scope();
   indexed_t t("alice"_n, 1);
   auto idx = t.get_index<"bysec"_n>();
   using entry_iterator = decltype(idx)::const_iterator;
   using reverse_entry  = decltype(idx)::const_reverse_iterator;

   indexed_t::const_iterator none;
   CHECK_ASSERT( no_table, ([&]() { --none; }) )
   CHECK_ASSERT( no_table, ([&]() { indexed_t::const_reverse_iterator rit(none); }) )
   entry_iterator no_entry;
   CHECK_ASSERT( no_table, ([&]() { --no_entry; }) )
   CHECK_ASSERT( no_table, ([&]() { reverse_entry rit(no_entry); }) )

   indexed_t::const_reverse_iterator unset;
   CHECK_ASSERT( no_table, ([&]() { --unset; }) )
   CHECK_ASSERT( no_table, ([&]() { unset.base(); }) )
   reverse_entry unset_entry;
   CHECK_ASSERT( no_table, ([&]() { --unset_entry; }) )
   CHECK_ASSERT( no_table, ([&]() { unset_entry.base(); }) )
SYSIO_TEST_END

// The row at --end() of a secondary index, at the very top of the key space: the largest scope and
// the largest secondary key.
SYSIO_TEST_BEGIN(end_decrement_reaches_the_largest_secondary_key)
   const uint64_t top = std::numeric_limits<uint64_t>::max();
   arrange("alice"_n.value, top, 0, false);
   host().rows.clear();
   indexed_t t("alice"_n, top);
   add_rows(t, {{1, 5}, {2, top}});
   auto idx = t.get_index<"bysec"_n>();

   auto it = idx.end();
   --it;
   CHECK_EQUAL( it != idx.end(), true )
   CHECK_EQUAL( it->id, 2u )
   CHECK_EQUAL( idx.rbegin()->id, 2u )
SYSIO_TEST_END

// get hands back the cached row, not a member of an iterator that is gone by the time it returns.
SYSIO_TEST_BEGIN(secondary_get_refers_to_the_cached_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});
   auto idx = t.get_index<"bysec"_n>();

   const record& found = idx.get(uint64_t{20});
   clobber_stack();
   CHECK_EQUAL( found.id, 2u )
   CHECK_EQUAL( &found == &t.get(2), true )
   CHECK_ASSERT( "unable to find secondary key", ([&]() { idx.get(uint64_t{30}); }) )
SYSIO_TEST_END

// A modify keeps the cached object, so an iterator and a reference on the row carry on working and
// see the new value.
SYSIO_TEST_BEGIN(modify_keeps_the_cached_object)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});

   auto it = t.find(1);
   const record* cached = &*it;
   t.modify(it, "alice"_n, [](auto& r) { r.sec = 11; });
   CHECK_EQUAL( &t.get(1) == cached, true )
   CHECK_EQUAL( it->sec, 11u )
   CHECK_EQUAL( cached->sec, 11u )

   // Through a secondary iterator, which points at the same object.
   auto idx = t.get_index<"bysec"_n>();
   auto sec = idx.find(uint64_t{11});
   CHECK_EQUAL( sec.operator->() == cached, true )
   idx.modify(sec, "alice"_n, [](auto& r) { r.sec = 12; });
   CHECK_EQUAL( &t.get(1) == cached, true )
   CHECK_EQUAL( cached->sec, 12u )
SYSIO_TEST_END

// A mutator takes a row of this handle, as upstream's do. A copy can be out of date, and its secondary
// keys are what the host would be told to move, so taking one could split a row from its index.
SYSIO_TEST_BEGIN(mutators_refuse_a_copy_of_a_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});
   auto idx = t.get_index<"bysec"_n>();

   record copy = t.get(1);
   t.modify(t.get(1), "alice"_n, [](auto& r) { r.sec = 11; });
   CHECK_ASSERT( "object passed to modify is not in multi_index",
                 ([&]() { t.modify(copy, "alice"_n, [](auto& r) { r.sec = 10; }); }) )
   CHECK_ASSERT( "object passed to erase is not in multi_index", ([&]() { t.erase(copy); }) )
   CHECK_EQUAL( t.get(1).sec, 11u )
   CHECK_EQUAL( idx.find(uint64_t{11})->id, 1u )

   // So is a row of another handle, or one no handle has read.
   indexed_t other("alice"_n, 1);
   const record& theirs = other.get(1);
   CHECK_ASSERT( "object passed to modify is not in multi_index",
                 ([&]() { t.modify(theirs, "alice"_n, [](auto& r) { r.sec = 12; }); }) )
   CHECK_ASSERT( "object passed to erase is not in multi_index", ([&]() { t.erase(theirs); }) )
   indexed_t reader("alice"_n, 1);
   record built{1, 11};
   CHECK_ASSERT( "object passed to modify is not in multi_index",
                 ([&]() { reader.modify(built, "alice"_n, [](auto& r) { r.sec = 12; }); }) )
SYSIO_TEST_END

// A modify of a row that has gone is refused rather than recreating it, as upstream refuses.
SYSIO_TEST_BEGIN(modify_of_a_gone_row_aborts)
   empty_scope();
   table_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});

   const record& stale = t.get(1);
   table_t other("alice"_n, 1);
   other.erase(other.get(1));

   CHECK_ASSERT( "cannot modify a row that no longer exists",
                 ([&]() { t.modify(stale, "alice"_n, [](auto& r) { r.sec = 11; }); }) )
   CHECK_EQUAL( host().rows.size(), 0u )
SYSIO_TEST_END

// same_payer keeps working on update mode: the row and its index entry stay with whoever pays for
// them. The payer, the code and the receiver are three accounts here, so billing either of the other
// two in place of "keep the payer" shows.
SYSIO_TEST_BEGIN(modify_with_same_payer_keeps_the_row_payer)
   arrange("alice"_n.value, 1, 0, true);   // code alice, receiver the decoy
   host().rows.clear();
   indexed_t t("alice"_n, 1);
   t.emplace("bob"_n, [](auto& r) { r.id = 1; r.sec = 10; });
   CHECK_EQUAL( "bob"_n.value == "alice"_n.value || "bob"_n.value == host().receiver, false )
   CHECK_EQUAL( host().receiver == "alice"_n.value, false )

   const auto stored = row_key_t{host().receiver, records_tid, pk_key(1, 1)};
   t.modify(t.get(1), same_payer, [](auto& r) { r.sec = 11; });
   CHECK_EQUAL( host().row_payers.at(stored), "bob"_n.value )
   CHECK_EQUAL( host().entries.begin()->second, "bob"_n.value )

   t.modify(t.get(1), "dave"_n, [](auto& r) { r.sec = 12; });
   CHECK_EQUAL( host().row_payers.at(stored), "dave"_n.value )
   CHECK_EQUAL( host().entries.begin()->second, "dave"_n.value )
SYSIO_TEST_END

// An erase ends every iterator on the row, a secondary one included: it points at the cached row, as
// a primary one does. What needs that row aborts. What needs only the position carries on.
SYSIO_TEST_BEGIN(erase_ends_the_iterators_on_the_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
   auto idx = t.get_index<"bysec"_n>();

   auto primary = t.find(2);
   auto secondary = idx.find(uint64_t{20});
   auto sibling = secondary;
   t.erase(*primary);

   CHECK_ASSERT( row_erased, ([&]() { (void)*primary; }) )
   CHECK_ASSERT( row_erased, ([&]() { (void)primary->id; }) )
   CHECK_ASSERT( row_erased, ([&]() { auto copy = primary; }) )
   CHECK_ASSERT( row_erased, ([&]() { t.erase(primary); }) )
   CHECK_ASSERT( row_erased, ([&]() { t.modify(primary, "alice"_n, [](auto&) {}); }) )

   CHECK_ASSERT( row_erased, ([&]() { (void)*secondary; }) )
   CHECK_ASSERT( row_erased, ([&]() { (void)secondary->id; }) )
   CHECK_ASSERT( row_erased, ([&]() { auto copy = secondary; }) )
   CHECK_ASSERT( row_erased, ([&]() { idx.erase(secondary); }) )
   CHECK_ASSERT( row_erased, ([&]() { idx.modify(secondary, "alice"_n, [](auto&) {}); }) )
   CHECK_EQUAL( host().rows.size(), 2u )

   CHECK_EQUAL( primary == t.end(), false )
   CHECK_EQUAL( secondary == idx.end(), false )
   CHECK_EQUAL( primary == t.find(1), false )
   ++primary;
   CHECK_EQUAL( primary->id, 3u )
   ++secondary;
   CHECK_EQUAL( secondary->id, 3u )
   --sibling;
   CHECK_EQUAL( sibling->id, 1u )

   // An erase through an iterator ends that iterator too, whichever form takes it.
   auto entry = idx.begin();
   auto next = idx.erase(entry);
   CHECK_EQUAL( next->id, 3u )
   CHECK_ASSERT( row_erased, ([&]() { (void)entry->id; }) )
   auto last = t.find(3);
   t.erase(last);
   CHECK_ASSERT( row_erased, ([&]() { (void)last->id; }) )
SYSIO_TEST_END

// The handle keeps the object an erased row had, for iterators still on it, but the row is gone.
SYSIO_TEST_BEGIN(erased_row_is_gone_from_the_handle)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});
   auto idx = t.get_index<"bysec"_n>();

   const record copy = t.get(1);
   t.erase(t.get(1));
   CHECK_ASSERT( "unable to find key", ([&]() { t.get(1); }) )
   CHECK_ASSERT( "object passed to iterator_to is not in multi_index", ([&]() { t.iterator_to(copy); }) )
   CHECK_EQUAL( t.find(1) == t.end(), true )
   CHECK_EQUAL( idx.find(uint64_t{10}) == idx.end(), true )
SYSIO_TEST_END

// A later write at the same primary key reuses the object, so an iterator left on the row reads the
// row now there. An erased row can come back through another handle too: a read finds it, and a
// modify picks it up.
SYSIO_TEST_BEGIN(write_after_an_erase_reuses_the_cached_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}});

   auto it = t.find(1);
   const record* cached = &*it;
   t.erase(*it);
   t.emplace("alice"_n, [](auto& r) { r.id = 1; r.sec = 11; });
   CHECK_EQUAL( it->sec, 11u )
   CHECK_EQUAL( &t.get(1) == cached, true )

   indexed_t other("alice"_n, 1);
   t.erase(*it);
   add_rows(other, {{1, 12}});
   CHECK_EQUAL( t.get(1).sec, 12u )
   CHECK_EQUAL( it->sec, 12u )
   CHECK_EQUAL( &t.get(1) == cached, true )

   t.erase(*it);
   add_rows(other, {{1, 13}});
   t.modify(t.get(1), "alice"_n, [](auto& r) { r.sec = 14; });
   host().clear_counts();
   CHECK_EQUAL( t.get(1).sec, 14u )
   CHECK_EQUAL( host().total(), 0u )
   CHECK_EQUAL( it->sec, 14u )
SYSIO_TEST_END

// The host keeps a handle on the key it was on, so after a modify moves an entry the iterator reads
// the new value and steps from the old key. A copy opens its handle at the new one, as does a find.
// docs/kv-multi-index.md quotes the first half.
SYSIO_TEST_BEGIN(secondary_step_after_a_key_move_starts_from_the_old_key)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}, {3, 30}});
   auto idx = t.get_index<"bysec"_n>();

   auto it = idx.find(uint64_t{10});
   idx.modify(it, "alice"_n, [](auto& r) { r.sec = 25; });
   CHECK_EQUAL( it->sec, 25u )
   auto copy = it;
   ++copy;
   CHECK_EQUAL( copy->id, 3u )
   auto found = idx.find(uint64_t{25});
   ++found;
   CHECK_EQUAL( found->id, 3u )
   ++it;
   CHECK_EQUAL( it->id, 2u )

   // The same when the table moves the entry behind the iterator. Entries are now 20, 25, 30.
   auto behind = idx.find(uint64_t{20});
   t.modify(t.get(2), "alice"_n, [](auto& r) { r.sec = 28; });
   CHECK_EQUAL( behind->sec, 28u )
   auto behind_copy = behind;
   ++behind_copy;
   CHECK_EQUAL( behind_copy->id, 3u )
   ++behind;
   CHECK_EQUAL( behind->id, 1u )
SYSIO_TEST_END

// A seek takes upper_bound past all the duplicates at once, with no step through them.
SYSIO_TEST_BEGIN(secondary_upper_bound_seeks_past_duplicates)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 5}, {2, 5}, {3, 5}, {4, 5}, {5, 5}, {6, 5}, {7, 9}});
   auto idx = t.get_index<"bysec"_n>();

   host().clear_counts();
   {
      auto it = idx.upper_bound(uint64_t{5});
      CHECK_EQUAL( it->id, 7u )
      CHECK_EQUAL( host().calls("kv_idx_create"), 1u )
      CHECK_EQUAL( host().calls("kv_idx_read"), 1u )
      CHECK_EQUAL( host().total(), 2u )
   }
   CHECK_EQUAL( idx.upper_bound(uint64_t{4})->id, 1u )     // a key with no entries
   CHECK_EQUAL( idx.upper_bound(uint64_t{9}) == idx.end(), true )
   CHECK_EQUAL( idx.upper_bound(uint64_t{10}) == idx.end(), true )

   // Stepping back from the bound lands on the last duplicate, which is the largest primary key.
   auto bound = idx.upper_bound(uint64_t{5});
   --bound;
   CHECK_EQUAL( bound->id, 6u )
SYSIO_TEST_END

// upper_bound stops at the top of the key range, and stays inside the scope.
SYSIO_TEST_BEGIN(secondary_upper_bound_at_the_top_of_the_scope)
   const uint64_t top = std::numeric_limits<uint64_t>::max();
   arrange("alice"_n.value, 1, 0, false);
   host().rows.clear();
   indexed_t first("alice"_n, 1);
   indexed_t second("alice"_n, 2);
   add_rows(first, {{1, top}});
   add_rows(second, {{1, 0}});

   auto idx = first.get_index<"bysec"_n>();
   CHECK_EQUAL( idx.upper_bound(top) == idx.end(), true )
   CHECK_EQUAL( idx.upper_bound(top - 1)->id, 1u )
SYSIO_TEST_END

// erase(iterator) steps the iterator it was given, so it opens no second handle.
SYSIO_TEST_BEGIN(erase_by_iterator_opens_no_second_handle)
   empty_scope();
   table_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});

   auto it = t.find(1);
   host().clear_counts();
   it = t.erase(std::move(it));
   CHECK_EQUAL( it->id, 2u )
   CHECK_EQUAL( host().calls("kv_it_create"), 0u )
   CHECK_EQUAL( host().calls("kv_it_next"), 1u )
   CHECK_EQUAL( host().calls("kv_erase"), 1u )
SYSIO_TEST_END

// Another table object moved the entry, so a copy can no longer find the row by its secondary key. It
// aborts rather than sitting on the row with no handle, where ++ could never move it.
SYSIO_TEST_BEGIN(secondary_copy_of_a_moved_entry_aborts)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});
   auto idx = t.get_index<"bysec"_n>();
   auto it = idx.begin();

   indexed_t other("alice"_n, 1);
   other.modify(other.get(1), "alice"_n, [](auto& r) { r.sec = 11; });
   CHECK_ASSERT( "cannot copy an iterator whose index entry has moved", ([&]() { auto copy = it; }) )

   // The same when only another row holds that secondary key, so the walk from it never finds this one.
   other.modify(other.get(2), "alice"_n, [](auto& r) { r.sec = 10; });
   other.erase(other.get(1));
   CHECK_ASSERT( "cannot copy an iterator whose index entry has moved", ([&]() { auto copy = it; }) )
SYSIO_TEST_END

// --end() has nowhere to go on an empty table, as --lower_bound() has not. rbegin() is unaffected, and
// is rend().
SYSIO_TEST_BEGIN(end_decrement_of_an_empty_table_aborts)
   empty_scope();
   indexed_t t("alice"_n, 1);
   auto idx = t.get_index<"bysec"_n>();

   auto end = t.end();
   CHECK_ASSERT( "cannot decrement end iterator when the table is empty", ([&]() { --end; }) )
   auto entry_end = idx.end();
   CHECK_ASSERT( "cannot decrement end iterator when the index is empty", ([&]() { --entry_end; }) )
   CHECK_EQUAL( t.rbegin() == t.rend(), true )
   CHECK_EQUAL( idx.rbegin() == idx.rend(), true )
SYSIO_TEST_END

// The largest key can be stored whether or not available_primary_key() has run. Only that call
// refuses, and the two keys upstream reserves are never handed out.
SYSIO_TEST_BEGIN(primary_key_limit_follows_upstream)
   constexpr uint64_t largest = std::numeric_limits<uint64_t>::max();
   constexpr uint64_t last_handed_out = largest - 2;

   empty_scope();
   table_t fresh("alice"_n, 1);
   add_rows(fresh, {{largest, 1}});
   CHECK_EQUAL( fresh.get(largest).sec, 1u )
   CHECK_ASSERT( "next primary key in table is at autoincrement limit", ([&]() { fresh.available_primary_key(); }) )

   empty_scope();
   table_t asked("alice"_n, 1);
   CHECK_EQUAL( asked.available_primary_key(), 0u )
   add_rows(asked, {{largest, 1}});
   CHECK_EQUAL( asked.get(largest).sec, 1u )
   CHECK_ASSERT( "next primary key in table is at autoincrement limit", ([&]() { asked.available_primary_key(); }) )

   empty_scope();
   table_t near("alice"_n, 1);
   add_rows(near, {{last_handed_out - 1, 1}});
   CHECK_EQUAL( near.available_primary_key(), last_handed_out )
   add_rows(near, {{last_handed_out, 1}});
   CHECK_ASSERT( "next primary key in table is at autoincrement limit", ([&]() { near.available_primary_key(); }) )
SYSIO_TEST_END

// A copy reads the row its source does, from the same cached object.
SYSIO_TEST_BEGIN(secondary_copy_shares_the_cached_row)
   empty_scope();
   indexed_t t("alice"_n, 1);
   add_rows(t, {{1, 10}, {2, 20}});
   auto idx = t.get_index<"bysec"_n>();

   auto it = idx.begin();
   auto copy = it;
   CHECK_EQUAL( &*copy == &*it, true )
   auto moved = std::move(copy);
   CHECK_EQUAL( moved->id, 1u )
   CHECK_EQUAL( copy == idx.end(), true )
SYSIO_TEST_END

SYSIO_TEST_BEGIN(secondary_scan_stays_in_scope_on_two_calls_a_row)
   arrange("alice"_n.value, 1, 0, false);
   host().rows.clear();
   indexed_t first("alice"_n, 1);
   indexed_t second("alice"_n, 2);
   for (uint64_t id : {uint64_t{1}, uint64_t{2}}) {
      first.emplace("alice"_n, [&](auto& r) { r.id = id; r.sec = id; });
      second.emplace("alice"_n, [&](auto& r) { r.id = id; r.sec = id; });
   }
   indexed_t reader("alice"_n, 1);   // a cold cache, so every row costs its kv_get
   auto idx = reader.get_index<"bysec"_n>();

   auto it = idx.begin();
   CHECK_EQUAL( it->id, 1u )
   host().clear_counts();
   ++it;
   CHECK_EQUAL( it->id, 2u )
   CHECK_EQUAL( host().calls("kv_idx_next"), 1u )
   CHECK_EQUAL( host().calls("kv_get"), 1u )
   CHECK_EQUAL( host().total(), 2u )

   ++it;
   CHECK_EQUAL( it == idx.end(), true )     // the next entry belongs to scope 2

   auto last = idx.end();
   --last;
   CHECK_EQUAL( last->id, 2u )              // the last entry of THIS scope
   auto found = idx.find(uint64_t{2});
   CHECK_EQUAL( found->id, 2u )
   auto bound = idx.lower_bound(uint64_t{9});
   CHECK_EQUAL( bound == idx.end(), true )
SYSIO_TEST_END

// A copy of a secondary iterator re-finds its own position among rows sharing a secondary key.
SYSIO_TEST_BEGIN(secondary_copy_finds_its_duplicate)
   arrange("alice"_n.value, 1, 0, false);
   host().rows.clear();
   indexed_t t("alice"_n, 1);
   for (uint64_t id : {uint64_t{1}, uint64_t{2}, uint64_t{3}})
      t.emplace("alice"_n, [&](auto& r) { r.id = id; r.sec = 5; });
   auto idx = t.get_index<"bysec"_n>();

   auto it = idx.begin();
   ++it;
   auto copy = it;
   ++copy;
   CHECK_EQUAL( it->id, 2u )
   CHECK_EQUAL( copy->id, 3u )
SYSIO_TEST_END

// Copying an iterator that walked off the end keeps it at the end, so -- reaches the last row.
SYSIO_TEST_BEGIN(copy_of_a_walked_off_iterator_stays_at_the_end)
   arrange("alice"_n.value, "alice"_n.value, 1, false);
   host().rows.clear();
   table_t t("alice"_n, "alice"_n.value);
   t.emplace("alice"_n, [](auto& r) { r.id = 1; r.sec = 7; });

   auto it = t.begin();
   ++it;
   CHECK_EQUAL( it == t.end(), true )
   auto copy = it;
   --copy;
   CHECK_EQUAL( copy->id, 1u )
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   SYSIO_TEST(duplicate_primary_key_rejected)
   SYSIO_TEST(foreign_code_handle_cannot_mutate)
   SYSIO_TEST(own_table_handle_passes_the_guard)
   SYSIO_TEST(own_table_handle_can_modify_and_erase)
   SYSIO_TEST(primary_bounds_accept_uint64_and_name)
   SYSIO_TEST(find_hit_costs_four_host_calls)
   SYSIO_TEST(primary_scan_reads_key_in_the_step)
   SYSIO_TEST(emplace_makes_one_write_and_no_probe)
   SYSIO_TEST(end_decrement_reaches_the_largest_primary_key)
   SYSIO_TEST(available_primary_key_paths)
   SYSIO_TEST(available_primary_key_is_three_host_calls)
   SYSIO_TEST(available_primary_key_at_the_limit_caches_nothing)
   SYSIO_TEST(reverse_iteration_visits_every_row)
   SYSIO_TEST(reverse_scan_is_one_host_call_a_row_when_cached)
   SYSIO_TEST(secondary_reverse_reference_is_the_cached_row)
   SYSIO_TEST(reverse_step_off_the_first_row_reaches_rend)
   SYSIO_TEST(reverse_iterator_edges_abort)
   SYSIO_TEST(reverse_iteration_of_an_empty_table)
   SYSIO_TEST(reverse_iterator_base_is_one_row_past)
   SYSIO_TEST(std_reverse_iterator_compares_against_rend)
   SYSIO_TEST(reverse_iterator_rebuilds_from_the_erase_result)
   SYSIO_TEST(reverse_loop_with_no_step_aborts)
   SYSIO_TEST(reverse_iterator_on_an_erased_row)
   SYSIO_TEST(reverse_iterator_does_not_see_a_later_row)
   SYSIO_TEST(reverse_decrement_of_an_empty_table_aborts)
   SYSIO_TEST(default_iterator_has_no_table_to_step_back_in)
   SYSIO_TEST(end_decrement_reaches_the_largest_secondary_key)
   SYSIO_TEST(secondary_get_refers_to_the_cached_row)
   SYSIO_TEST(modify_keeps_the_cached_object)
   SYSIO_TEST(mutators_refuse_a_copy_of_a_row)
   SYSIO_TEST(modify_of_a_gone_row_aborts)
   SYSIO_TEST(modify_with_same_payer_keeps_the_row_payer)
   SYSIO_TEST(erase_ends_the_iterators_on_the_row)
   SYSIO_TEST(erased_row_is_gone_from_the_handle)
   SYSIO_TEST(write_after_an_erase_reuses_the_cached_row)
   SYSIO_TEST(secondary_step_after_a_key_move_starts_from_the_old_key)
   SYSIO_TEST(secondary_upper_bound_seeks_past_duplicates)
   SYSIO_TEST(secondary_upper_bound_at_the_top_of_the_scope)
   SYSIO_TEST(secondary_copy_shares_the_cached_row)
   SYSIO_TEST(secondary_scan_stays_in_scope_on_two_calls_a_row)
   SYSIO_TEST(erase_by_iterator_opens_no_second_handle)
   SYSIO_TEST(secondary_copy_of_a_moved_entry_aborts)
   SYSIO_TEST(end_decrement_of_an_empty_table_aborts)
   SYSIO_TEST(primary_key_limit_follows_upstream)
   SYSIO_TEST(secondary_copy_finds_its_duplicate)
   SYSIO_TEST(copy_of_a_walked_off_iterator_stays_at_the_end)
   return has_failed();
}
