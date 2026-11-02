/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Native unit tests for be_key_reader in kv_table.hpp.
 *
 *  Background: add_native_contract() formerly defined uint128_t and int128_t
 *  as preprocessor macros expanding to unsigned __int128 and __int128.
 *  Functional casts such as uint128_t(x) became "unsigned __int128(x)",
 *  which is invalid C++. be_key_reader used two such casts, so native module
 *  consumers (sysio.system, sysio.bios, etc.) failed to build. Those casts
 *  were changed to static_cast<uint128_t>(x); native modules now use typedefs.
 *
 *  These tests use add_native_executable (cdt-cpp -fnative), which provides
 *  the sysroot typedefs, and check key decoding behavior. Coverage of
 *  add_native_contract's host compiler flags lives in tests/native_contract_types.
 *
 *  The cases after the key decoder pin what kv::table, kv::scoped_table and kv::index cost in host
 *  calls, not only what they return. They run against kv_mock.hpp, which follows the chain's rules
 *  and counts every call. A wrapper that still returns the right row while paying five calls for a
 *  point lookup fails here, and a passing integration suite would not catch it.
 *
 *  The last group pins sizes at the edge of each buffer the wrappers hold: be_key_stream's 256 bytes,
 *  key_buf's 64, ser_buf's 256, and the seek past every key.
 */

#include <sysio/tester.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/kv_scoped_table.hpp>
#include <sysio/kv_global.hpp>
#include <sysio/fixed_bytes.hpp>

#include "kv_mock.hpp"

#include <array>
#include <cstring>
#include <iterator>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

using namespace sysio;
using sysio::kv::be_key_reader;
using sysio::kv::be_key_stream;
using kv_mock::host;

// The generated dispatcher records the receiver here at the top of apply(); the native dispatch
// does not. Driving it directly exercises both branches of kv::detail::receiving_account().
extern "C" void sysio_set_contract_name(uint64_t n);

namespace {

constexpr auto owner = "alice"_n;
constexpr auto payer = "bob"_n;

struct row_key {
   uint64_t id;
   SYSLIB_SERIALIZE(row_key, (id))
};

struct row_val {
   uint64_t balance;
   name     holder;
   SYSLIB_SERIALIZE(row_val, (balance)(holder))
};

/// Variable-size, so a value can overflow kv_value_stack_size and take the heap path.
struct text_val {
   std::string text;
   SYSLIB_SERIALIZE(text_val, (text))
};

/// A key whose table can allocate the next primary key.
struct auto_key {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(auto_key, (id))
};

/// A key past key_buf's inline storage, so a step has to read it again at full size.
struct long_key {
   std::string name;
   SYSLIB_SERIALIZE(long_key, (name))
};

/// Counts its copies, so a case can pin that a wrapper moves a value rather than copying it.
struct copy_counted {
   uint64_t value = 0;
   static inline uint32_t copies = 0;

   copy_counted() = default;
   copy_counted(const copy_counted& o) : value(o.value) { ++copies; }
   copy_counted(copy_counted&& o) noexcept : value(o.value) {}
   copy_counted& operator=(const copy_counted& o) { value = o.value; ++copies; return *this; }
   copy_counted& operator=(copy_counted&& o) noexcept { value = o.value; return *this; }

   SYSLIB_SERIALIZE(copy_counted, (value))
};

using plain_table   = kv::table<"plain"_n, row_key, row_val>;
using long_table    = kv::table<"longkey"_n, long_key, text_val>;
using counted_table = kv::table<"counted"_n, row_key, copy_counted>;
using config_global = kv::global<"config"_n, row_val>;
using auto_table    = kv::table<"autokey"_n, auto_key, row_val>;
using text_table    = kv::table<"text"_n, row_key, text_val>;
using scoped_table  = kv::scoped_table<"scoped"_n, row_key, row_val>;
using indexed_table = kv::table<"indexed"_n, row_key, row_val,
                                kv::index<"bybal"_n, kv::member_data<row_val, uint64_t, &row_val::balance>>>;
using scoped_indexed_table = kv::scoped_table<"scopedidx"_n, row_key, row_val,
                                kv::index<"bybal"_n, kv::member_data<row_val, uint64_t, &row_val::balance>>>;
template<uint64_t Number>
struct balance_of {
   using result_type = uint64_t;
   uint64_t operator()(const row_val& row) const { return row.balance + Number; }
};

using sixteen_table = kv::table<"sixteen"_n, row_key, row_val,
   kv::index<"ia"_n, balance_of<0>>,  kv::index<"ib"_n, balance_of<1>>,   kv::index<"ic"_n, balance_of<2>>,
   kv::index<"id"_n, balance_of<3>>,  kv::index<"ie"_n, balance_of<4>>,   kv::index<"if"_n, balance_of<5>>,
   kv::index<"ig"_n, balance_of<6>>,  kv::index<"ih"_n, balance_of<7>>,   kv::index<"ii"_n, balance_of<8>>,
   kv::index<"ij"_n, balance_of<9>>,  kv::index<"ik"_n, balance_of<10>>,  kv::index<"il"_n, balance_of<11>>,
   kv::index<"im"_n, balance_of<12>>, kv::index<"in"_n, balance_of<13>>,  kv::index<"io"_n, balance_of<14>>,
   kv::index<"ip"_n, balance_of<15>>>;

struct hash_key {
   checksum256 hash;
   SYSLIB_SERIALIZE(hash_key, (hash))
};

struct hash_val {
   checksum256 hash;
   uint64_t    amount;
   SYSLIB_SERIALIZE(hash_val, (hash)(amount))
};

using hashed_table = kv::table<"hashed"_n, hash_key, hash_val,
                               kv::index<"byhash"_n, kv::member_data<hash_val, checksum256, &hash_val::hash>>>;

// The secondary iterator reads as the value, as operator* does.
using balance_iterator = indexed_table::secondary_index_view<"bybal"_n>::const_iterator;
static_assert(std::is_same_v<std::iterator_traits<balance_iterator>::value_type, row_val>);

using long_index_table = kv::table<"longidx"_n, row_key, text_val,
                                kv::index<"bytext"_n, kv::member_data<text_val, std::string, &text_val::text>>>;

// The values a contract compiles against. They cross the host boundary, so changing one is a
// protocol change, not a refactor.
static_assert(kv_status_ok == 0 && kv_status_end == 1 && kv_status_no_row == 2 && kv_status_exists == 3);
static_assert(kv_set_upsert == 0 && kv_set_insert == 1 && kv_set_update == 2);
static_assert(sysio::kv::kv_key_max_bytes == 512);

/// Is `t.erase(A)` well-formed?
template<typename Table, typename A, typename = void>
struct erasable_with : std::false_type {};
template<typename Table, typename A>
struct erasable_with<Table, A, std::void_t<decltype(std::declval<Table&>().erase(std::declval<A>()))>>
   : std::true_type {};

// `*rit` is the value here and not the key, so multi_index's `t.erase(*rit)` does not compile on a
// table whose value is not its key. docs/kv-table.md says so.
static_assert(erasable_with<plain_table, const row_key&>::value);
static_assert(!erasable_with<plain_table, const row_val&>::value);

/// Reset the mock, make the dispatcher global name \p who, and seed nothing.
void begin_case(name who = owner) {
   host().reset(who.value);
   kv_mock::install();
   sysio_set_contract_name(who.value);
}

/// Insert ids through the wrapper, each with balance id * 10, then clear the call counters.
template<typename Table>
void fill(Table& t, std::initializer_list<uint64_t> ids) {
   for (uint64_t id : ids) t.emplace(payer, row_key{id}, row_val{id * 10, payer});
   host().clear_counts();
}

/// Overwrite the stack below the caller, so a reference into a dead frame reads something else.
[[gnu::noinline]] void clobber_stack() {
   volatile char junk[4096];
   for (auto& byte : junk) byte = 0x5A;
}

/// The size of one escaped field as a key encodes it, aborting as the key would.
template<typename Field>
uint32_t encoded_size(const Field& field) {
   be_key_stream stream;
   stream << field;
   return stream.size();
}

/// \p nuls NUL bytes, then \p plain other bytes. Each NUL encodes to two bytes and the rest to one.
std::string nul_heavy(size_t nuls, size_t plain) { return std::string(nuls, '\0') + std::string(plain, 'a'); }

std::vector<char> as_bytes(const std::string& text) { return std::vector<char>(text.begin(), text.end()); }

/// A string that packs to exactly \p size bytes, length prefix included, past 127 bytes of text.
text_val packing_to(uint32_t size) { return text_val{std::string(size - 2, 'v')}; }

/// \p value as the chain stores it.
std::string stored_bytes(const row_val& value) {
   return std::string(reinterpret_cast<const char*>(&value), sizeof(row_val));
}

/// A row_val of all zero bytes as the chain stores it.
std::string stored_row_val() { return stored_bytes(row_val{}); }

} // namespace

// Round-trip a known 128-bit big-endian value through be_key_reader.
SYSIO_TEST_BEGIN(be_key_reader_uint128_roundtrip)
   // [hi:8B BE][lo:8B BE] = 0x0123456789ABCDEF_FEDCBA9876543210
   const unsigned char buf[16] = {
      0x01, 0x23, 0x45, 0x67, 0x89, 0xAB, 0xCD, 0xEF,
      0xFE, 0xDC, 0xBA, 0x98, 0x76, 0x54, 0x32, 0x10
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   uint128_t v = 0;
   r >> v;
   const uint128_t expected =
      (static_cast<uint128_t>(0x0123456789ABCDEFULL) << 64)
      | static_cast<uint128_t>(0xFEDCBA9876543210ULL);
   CHECK_EQUAL(v, expected)
SYSIO_TEST_END

// int128_t is encoded with its high bit flipped so BE-lex ordering matches
// numeric ordering. Decoding 0x80 00 … 00 must yield 0.
SYSIO_TEST_BEGIN(be_key_reader_int128_zero)
   const unsigned char buf[16] = {
      0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
      0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 1; // non-zero so a no-op read would fail the check
   r >> v;
   CHECK_EQUAL(v, static_cast<int128_t>(0))
SYSIO_TEST_END

// Decoding 0x7F FF … FF (-1 with sign-bit flip) must yield -1.
SYSIO_TEST_BEGIN(be_key_reader_int128_negative_one)
   const unsigned char buf[16] = {
      0x7F, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 0;
   r >> v;
   CHECK_EQUAL(v, static_cast<int128_t>(-1))
SYSIO_TEST_END

// Decoding 0xFF FF … FF (max int128 with sign-bit flip) must yield INT128_MAX.
SYSIO_TEST_BEGIN(be_key_reader_int128_max)
   const unsigned char buf[16] = {
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
      0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF
   };
   be_key_reader r(reinterpret_cast<const char*>(buf), sizeof(buf));
   int128_t v = 0;
   r >> v;
   const int128_t expected =
      static_cast<int128_t>((static_cast<uint128_t>(~0ULL) << 64)
                            | static_cast<uint128_t>(~0ULL))
      ^ (static_cast<int128_t>(1) << 127);
   CHECK_EQUAL(v, expected)
SYSIO_TEST_END

// A hit is one kv_get: no contains probe, no handle, nothing left to destroy.
SYSIO_TEST_BEGIN(find_hit_is_one_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});
   {
      auto it = t.find({2});
      CHECK_EQUAL(host().calls("kv_get"), 1u)
      CHECK_EQUAL(host().total(), 1u)
      CHECK_EQUAL(it != t.end(), true)
      CHECK_EQUAL(it.key().id, 2u)
      CHECK_EQUAL(it->balance, 20u)
      CHECK_EQUAL(it->holder.value, payer.value)
      CHECK_EQUAL(host().total(), 1u)   // reading the iterator stays in the contract
   }
   CHECK_EQUAL(host().total(), 1u)      // and no handle to destroy on the way out
   CHECK_EQUAL(host().receiver_calls, 0u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(find_miss_is_one_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   auto it = t.find({9});
   CHECK_EQUAL(it == t.end(), true)
   CHECK_EQUAL(host().total(), 1u)
SYSIO_TEST_END

// A table built without an account resolves the receiver from the dispatcher's global, so its lookup
// costs no current_receiver. Without the global the wrapper falls back to the intrinsic.
SYSIO_TEST_BEGIN(default_code_uses_dispatcher_global)
   begin_case();
   plain_table t;
   fill(t, {1});
   CHECK_EQUAL(t.find({1}) != t.end(), true)
   CHECK_EQUAL(host().receiver_calls, 0u)
   CHECK_EQUAL(t.get_code().value, owner.value)

   sysio_set_contract_name(0);
   CHECK_EQUAL(t.find({1}) != t.end(), true)
   CHECK_EQUAL(host().receiver_calls, 1u)
SYSIO_TEST_END

// A value past the stack buffer needs a second read at its real size.
SYSIO_TEST_BEGIN(find_large_value_rereads_once)
   begin_case();
   text_table t(owner);
   const std::string big(sysio::kv::kv_value_stack_size * 2, 'x');
   t.emplace(payer, row_key{1}, text_val{big});
   host().clear_counts();

   auto it = t.find({1});
   CHECK_EQUAL(it->text == big, true)
   CHECK_EQUAL(host().calls("kv_get"), 2u)
   CHECK_EQUAL(host().total(), 2u)
SYSIO_TEST_END

// The target that motivates the lazy erase: find, then erase with the result ignored. An lvalue with
// no handle has none to hand on, and erasing it must not open one.
SYSIO_TEST_BEGIN(find_then_erase_is_two_host_calls)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});
   {
      auto it = t.find({2});
      t.erase(it);
      CHECK_EQUAL(host().calls("kv_get"), 1u)
      CHECK_EQUAL(host().calls("kv_erase"), 1u)
      CHECK_EQUAL(host().total(), 2u)
   }
   CHECK_EQUAL(host().total(), 2u)
   CHECK_EQUAL(t.contains({2}), false)
   CHECK_EQUAL(host().open_handles(), 0u)
SYSIO_TEST_END

// With a secondary index the erase still reaches every entry, but nothing else is added.
SYSIO_TEST_BEGIN(find_then_erase_with_index_adds_only_the_entry)
   begin_case();
   indexed_table t(owner);
   fill(t, {1, 2});
   auto it = t.find({2});
   t.erase(it);
   CHECK_EQUAL(host().calls("kv_get"), 1u)
   CHECK_EQUAL(host().calls("kv_idx_erase"), 1u)
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 3u)
   CHECK_EQUAL(host().entries.size(), 1u)
SYSIO_TEST_END

// The result of erase, once used, is the next row, and the end when the last row went.
SYSIO_TEST_BEGIN(erase_result_is_the_next_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto next = t.erase(t.find({2}));
   CHECK_EQUAL(next != t.end(), true)
   CHECK_EQUAL(next.key().id, 3u)
   CHECK_EQUAL(next->balance, 30u)

   auto past_last = t.erase(t.find({3}));
   CHECK_EQUAL(past_last == t.end(), true)
SYSIO_TEST_END

// A dereference, a key read and a comparison each settle the owed step, on a const iterator too.
SYSIO_TEST_BEGIN(erase_result_settles_on_every_read)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   const auto by_deref = t.erase(t.find({1}));
   CHECK_EQUAL((*by_deref).balance, 20u)
   const auto by_arrow = t.erase(t.find({2}));
   CHECK_EQUAL(by_arrow->balance, 30u)
   t.emplace(payer, row_key{4}, row_val{40, payer});
   const auto by_key = t.erase(t.find({3}));
   CHECK_EQUAL(by_key.key().id, 4u)
SYSIO_TEST_END

// erase() reuses a handle it is moved, so the step is one kv_it_next and no create or seek.
SYSIO_TEST_BEGIN(erase_of_a_moved_handle_steps_it)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto it = t.begin();
   host().clear_counts();
   it = t.erase(std::move(it));
   CHECK_EQUAL(host().total(), 1u)        // kv_erase, step still owed
   CHECK_EQUAL(it.key().id, 2u)
   CHECK_EQUAL(host().calls("kv_it_next"), 1u)
   CHECK_EQUAL(host().calls("kv_it_create"), 0u)
   CHECK_EQUAL(host().calls("kv_it_lower_bound"), 0u)
SYSIO_TEST_END

// An lvalue hands its handle to the result, so `it = t.erase(it)` is the same step and no create.
SYSIO_TEST_BEGIN(erase_of_an_lvalue_hands_its_handle_on)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto it = t.begin();
   host().clear_counts();
   it = t.erase(it);
   CHECK_EQUAL(host().total(), 1u)        // kv_erase, step still owed
   CHECK_EQUAL(it.key().id, 2u)
   CHECK_EQUAL(host().calls("kv_it_next"), 1u)
   CHECK_EQUAL(host().calls("kv_it_create"), 0u)
   CHECK_EQUAL(host().calls("kv_it_destroy"), 0u)
SYSIO_TEST_END

// The loop the contracts write: an erase and a step, two calls a row, on the one handle.
SYSIO_TEST_BEGIN(erase_loop_is_two_host_calls_a_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});

   auto it = t.begin();
   host().clear_counts();
   while (it != t.end()) it = t.erase(it);
   CHECK_EQUAL(host().rows.size(), 0u)
   CHECK_EQUAL(host().calls("kv_erase"), 4u)
   CHECK_EQUAL(host().calls("kv_it_next"), 4u)
   CHECK_EQUAL(host().total(), 8u)
SYSIO_TEST_END

// The lvalue gives up only its handle. It stays on the erased row, and steps from there by key.
SYSIO_TEST_BEGIN(erased_lvalue_stays_on_its_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto it = t.begin();
   auto next = t.erase(it);
   CHECK_EQUAL(it.key().id, 1u)
   CHECK_EQUAL(next.key().id, 2u)
   ++it;
   CHECK_EQUAL(it.key().id, 2u)

   // find opens no handle, so there is none to hand on and the result opens one by key. A const
   // lvalue that does hold one cannot give it up, with the same outcome.
   const auto held = t.find({2});
   auto after = t.erase(held);
   CHECK_EQUAL(after.key().id, 3u)
   CHECK_EQUAL(held.key().id, 2u)
SYSIO_TEST_END

// An lvalue that still owes a step keeps owing it, rather than staying on a row. It takes that step
// by key at first use, so it lands past the row the second erase took out as well.
SYSIO_TEST_BEGIN(erased_lvalue_keeps_an_owed_step)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});

   auto first = t.erase(t.find({1}));
   auto second = t.erase(first);
   CHECK_EQUAL(second.key().id, 3u)
   CHECK_EQUAL(first.key().id, 3u)
SYSIO_TEST_END

// An erase result moves both ways from wherever its owed step lands.
SYSIO_TEST_BEGIN(erase_result_steps_both_ways)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4, 5, 6});

   auto forward = t.erase(t.find({2}));
   ++forward;
   CHECK_EQUAL(forward.key().id, 4u)

   auto backward = t.erase(t.find({4}));
   --backward;
   CHECK_EQUAL(backward.key().id, 3u)

   // The same with the handle the result took over.
   auto it = t.lower_bound({3});
   auto with_handle = t.erase(std::move(it));
   --with_handle;
   CHECK_EQUAL(with_handle.key().id, 1u)
SYSIO_TEST_END

// A copy taken before the step is owed one of its own, and each settles apart from the other.
SYSIO_TEST_BEGIN(copy_of_an_erase_result_owes_its_own_step)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});

   auto owed = t.erase(t.find({2}));
   auto copy = owed;
   CHECK_EQUAL(copy.key().id, 3u)
   ++copy;
   CHECK_EQUAL(copy.key().id, 4u)
   --owed;
   CHECK_EQUAL(owed.key().id, 1u)
SYSIO_TEST_END

// As with every write, erase lands on the receiver whatever account the handle names. A table with
// no index never reads that account. With an index the read goes there first, and misses.
SYSIO_TEST_BEGIN(erase_by_key_on_a_foreign_handle)
   begin_case();
   plain_table plain(owner);
   fill(plain, {1});
   plain_table foreign("carol"_n);
   foreign.erase(row_key{1});
   CHECK_EQUAL(plain.contains({1}), false)

   indexed_table indexed(owner);
   fill(indexed, {1});
   indexed_table foreign_indexed("carol"_n);
   CHECK_ASSERT("key not found", ([&]() { foreign_indexed.erase(row_key{1}); }))
   CHECK_EQUAL(indexed.contains({1}), true)
SYSIO_TEST_END

// Both shapes of the erase loop visit every row, and neither leaks a handle.
SYSIO_TEST_BEGIN(erase_loops_clear_the_table)
   begin_case();
   {
      plain_table t(owner);
      fill(t, {1, 2, 3, 4});
      for (auto it = t.begin(); it != t.end();) it = t.erase(it);
      CHECK_EQUAL(host().rows.size(), 0u)

      fill(t, {5, 6, 7, 8});
      for (auto it = t.begin(); it != t.end();) it = t.erase(std::move(it));
      CHECK_EQUAL(host().rows.size(), 0u)
   }
   CHECK_EQUAL(host().open_handles(), 0u)
SYSIO_TEST_END

// The owed step is strictly past the erased key. Re-inserting that key before the iterator is used
// must not land it back on the row, or an erase-and-rewrite loop would never end.
SYSIO_TEST_BEGIN(erase_result_skips_a_key_rewritten_before_use)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto next = t.erase(t.find({2}));
   t.emplace(payer, row_key{2}, row_val{99, payer});
   CHECK_EQUAL(next.key().id, 3u)

   // The same with a handle: the host's step is strictly past the key as well.
   auto it = t.begin();
   ++it;
   auto stepped = t.erase(std::move(it));
   t.emplace(payer, row_key{2}, row_val{99, payer});
   CHECK_EQUAL(stepped.key().id, 3u)
SYSIO_TEST_END

// The result resolves at first use, so it sees a row written after the erase. Documented on erase();
// pinned because it differs from stepping before the erase, as this wrapper used to.
SYSIO_TEST_BEGIN(erase_result_resolves_at_first_use)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 5});

   auto next = t.erase(t.find({1}));
   t.emplace(payer, row_key{3}, row_val{30, payer});
   CHECK_EQUAL(next.key().id, 3u)
SYSIO_TEST_END

// A found iterator opens its handle by key on the first step, in both directions.
SYSIO_TEST_BEGIN(found_iterator_opens_a_handle_to_step)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto forward = t.find({2});
   CHECK_EQUAL(host().calls("kv_it_create"), 0u)
   ++forward;
   CHECK_EQUAL(host().calls("kv_it_create"), 1u)
   CHECK_EQUAL(forward.key().id, 3u)
   ++forward;
   CHECK_EQUAL(forward == t.end(), true)
   CHECK_EQUAL(host().calls("kv_it_create"), 1u)   // the handle stays, and the second step reuses it

   auto backward = t.find({2});
   --backward;
   CHECK_EQUAL(backward.key().id, 1u)
   --backward;
   CHECK_EQUAL(backward == t.end(), true)        // past the first row, as a handle-backed iterator
SYSIO_TEST_END

// Stepping a found iterator whose row is gone must not skip the row after it. Seeking the key and
// then stepping would, because the seek already lands on that row.
SYSIO_TEST_BEGIN(found_iterator_steps_past_its_erased_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto forward = t.find({2});
   t.erase(row_key{2});
   ++forward;
   CHECK_EQUAL(forward != t.end(), true)
   CHECK_EQUAL(forward.key().id, 3u)

   t.emplace(payer, row_key{2}, row_val{20, payer});
   auto backward = t.find({2});
   t.erase(row_key{2});
   --backward;
   CHECK_EQUAL(backward != t.end(), true)
   CHECK_EQUAL(backward.key().id, 1u)
SYSIO_TEST_END

// With the erased row last in the table the seek lands on end, and prev still reaches the row before.
SYSIO_TEST_BEGIN(found_iterator_steps_back_from_erased_last_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});

   auto it = t.find({2});
   t.erase(row_key{2});
   --it;
   CHECK_EQUAL(it != t.end(), true)
   CHECK_EQUAL(it.key().id, 1u)
SYSIO_TEST_END

// A copy holds the position, makes no host call, and moves independently of its source.
SYSIO_TEST_BEGIN(copy_makes_no_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto source = t.begin();
   host().clear_counts();
   auto copy = source;
   plain_table::const_iterator assigned;
   assigned = source;
   CHECK_EQUAL(host().total(), 0u)
   CHECK_EQUAL(copy == source, true)
   CHECK_EQUAL(assigned == source, true)

   ++copy;
   CHECK_EQUAL(copy.key().id, 2u)
   CHECK_EQUAL(source.key().id, 1u)

   // Assigning over an iterator with a handle lets that handle go.
   assigned = copy;
   ++assigned;
   CHECK_EQUAL(assigned.key().id, 3u)
SYSIO_TEST_END

// An end iterator stays end through a copy, and steps back to the last row.
SYSIO_TEST_BEGIN(end_iterator_steps_back_to_the_last_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});

   auto it = t.end();
   auto copy = it;
   --it;
   CHECK_EQUAL(it.key().id, 2u)
   --copy;
   CHECK_EQUAL(copy.key().id, 2u)

   // Walked off the end with a live handle: same answer through the handle.
   auto walked = t.find({2});
   ++walked;
   CHECK_EQUAL(walked == t.end(), true)
   --walked;
   CHECK_EQUAL(walked.key().id, 2u)
SYSIO_TEST_END

static_assert(std::is_same_v<std::iterator_traits<plain_table::const_reverse_iterator>::iterator_category,
                             std::bidirectional_iterator_tag>);
static_assert(std::is_same_v<decltype(*std::declval<plain_table::const_reverse_iterator&>()), const row_val&>);

SYSIO_TEST_BEGIN(reverse_iteration_visits_every_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   std::vector<uint64_t> seen;
   for (auto rit = t.rbegin(); rit != t.rend(); ++rit) {
      CHECK_EQUAL(rit->balance, (*rit).balance)
      CHECK_EQUAL(rit.key().id * 10, rit->balance)
      seen.push_back(rit->balance);
   }
   CHECK_EQUAL(seen.size(), 3u)
   CHECK_EQUAL(seen[0], 30u)
   CHECK_EQUAL(seen[1], 20u)
   CHECK_EQUAL(seen[2], 10u)
   CHECK_EQUAL(std::distance(t.rbegin(), t.rend()), 3)
   CHECK_EQUAL(std::next(t.rbegin())->balance, 20u)
SYSIO_TEST_END

// What a contract writes: a reference from *rit, held across other work. It refers to the row inside
// the iterator, which lives as long as the iterator does. std::reverse_iterator handed back one into
// a temporary that was already gone.
SYSIO_TEST_BEGIN(reverse_iterator_reference_outlives_the_read)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto rit = t.rbegin();
   const row_val& last = *rit;
   const row_val* through_arrow = rit.operator->();
   clobber_stack();
   CHECK_EQUAL(last.balance, 30u)
   CHECK_EQUAL(through_arrow->balance, 30u)
   CHECK_EQUAL(&last == through_arrow, true)
SYSIO_TEST_END

// One row, one host call, as a forward scan pays. Opening it is the two calls of --end().
SYSIO_TEST_BEGIN(reverse_scan_is_one_host_call_a_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});
   {
      auto rit = t.rbegin();
      CHECK_EQUAL(host().calls("kv_it_create"), 1u)
      CHECK_EQUAL(host().calls("kv_it_prev"), 1u)
      CHECK_EQUAL(host().total(), 2u)

      host().clear_counts();
      uint32_t rows = 0;
      for (; rit != t.rend(); ++rit) ++rows;
      CHECK_EQUAL(rows, 4u)
      CHECK_EQUAL(host().calls("kv_it_prev"), 4u)   // three rows, then the step off the first
      CHECK_EQUAL(host().total(), 4u)
   }
   CHECK_EQUAL(host().open_handles(), 0u)
SYSIO_TEST_END

// Reading the row and comparing with rend() stay in the contract.
SYSIO_TEST_BEGIN(reverse_reads_make_no_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1});

   auto rit = t.rbegin();
   host().clear_counts();
   auto rend = t.rend();
   CHECK_EQUAL(rit != rend, true)
   CHECK_EQUAL(rit->balance, 10u)
   CHECK_EQUAL((*rit).balance, 10u)
   CHECK_EQUAL(rit.key().id, 1u)
   CHECK_EQUAL(host().total(), 0u)
SYSIO_TEST_END

// base() is one row past the position, as on std::reverse_iterator.
SYSIO_TEST_BEGIN(reverse_iterator_base_is_one_row_past)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   CHECK_EQUAL(t.rbegin().base() == t.end(), true)
   CHECK_EQUAL(t.rend().base() == t.begin(), true)

   auto rit = t.rbegin();
   ++rit;
   CHECK_EQUAL(rit.base().key().id, 3u)
   auto position = rit.base();
   --position;
   CHECK_EQUAL(position.key().id, 2u)

   // The idiom for erasing the row under a reverse iterator.
   auto next = t.erase(std::next(t.rbegin()).base());
   CHECK_EQUAL(next == t.end(), true)
   CHECK_EQUAL(t.contains({3}), false)
SYSIO_TEST_END

// Built from a forward iterator it reads the row before, like the std adaptor.
SYSIO_TEST_BEGIN(reverse_iterator_from_a_forward_iterator)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   plain_table::const_reverse_iterator from_found(t.find({3}));
   CHECK_EQUAL(from_found.key().id, 2u)
   plain_table::const_reverse_iterator from_first(t.begin());
   CHECK_EQUAL(from_first == t.rend(), true)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_iterator_steps_both_ways)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto rit = t.rbegin();
   ++rit;
   CHECK_EQUAL(rit.key().id, 2u)
   --rit;
   CHECK_EQUAL(rit.key().id, 3u)

   // A copy moves on its own, and the postfix forms hand back the old position.
   auto copy = rit;
   ++copy;
   CHECK_EQUAL(copy.key().id, 2u)
   CHECK_EQUAL(rit.key().id, 3u)
   auto old = rit++;
   CHECK_EQUAL(old.key().id, 3u)
   CHECK_EQUAL(rit.key().id, 2u)
   old = rit--;
   CHECK_EQUAL(old.key().id, 2u)
   CHECK_EQUAL(rit.key().id, 3u)

   // From rend, back onto the first row.
   auto back = t.rend();
   --back;
   CHECK_EQUAL(back.key().id, 1u)
   ++back;
   CHECK_EQUAL(back == t.rend(), true)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_iterator_edges_abort)
   begin_case();
   plain_table t(owner);
   fill(t, {1});

   auto past = t.rend();
   CHECK_ASSERT("cannot increment reverse iterator at rend", ([&]() { ++past; }))
   CHECK_ASSERT("deref end iterator", ([&]() { (void)*past; }))
   auto first = t.rbegin();
   CHECK_ASSERT("cannot decrement reverse iterator at rbegin", ([&]() { --first; }))

   plain_table::const_reverse_iterator unset;
   CHECK_ASSERT("cannot position a default-constructed iterator", ([&]() { --unset; }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_iteration_of_an_empty_table)
   begin_case();
   plain_table t(owner);
   CHECK_EQUAL(t.rbegin() == t.rend(), true)
   CHECK_EQUAL(t.crbegin() == t.crend(), true)
   CHECK_EQUAL(t.rbegin().base() == t.end(), true)
   CHECK_EQUAL(host().open_handles(), 0u)
SYSIO_TEST_END

// On an empty table rbegin() is rend(), so -- has nowhere to go from either. A neighbouring scope's
// rows are not somewhere to go.
SYSIO_TEST_BEGIN(reverse_decrement_of_an_empty_table_aborts)
   begin_case();
   plain_table t(owner);
   auto first = t.rbegin();
   CHECK_ASSERT("cannot decrement reverse iterator at rbegin", ([&]() { --first; }))
   auto past = t.rend();
   CHECK_ASSERT("cannot decrement reverse iterator at rbegin", ([&]() { --past; }))

   scoped_table empty(owner, 1);
   scoped_table neighbour(owner, 2);
   fill(neighbour, {1});
   auto scoped = empty.rbegin();
   CHECK_ASSERT("cannot decrement reverse iterator at rbegin", ([&]() { --scoped; }))
SYSIO_TEST_END

// To carry on from an erase, rebuild the iterator from the result. It reads the row before with no
// step of its own.
SYSIO_TEST_BEGIN(reverse_iterator_rebuilds_from_the_erase_result)
   begin_case();
   {
      plain_table t(owner);
      fill(t, {1, 2, 3, 4, 5});
      using reverse = plain_table::const_reverse_iterator;

      // Out of the middle, with a row left on each side.
      auto rit = t.rbegin();
      ++rit;
      ++rit;
      CHECK_EQUAL(rit.key().id, 3u)
      rit = reverse(t.erase(--rit.base()));
      CHECK_EQUAL(rit.key().id, 2u)

      // The rest, from the last row down to rend.
      rit = t.rbegin();
      for (uint64_t id : {uint64_t{5}, uint64_t{4}, uint64_t{2}, uint64_t{1}}) {
         CHECK_EQUAL(rit.key().id, id)
         rit = reverse(t.erase(--rit.base()));
      }
      CHECK_EQUAL(rit == t.rend(), true)
      CHECK_EQUAL(host().rows.size(), 0u)
   }
   CHECK_EQUAL(host().open_handles(), 0u)

   // On a table with an index, and inside a scope with neighbours either side.
   {
      scoped_indexed_table below(owner, 1);
      scoped_indexed_table inside(owner, 2);
      scoped_indexed_table above(owner, 3);
      fill(below, {1});
      fill(inside, {1, 2});
      fill(above, {1});
      using reverse = scoped_indexed_table::const_reverse_iterator;

      auto rit = inside.rbegin();
      rit = reverse(inside.erase(--rit.base()));
      CHECK_EQUAL(rit.key().id, 1u)
      rit = reverse(inside.erase(--rit.base()));
      CHECK_EQUAL(rit == inside.rend(), true)
      CHECK_EQUAL(host().rows.size(), 2u)
      CHECK_EQUAL(host().entries.size(), 2u)
   }
   CHECK_EQUAL(host().open_handles(), 0u)
SYSIO_TEST_END

// Nothing moves a reverse iterator but a step. After an erase it reads its own copy of the row, so the
// loop with no step finds the key gone, and a step reaches the row before.
SYSIO_TEST_BEGIN(reverse_iterator_holds_its_row_through_an_erase)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3});

   auto rit = t.rbegin();
   t.erase(rit.key());
   CHECK_EQUAL(rit != t.rend(), true)
   CHECK_EQUAL(rit.key().id, 3u)
   CHECK_EQUAL(rit->balance, 30u)
   CHECK_ASSERT("key not found", ([&]() { while (rit != t.rend()) t.erase(rit.key()); }))
   CHECK_EQUAL(host().rows.size(), 2u)

   ++rit;
   CHECK_EQUAL(rit.key().id, 2u)

   // base() steps by key, so it finds the row past an erased row.
   t.erase(rit.key());
   t.emplace(payer, row_key{3}, row_val{30, payer});
   CHECK_EQUAL(rit.base().key().id, 3u)
SYSIO_TEST_END

// Empties a table from the end at what a forward erase loop costs: an erase and a step, on one handle.
SYSIO_TEST_BEGIN(reverse_erase_loop_is_two_host_calls_a_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});

   auto rit = t.rbegin();
   host().clear_counts();
   while (rit != t.rend()) {
      t.erase(rit.key());
      ++rit;
   }
   CHECK_EQUAL(host().rows.size(), 0u)
   CHECK_EQUAL(host().calls("kv_erase"), 4u)
   CHECK_EQUAL(host().calls("kv_it_prev"), 4u)
   CHECK_EQUAL(host().total(), 8u)
SYSIO_TEST_END

// It reads the row it holds, so a row written past the position waits for a new rbegin().
SYSIO_TEST_BEGIN(reverse_iterator_does_not_see_a_later_row)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});

   auto rit = t.rbegin();
   t.emplace(payer, row_key{3}, row_val{30, payer});
   CHECK_EQUAL(rit.key().id, 2u)
   CHECK_EQUAL(t.rbegin().key().id, 3u)
   ++rit;
   CHECK_EQUAL(rit.key().id, 1u)
SYSIO_TEST_END

// Bounded by the scope at both ends.
SYSIO_TEST_BEGIN(reverse_iteration_stays_in_scope)
   begin_case();
   scoped_table first(owner, 1);
   scoped_table second(owner, 2);
   scoped_table third(owner, 3);
   fill(first, {1});
   fill(second, {1, 2});
   fill(third, {1});

   auto rit = second.rbegin();
   CHECK_EQUAL(rit.key().id, 2u)
   ++rit;
   CHECK_EQUAL(rit.key().id, 1u)
   ++rit;
   CHECK_EQUAL(rit == second.rend(), true)
SYSIO_TEST_END

// A row past the stack buffers is read again at full size, as on a forward step.
SYSIO_TEST_BEGIN(reverse_scan_rereads_a_row_past_the_buffers)
   begin_case();
   long_table t(owner);
   const std::string big_key(sysio::kv::key_buf::inline_cap + 10, 'b');
   const std::string big_value(sysio::kv::kv_value_stack_size * 2, 'x');
   t.emplace(payer, long_key{"a"}, text_val{"small"});
   t.emplace(payer, long_key{big_key}, text_val{big_value});
   host().clear_counts();

   auto rit = t.rbegin();
   CHECK_EQUAL(rit.key().name == big_key, true)
   CHECK_EQUAL(rit->text == big_value, true)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)
   ++rit;
   CHECK_EQUAL(rit->text == "small", true)
SYSIO_TEST_END

// A default-constructed iterator has no table to open a handle against.
SYSIO_TEST_BEGIN(default_constructed_iterator_cannot_step_back)
   begin_case();
   plain_table::const_iterator it;
   CHECK_ASSERT("cannot position a default-constructed iterator", ([&]() { --it; }))
   CHECK_ASSERT("deref end iterator", ([&]() { (void)*it; }))
   CHECK_ASSERT("cannot position a default-constructed iterator",
                ([&]() { plain_table::const_reverse_iterator rit(it); }))
SYSIO_TEST_END

// The error text the wrappers raise is pinned by contract tests; the lazy paths must keep it.
SYSIO_TEST_BEGIN(end_iterator_messages_are_unchanged)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   auto missing = t.find({9});
   CHECK_ASSERT("deref end iterator", ([&]() { (void)*missing; }))
   CHECK_ASSERT("key() on end iterator", ([&]() { (void)missing.key(); }))
   CHECK_ASSERT("cannot erase end iterator", ([&]() { t.erase(missing); }))
   CHECK_ASSERT("cannot modify end iterator", ([&]() { t.modify(payer, missing, row_val{}); }))

   // Same for an iterator that only learns it is at the end when its owed step settles.
   auto past_last = t.erase(t.find({1}));
   CHECK_ASSERT("deref end iterator", ([&]() { (void)*past_last; }))
SYSIO_TEST_END

// modify through an iterator updates that iterator, so what it holds is what the table holds, and a
// later write through it moves index entries from the right key. Another iterator on the row keeps
// what it read.
SYSIO_TEST_BEGIN(modify_keeps_the_iterator_current)
   begin_case();
   indexed_table t(owner);
   fill(t, {1, 2});

   auto it = t.find({1});
   auto held = it;
   t.modify(payer, it, row_val{11, payer});
   CHECK_EQUAL(it->balance, 11u)
   t.modify(payer, it, row_val{12, payer});
   CHECK_EQUAL(it->balance, 12u)
   CHECK_EQUAL(t.get({1}).balance, 12u)
   CHECK_EQUAL(host().entries.size(), 2u)

   CHECK_EQUAL(held->balance, 10u)
   CHECK_ASSERT("KV secondary index entry not found for remove", ([&]() { t.erase(held); }))

   t.erase(it);
   CHECK_EQUAL(t.contains({1}), false)
   CHECK_EQUAL(host().entries.size(), 1u)
SYSIO_TEST_END

// modify through a found iterator needs no handle either.
SYSIO_TEST_BEGIN(find_then_modify_is_two_host_calls)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   auto it = t.find({1});
   t.modify(payer, it, row_val{7, payer});
   CHECK_EQUAL(host().total(), 2u)
   CHECK_EQUAL(t.get({1}).balance, 7u)
SYSIO_TEST_END

/// The row at \p id and the index's view of \p balance agree, and the index has no entry at \p gone.
template<typename Table>
void check_row_and_index_agree(Table& t, uint64_t id, uint64_t balance, uint64_t gone) {
   auto idx = t.template get_index<"bybal"_n>();
   CHECK_EQUAL(t.get({id}).balance, balance)
   CHECK_EQUAL(idx.find(balance)->balance, balance)
   CHECK_EQUAL(idx.find(balance).key().id, id)
   CHECK_EQUAL(idx.find(gone) == idx.end(), true)
}

// A to B to A through one secondary iterator. Without its value kept current the second write looks
// like no move, and the row and its index stay apart for good.
SYSIO_TEST_BEGIN(index_modify_there_and_back_through_one_iterator)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   auto idx = t.get_index<"bybal"_n>();
   auto it = idx.find(uint64_t{10});

   idx.modify(payer, it, row_val{20, payer});
   CHECK_EQUAL(it->balance, 20u)
   check_row_and_index_agree(t, 1, 20, 10);
   idx.modify(payer, it, row_val{10, payer});
   CHECK_EQUAL(it->balance, 10u)
   check_row_and_index_agree(t, 1, 10, 20);
   t.erase({1});
   CHECK_EQUAL(host().entries.size(), 0u)
SYSIO_TEST_END

// A copy of an iterator keeps the old value. A write through it moves the entry from what is stored.
SYSIO_TEST_BEGIN(modify_through_a_stale_copy_moves_the_entry_from_the_stored_row)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   auto it = t.find({1});
   auto held = it;

   t.modify(payer, it, row_val{11, payer});
   check_row_and_index_agree(t, 1, 11, 10);
   t.modify(payer, held, row_val{10, payer});
   CHECK_EQUAL(held->balance, 10u)
   check_row_and_index_agree(t, 1, 10, 11);
   t.modify(payer, held, row_val{12, payer});
   check_row_and_index_agree(t, 1, 12, 10);
SYSIO_TEST_END

// The read is the price of that on an indexed table: one kv_get, then the write.
SYSIO_TEST_BEGIN(modify_through_an_iterator_reads_an_indexed_row_first)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   auto it = t.find({1});
   host().clear_counts();
   t.modify(payer, it, row_val{10, payer});
   CHECK_EQUAL(host().calls("kv_get"), 1u)
   CHECK_EQUAL(host().calls("kv_set"), 1u)
   CHECK_EQUAL(host().calls("kv_idx_update"), 0u)
SYSIO_TEST_END

// A handle on another account reads there, but its modify writes to the receiver. With no row of that key at the
// receiver the write has nothing to replace, and the row the iterator read has not gone, so the text says what is true.
SYSIO_TEST_BEGIN(modify_through_a_foreign_iterator_needs_a_row_at_the_receiver)
   begin_case();
   constexpr auto foreign_code = "carol"_n;
   const row_val stored{10, payer};

   plain_table foreign(foreign_code);
   const auto plain_key = foreign.make_key(row_key{1});
   host().seed(foreign_code.value, sysio::kv::compute_table_id("plain"_n.value),
               std::string(plain_key.data(), plain_key.size()), stored_bytes(stored));
   auto plain_it = foreign.find({1});
   CHECK_ASSERT("cannot modify a row the receiver does not hold", ([&]() { foreign.modify(payer, plain_it, stored); }))

   indexed_table foreign_indexed(foreign_code);
   const auto indexed_key = foreign_indexed.make_key(row_key{1});
   host().seed(foreign_code.value, sysio::kv::compute_table_id("indexed"_n.value),
               std::string(indexed_key.data(), indexed_key.size()), stored_bytes(stored));
   auto indexed_it = foreign_indexed.find({1});
   CHECK_ASSERT("cannot modify a row the receiver does not hold",
                ([&]() { foreign_indexed.modify(payer, indexed_it, stored); }))
   CHECK_ASSERT("KV secondary index entry not found for update",
                ([&]() { foreign_indexed.modify(payer, indexed_it, row_val{20, payer}); }))
   CHECK_EQUAL(host().rows.size(), 2u)
SYSIO_TEST_END

// An iterator on a row that has gone cannot write it back, by either route.
SYSIO_TEST_BEGIN(modify_through_an_iterator_refuses_a_gone_row)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   auto idx = t.get_index<"bybal"_n>();
   auto it = t.find({1});
   auto entry = idx.find(uint64_t{10});
   t.erase({1});

   CHECK_ASSERT("cannot modify a row that no longer exists", ([&]() { t.modify(payer, it, row_val{10, payer}); }))
   CHECK_ASSERT("cannot modify a row that no longer exists", ([&]() { idx.modify(payer, entry, row_val{10, payer}); }))
   CHECK_EQUAL(host().rows.size(), 0u)
SYSIO_TEST_END

// The most indices a table can hold, each moved in one write. The stack the WASM has is an integration
// test's to show; this pins that the fold reaches every one of them.
SYSIO_TEST_BEGIN(sixteen_indices_store_move_and_erase)
   begin_case();
   sixteen_table t(owner);
   t.emplace(payer, row_key{1}, row_val{10, payer});
   CHECK_EQUAL(host().entries.size(), 16u)
   t.modify(payer, row_key{1}, [](row_val& row) { row.balance = 20; });
   CHECK_EQUAL(host().entries.size(), 16u)
   CHECK_EQUAL(host().calls("kv_idx_update"), 16u)
   auto idx = t.get_index<"ip"_n>();
   CHECK_EQUAL(idx.find(uint64_t{35}).key().id, 1u)
   t.erase({1});
   CHECK_EQUAL(host().entries.size(), 0u)
SYSIO_TEST_END

// A checksum256 is a key leaf, so anything that reads one back has to compile as well as write it.
SYSIO_TEST_BEGIN(checksum_keys_read_back)
   begin_case();
   constexpr std::array<uint8_t, 32> bytes = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
                                              17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32};
   const checksum256 hash(bytes);
   hashed_table t(owner);
   t.emplace(payer, hash_key{hash}, hash_val{hash, 7});

   CHECK_EQUAL(t.begin().key().hash == hash, true)
   auto idx = t.get_index<"byhash"_n>();
   CHECK_EQUAL(idx.key_begin()->sec_key == hash, true)
SYSIO_TEST_END

// A scoped table shares the lazy path, and a step never leaves its scope.
SYSIO_TEST_BEGIN(scoped_find_and_erase_stay_in_scope)
   begin_case();
   scoped_table first(owner, 1);
   scoped_table second(owner, 2);
   fill(first, {1, 2});
   fill(second, {1, 2});

   auto it = first.find({2});
   CHECK_EQUAL(host().total(), 1u)
   CHECK_EQUAL(it.key().id, 2u)
   ++it;
   CHECK_EQUAL(it == first.end(), true)   // next row is scope 2's

   auto next = first.erase(first.find({1}));
   CHECK_EQUAL(next.key().id, 2u)
   CHECK_EQUAL(second.contains({1}), true)

   auto back = second.find({1});
   --back;
   CHECK_EQUAL(back == second.end(), true)   // previous row is scope 1's
SYSIO_TEST_END

// A secondary erase defers its step the same way: ignored, the next entry is never read.
SYSIO_TEST_BEGIN(secondary_erase_result_unused_makes_no_step)
   begin_case();
   indexed_table t(owner);
   fill(t, {1, 2, 3});
   auto idx = t.get_index<"bybal"_n>();

   auto it = idx.find(uint64_t{20});
   host().clear_counts();
   idx.erase(std::move(it));
   CHECK_EQUAL(host().calls("kv_idx_erase"), 1u)
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().calls("kv_idx_next"), 0u)
   CHECK_EQUAL(host().calls("kv_get"), 0u)
SYSIO_TEST_END

// A secondary erase result steps back too, once it has settled on the next entry.
SYSIO_TEST_BEGIN(secondary_erase_result_steps_both_ways)
   begin_case();
   indexed_table t(owner);
   fill(t, {1, 2, 3, 4, 5});
   auto idx = t.get_index<"bybal"_n>();

   auto backward = idx.erase(idx.find(uint64_t{30}));
   --backward;
   CHECK_EQUAL(backward.key().id, 2u)

   auto forward = idx.erase(idx.find(uint64_t{20}));
   ++forward;
   CHECK_EQUAL(forward.key().id, 5u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(secondary_erase_result_is_the_next_entry)
   begin_case();
   indexed_table t(owner);
   fill(t, {1, 2, 3});
   auto idx = t.get_index<"bybal"_n>();

   auto next = idx.erase(idx.find(uint64_t{20}));
   CHECK_EQUAL(next != idx.end(), true)
   CHECK_EQUAL(next.key().id, 3u)
   CHECK_EQUAL(next->balance, 30u)

   auto past_last = idx.erase(std::move(next));
   CHECK_EQUAL(past_last == idx.end(), true)
   CHECK_ASSERT("deref invalid sec iter", ([&]() { (void)*past_last; }))

   for (auto it = idx.begin(); it != idx.end();) it = idx.erase(std::move(it));
   CHECK_EQUAL(host().rows.size(), 0u)
   CHECK_EQUAL(host().entries.size(), 0u)
SYSIO_TEST_END

// The scope check still ends a deferred secondary step at the edge of the scope.
SYSIO_TEST_BEGIN(secondary_erase_result_stays_in_scope)
   begin_case();
   scoped_indexed_table first(owner, 1);
   scoped_indexed_table second(owner, 2);
   fill(first, {1});
   fill(second, {1});

   auto idx = first.get_index<"bybal"_n>();
   auto next = idx.erase(idx.find(uint64_t{10}));
   CHECK_EQUAL(next == idx.end(), true)
   CHECK_EQUAL(second.contains({1}), true)
SYSIO_TEST_END

// --- Host-call targets on the new interface ---

SYSIO_TEST_BEGIN(emplace_is_one_host_call)
   begin_case();
   plain_table t(owner);
   t.emplace(payer, row_key{1}, row_val{10, payer});
   CHECK_EQUAL(host().calls("kv_set"), 1u)
   CHECK_EQUAL(host().total(), 1u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(erase_by_key_is_one_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   t.erase(row_key{1});
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 1u)
   CHECK_EQUAL(host().rows.size(), 0u)
SYSIO_TEST_END

// An index needs the old value to find its entries, so the read stays.
SYSIO_TEST_BEGIN(erase_by_key_with_index_reads_the_row)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   t.erase(row_key{1});
   CHECK_EQUAL(host().calls("kv_get"), 1u)
   CHECK_EQUAL(host().calls("kv_idx_erase"), 1u)
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 3u)
   CHECK_EQUAL(host().entries.size(), 0u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(begin_and_bounds_are_two_host_calls)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 3});
   {
      auto it = t.begin();
      CHECK_EQUAL(it.key().id, 1u)
      CHECK_EQUAL(host().calls("kv_it_create"), 1u)
      CHECK_EQUAL(host().calls("kv_it_read"), 1u)
      CHECK_EQUAL(host().total(), 2u)
   }
   host().clear_counts();
   {
      auto it = t.lower_bound({2});
      CHECK_EQUAL(it.key().id, 3u)
      CHECK_EQUAL(host().total(), 2u)
   }
   host().clear_counts();
   {
      auto it = t.upper_bound({1});
      CHECK_EQUAL(it.key().id, 3u)
      CHECK_EQUAL(host().total(), 2u)
   }
   auto past = t.upper_bound({3});
   CHECK_EQUAL(past == t.end(), true)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(begin_of_an_empty_table_is_end)
   begin_case();
   plain_table t(owner);
   auto it = t.begin();
   CHECK_EQUAL(it == t.end(), true)
   --it;
   CHECK_EQUAL(it == t.end(), true)
SYSIO_TEST_END

// One step, one host call, with the key and value filled in by that call.
SYSIO_TEST_BEGIN(scan_row_is_one_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2, 3, 4});

   auto it = t.begin();
   host().clear_counts();
   uint32_t rows = 0;
   for (; it != t.end(); ++it) ++rows;
   CHECK_EQUAL(rows, 4u)
   CHECK_EQUAL(host().calls("kv_it_next"), 4u)   // three rows, then the step off the end
   CHECK_EQUAL(host().total(), 4u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reverse_step_from_end_is_two_host_calls)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});
   auto it = t.end();
   --it;
   CHECK_EQUAL(it.key().id, 2u)
   CHECK_EQUAL(host().calls("kv_it_create"), 1u)
   CHECK_EQUAL(host().calls("kv_it_prev"), 1u)
   CHECK_EQUAL(host().total(), 2u)
SYSIO_TEST_END

// One call a row, and the scope is the prefix: nothing reads the key to check where it landed.
SYSIO_TEST_BEGIN(index_scan_row_is_two_host_calls)
   begin_case();
   scoped_indexed_table first(owner, 1);
   scoped_indexed_table second(owner, 2);
   fill(first, {1, 2, 3});
   fill(second, {1, 2, 3});
   auto idx = first.get_index<"bybal"_n>();

   auto it = idx.begin();
   CHECK_EQUAL(host().calls("kv_idx_create"), 1u)
   CHECK_EQUAL(host().calls("kv_idx_read"), 1u)
   CHECK_EQUAL(host().calls("kv_get"), 1u)
   CHECK_EQUAL(host().total(), 3u)

   host().clear_counts();
   ++it;
   CHECK_EQUAL(it.key().id, 2u)
   CHECK_EQUAL(host().calls("kv_idx_next"), 1u)
   CHECK_EQUAL(host().calls("kv_get"), 1u)
   CHECK_EQUAL(host().total(), 2u)

   ++it;
   ++it;
   CHECK_EQUAL(it == idx.end(), true)   // stopped at the edge of scope 1
SYSIO_TEST_END

SYSIO_TEST_BEGIN(key_only_index_scan_is_one_host_call_a_row)
   begin_case();
   scoped_indexed_table first(owner, 1);
   scoped_indexed_table second(owner, 2);
   fill(first, {1, 2});
   fill(second, {1});
   auto idx = first.get_index<"bybal"_n>();

   auto it = idx.key_begin();
   CHECK_EQUAL(it->key.id, 1u)
   CHECK_EQUAL(it->sec_key, 10u)
   host().clear_counts();
   ++it;
   CHECK_EQUAL(it->key.id, 2u)
   CHECK_EQUAL(it->sec_key, 20u)
   CHECK_EQUAL(host().total(), 1u)
   ++it;
   CHECK_EQUAL(it == idx.key_end(), true)

   auto last = idx.key_end();
   --last;
   CHECK_EQUAL(last->key.id, 2u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(contains_is_one_host_call)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   CHECK_EQUAL(t.contains({1}), true)
   CHECK_EQUAL(t.contains({2}), false)
   CHECK_EQUAL(host().calls("kv_get"), 2u)
   CHECK_EQUAL(host().total(), 2u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(upsert_without_an_index_is_one_host_call)
   begin_case();
   plain_table t(owner);
   t.upsert(payer, row_key{1}, row_val{10, payer});
   CHECK_EQUAL(host().total(), 1u)
   t.upsert(payer, row_key{1}, row_val{11, payer});
   CHECK_EQUAL(host().total(), 2u)
   CHECK_EQUAL(t.get({1}).balance, 11u)
SYSIO_TEST_END

// With an index, upsert still reads the old value to store or move the entry.
SYSIO_TEST_BEGIN(upsert_with_an_index_keeps_entries_in_step)
   begin_case();
   indexed_table t(owner);
   t.upsert(payer, row_key{1}, row_val{10, payer});
   CHECK_EQUAL(host().calls("kv_idx_store"), 1u)
   t.upsert(payer, row_key{1}, row_val{20, payer});
   CHECK_EQUAL(host().calls("kv_idx_update"), 1u)
   CHECK_EQUAL(host().entries.size(), 1u)

   // The lambda form takes the same two paths.
   t.upsert(payer, row_key{2}, row_val{30, payer}, [](row_val& v) { v.balance = 99; });
   CHECK_EQUAL(t.get({2}).balance, 30u)
   t.upsert(payer, row_key{2}, row_val{30, payer}, [](row_val& v) { v.balance = 40; });
   CHECK_EQUAL(t.get({2}).balance, 40u)
   CHECK_EQUAL(host().entries.size(), 2u)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(global_costs_one_host_call_each)
   begin_case();
   config_global g(owner);
   CHECK_EQUAL(g.exists(), false)
   CHECK_EQUAL(host().total(), 1u)

   // Removing what is not there is a silent no-op, and no longer probes first. It is still a write
   // attempt: global_remove_aborts_in_a_read_only_transaction covers that side.
   host().clear_counts();
   g.remove();
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 1u)

   host().clear_counts();
   g.set(row_val{1, payer}, payer);
   CHECK_EQUAL(host().total(), 1u)

   host().clear_counts();
   g.remove();
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 1u)
   CHECK_EQUAL(host().rows.size(), 0u)
   CHECK_EQUAL(host().receiver_calls, 0u)
SYSIO_TEST_END

// kv_erase refuses a read-only transaction before it looks for a row, so one call means removing
// nothing aborts there. cached_value::remove() is the form that probes first and stays legal.
SYSIO_TEST_BEGIN(global_remove_aborts_in_a_read_only_transaction)
   begin_case();
   config_global g(owner);
   host().read_only = true;
   CHECK_EQUAL(g.exists(), false)
   host().clear_counts();
   CHECK_ASSERT("cannot erase a KV record when executing a readonly transaction", ([&]() { g.remove(); }))
   CHECK_EQUAL(host().calls("kv_erase"), 1u)
   CHECK_EQUAL(host().total(), 1u)
SYSIO_TEST_END

// Every wrapper write reaches a host call that refuses a read-only transaction, and every read works.
SYSIO_TEST_BEGIN(read_only_transaction_refuses_each_wrapper_write)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   host().read_only = true;

   CHECK_EQUAL(t.get({1}).balance, 10u)
   CHECK_EQUAL(t.begin().key().id, 1u)
   auto idx = t.get_index<"bybal"_n>();
   CHECK_EQUAL(idx.begin().key().id, 1u)

   CHECK_ASSERT("cannot store a KV record when executing a readonly transaction",
                ([&]() { t.emplace(payer, row_key{2}, row_val{20, payer}); }))
   CHECK_ASSERT("cannot update a KV index when executing a readonly transaction",
                ([&]() { t.modify(payer, row_key{1}, [](row_val& v) { v.balance = 11; }); }))
   CHECK_ASSERT("cannot remove a KV index when executing a readonly transaction",
                ([&]() { t.erase(row_key{1}); }))

   plain_table plain(owner);
   CHECK_ASSERT("cannot erase a KV record when executing a readonly transaction",
                ([&]() { plain.erase(row_key{1}); }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(scope_iteration_costs)
   begin_case();
   scoped_table a(owner, 3);
   scoped_table b(owner, 7);
   fill(a, {1, 2});
   fill(b, {1});

   auto it = scoped_table::scope_lower_bound(owner, 0);
   CHECK_EQUAL(*it, 3u)
   CHECK_EQUAL(host().calls("kv_it_create"), 1u)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)
   CHECK_EQUAL(host().total(), 2u)

   host().clear_counts();
   ++it;
   CHECK_EQUAL(*it, 7u)
   CHECK_EQUAL(host().calls("kv_it_lower_bound"), 1u)
   CHECK_EQUAL(host().total(), 1u)

   ++it;
   CHECK_EQUAL(it == scoped_table::scope_end(), true)
SYSIO_TEST_END

// --- Statuses reach the contract as its own message ---

// emplace keeps its caller's text: five system contract calls pass their own, pinned by their tests.
SYSIO_TEST_BEGIN(emplace_exists_raises_the_contract_message)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   CHECK_ASSERT("key already exists", ([&]() { t.emplace(payer, row_key{1}, row_val{1, payer}); }))
   CHECK_ASSERT("symbol already exists", ([&]() { t.emplace(payer, row_key{1}, row_val{1, payer}, "symbol already exists"); }))
   CHECK_ASSERT("already a candidate", ([&]() {
      t.emplace(payer, row_key{1}, [](row_val& v) { v.balance = 1; }, "already a candidate");
   }))
   CHECK_EQUAL(t.get({1}).balance, 10u)   // the row came through untouched
SYSIO_TEST_END

// same_payer on a duplicate must report the duplicate, not the payer.
SYSIO_TEST_BEGIN(emplace_with_same_payer_reports_the_duplicate)
   begin_case();
   plain_table t(owner);
   fill(t, {1});
   CHECK_ASSERT("key already exists", ([&]() { t.emplace(same_payer, row_key{1}, row_val{1, payer}); }))
   CHECK_ASSERT("must specify a valid account to pay for new record",
                ([&]() { t.emplace(same_payer, row_key{2}, row_val{1, payer}); }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(erase_by_key_raises_the_contract_message)
   begin_case();
   plain_table plain(owner);
   indexed_table indexed(owner);
   CHECK_ASSERT("key not found", ([&]() { plain.erase(row_key{1}); }))
   CHECK_ASSERT("nothing to close", ([&]() { plain.erase(row_key{1}, "nothing to close"); }))
   CHECK_ASSERT("key not found", ([&]() { indexed.erase(row_key{1}); }))
   CHECK_ASSERT("nothing to close", ([&]() { indexed.erase(row_key{1}, "nothing to close"); }))
SYSIO_TEST_END

SYSIO_TEST_BEGIN(modify_by_key_raises_the_contract_message)
   begin_case();
   plain_table t(owner);
   CHECK_ASSERT("key not found", ([&]() { t.modify(payer, row_key{1}, [](row_val&) {}); }))
   CHECK_ASSERT("no such row", ([&]() { t.modify(payer, row_key{1}, [](row_val&) {}, "no such row"); }))
SYSIO_TEST_END

// A write through a stale iterator is refused rather than silently creating the row again.
SYSIO_TEST_BEGIN(stale_iterator_writes_are_refused)
   begin_case();
   plain_table t(owner);
   fill(t, {1, 2});
   auto modified = t.find({1});
   auto erased = t.find({1});
   t.erase(row_key{1});

   CHECK_ASSERT("cannot modify a row the receiver does not hold", ([&]() { t.modify(payer, modified, row_val{}); }))
   CHECK_EQUAL(t.contains({1}), false)
   CHECK_ASSERT("KV key not found for erase", ([&]() { t.erase(erased); }))
SYSIO_TEST_END

// The same refusal through a secondary iterator, whose row was erased behind it.
SYSIO_TEST_BEGIN(stale_secondary_iterator_write_is_refused)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   auto idx = t.get_index<"bybal"_n>();
   auto it = idx.find(uint64_t{10});
   host().rows.clear();
   CHECK_ASSERT("cannot modify a row that no longer exists", ([&]() { idx.modify(payer, it, row_val{10, payer}); }))
SYSIO_TEST_END

// An index that has come apart from its table cannot carry on. The host used to raise these itself.
SYSIO_TEST_BEGIN(index_statuses_raise_named_messages)
   begin_case();
   indexed_table t(owner);
   fill(t, {1});
   const uint32_t index_id = sysio::kv::compute_sec_table_id("indexed"_n.value, "bybal"_n.value);

   // A row with no entry: nothing to erase, nothing to move.
   const auto saved = host().entries;
   host().entries.clear();
   CHECK_ASSERT("KV secondary index entry not found for remove", ([&]() { t.erase(row_key{1}); }))
   CHECK_ASSERT("KV secondary index entry not found for update", ([&]() {
      t.modify(payer, row_key{1}, [](row_val& v) { v.balance = 77; });
   }))

   // A new entry at a key already taken.
   host().entries = saved;
   auto pri = indexed_table::make_unscoped_key(row_key{2});
   auto sec = t.encode_sec_key(uint64_t{20});
   host().seed_entry(owner.value, index_id, std::string(sec.data(), sec.size()), std::string(pri.data(), pri.size()));
   CHECK_ASSERT("KV secondary index entry already exists",
                ([&]() { t.emplace(payer, row_key{2}, row_val{20, payer}); }))

   // Moving an entry onto one that exists.
   host().rows.erase(kv_mock::store::row_key{owner.value, sysio::kv::compute_table_id("indexed"_n.value),
                                             std::string(t.make_key(row_key{2}).data(), t.make_key(row_key{2}).size())});
   auto taken = indexed_table::make_unscoped_key(row_key{1});
   host().seed_entry(owner.value, index_id, std::string(sec.data(), sec.size()), std::string(taken.data(), taken.size()));
   CHECK_ASSERT("KV secondary index entry already exists", ([&]() {
      t.modify(payer, row_key{1}, [](row_val& v) { v.balance = 20; });
   }))
SYSIO_TEST_END

// --- Rows larger than the stack buffers ---

// A value past the buffer: the step reports its full size and one read fetches it.
SYSIO_TEST_BEGIN(scan_rereads_a_value_past_the_buffer)
   begin_case();
   text_table t(owner);
   const std::string big(sysio::kv::kv_value_stack_size * 2, 'x');
   t.emplace(payer, row_key{1}, text_val{"small"});
   t.emplace(payer, row_key{2}, text_val{big});
   t.emplace(payer, row_key{3}, text_val{"small"});
   host().clear_counts();

   auto it = t.begin();
   CHECK_EQUAL(it->text == "small", true)
   host().clear_counts();
   ++it;
   CHECK_EQUAL(it->text == big, true)
   CHECK_EQUAL(host().calls("kv_it_next"), 1u)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)
   CHECK_EQUAL(host().total(), 2u)
   ++it;
   CHECK_EQUAL(it->text == "small", true)
SYSIO_TEST_END

// A key past key_buf's inline storage, through every path that holds one.
SYSIO_TEST_BEGIN(long_keys_survive_every_path)
   begin_case();
   long_table t(owner);
   const std::string first(sysio::kv::key_buf::inline_cap + 10, 'a');
   const std::string second(sysio::kv::key_buf::inline_cap + 10, 'b');
   t.emplace(payer, long_key{first}, text_val{"one"});
   t.emplace(payer, long_key{second}, text_val{"two"});

   auto it = t.begin();
   CHECK_EQUAL(it.key().name == first, true)
   ++it;
   CHECK_EQUAL(it.key().name == second, true)
   CHECK_EQUAL(it->text == "two", true)

   // Stepping a found iterator seeks past a long key by key.
   auto found = t.find(long_key{first});
   ++found;
   CHECK_EQUAL(found.key().name == second, true)
   auto back = t.find(long_key{second});
   --back;
   CHECK_EQUAL(back.key().name == first, true)
SYSIO_TEST_END

// Both keys of a secondary entry past the stack buffers: re-read, then the row by its full key.
SYSIO_TEST_BEGIN(long_secondary_keys_are_reread)
   begin_case();
   long_index_table t(owner);
   const std::string text(sysio::kv::key_buf::inline_cap + 10, 'k');
   t.emplace(payer, row_key{1}, text_val{text});
   auto idx = t.get_index<"bytext"_n>();

   auto keys = idx.key_begin();
   CHECK_EQUAL(keys->sec_key == text, true)
   CHECK_EQUAL(keys->key.id, 1u)

   auto it = idx.find(text);
   CHECK_EQUAL(it != idx.end(), true)
   CHECK_EQUAL(it->text == text, true)
   auto ub = idx.upper_bound(text);
   CHECK_EQUAL(ub == idx.end(), true)
SYSIO_TEST_END

// A row shorter than a fixed-size value traps rather than read past what was stored.
SYSIO_TEST_BEGIN(short_stored_row_traps)
   begin_case();
   plain_table t(owner);
   auto key = t.make_key(row_key{1});
   host().seed(owner.value, sysio::kv::compute_table_id("plain"_n.value), std::string(key.data(), key.size()), "abc");
   CHECK_ASSERT("datastream attempted to read past the end", ([&]() { t.find({1}); }))
SYSIO_TEST_END

// A row longer than a fixed-size value reads its leading fields and ignores the rest. A contract can read rows a wider
// struct wrote, so this is pinned as the behavior rather than made a trap. kv::global of a fixed-size value does trap.
SYSIO_TEST_BEGIN(long_stored_row_reads_its_leading_fields)
   begin_case();
   plain_table t(owner);
   auto key = t.make_key(row_key{1});
   const row_val stored{10, payer};
   host().seed(owner.value, sysio::kv::compute_table_id("plain"_n.value), std::string(key.data(), key.size()),
               stored_bytes(stored) + "extra bytes");
   auto it = t.find({1});
   CHECK_EQUAL(it != t.end(), true)
   CHECK_EQUAL(it->balance, stored.balance)
   CHECK_EQUAL(it->holder == stored.holder, true)
SYSIO_TEST_END

// Secondary bounds and reverse steps keep to their scope with the prefix doing the work.
SYSIO_TEST_BEGIN(secondary_bounds_stay_in_scope)
   begin_case();
   scoped_indexed_table first(owner, 1);
   scoped_indexed_table second(owner, 2);
   fill(first, {1, 2});
   fill(second, {3, 4});
   auto idx = first.get_index<"bybal"_n>();

   CHECK_EQUAL(idx.find(uint64_t{30}) == idx.end(), true)          // scope 2's entry
   CHECK_EQUAL(idx.lower_bound(uint64_t{25}) == idx.end(), true)
   CHECK_EQUAL(idx.upper_bound(uint64_t{20}) == idx.end(), true)
   CHECK_EQUAL(idx.upper_bound(uint64_t{10}).key().id, 2u)
   CHECK_EQUAL(idx.lower_bound(uint64_t{0}).key().id, 1u)

   auto last = idx.end();
   --last;
   CHECK_EQUAL(last.key().id, 2u)
   --last;
   CHECK_EQUAL(last.key().id, 1u)
   --last;
   CHECK_EQUAL(last == idx.end(), true)

   auto past = idx.lower_bound(uint64_t{25});
   --past;
   CHECK_EQUAL(past.key().id, 2u)                                  // a bound past the scope steps back into it
SYSIO_TEST_END

// get hands back the value it read, with no copy of it. A deep copy of a V is contract time.
SYSIO_TEST_BEGIN(get_moves_the_value_out)
   begin_case();
   counted_table t(owner);
   t.emplace(payer, row_key{1}, copy_counted{});
   copy_counted::copies = 0;
   auto value = t.get({1});
   CHECK_EQUAL(value.value, 0u)
   CHECK_EQUAL(copy_counted::copies, 0u)
SYSIO_TEST_END

// --- Keys and values at the edge of a buffer ---

// 60 NULs and 190 plain bytes are 250 raw bytes, which a check on raw bytes passes, and 312 encoded
// ones. The encoder used to write them past its 256 byte buffer, over its own size_ and up the stack.
SYSIO_TEST_BEGIN(escaped_key_past_the_buffer_aborts)
   const auto key = nul_heavy(60, 190);
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(key); }))
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(as_bytes(key)); }))
SYSIO_TEST_END

// The encoded size decides, to the byte, whichever way the bytes split between plain and NUL.
SYSIO_TEST_BEGIN(escaped_key_fits_to_the_last_byte)
   const size_t room = be_key_stream::buf_cap - 2;   // the rest is the terminator
   CHECK_EQUAL(encoded_size(std::string(room, 'a')), be_key_stream::buf_cap)
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(std::string(room + 1, 'a')); }))

   CHECK_EQUAL(encoded_size(nul_heavy(room / 2, 0)), be_key_stream::buf_cap)
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(nul_heavy(room / 2 + 1, 0)); }))

   CHECK_EQUAL(encoded_size(nul_heavy(100, room - 200)), be_key_stream::buf_cap)
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(nul_heavy(100, room - 199)); }))

   CHECK_EQUAL(encoded_size(as_bytes(nul_heavy(100, room - 200))), be_key_stream::buf_cap)
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { encoded_size(as_bytes(nul_heavy(100, room - 199))); }))
SYSIO_TEST_END

// What came before the field counts against the same buffer.
SYSIO_TEST_BEGIN(escaped_key_after_a_scope_counts_the_scope)
   const size_t room = be_key_stream::buf_cap - sizeof(uint64_t) - 2;
   CHECK_ASSERT("be_key_stream: key too large", ([&]() {
      be_key_stream stream;
      stream << uint64_t{1} << nul_heavy(room / 2 + 1, 0);
   }))
   be_key_stream stream;
   stream << uint64_t{1} << nul_heavy(room / 2, 0);
   CHECK_EQUAL(stream.size(), be_key_stream::buf_cap)
SYSIO_TEST_END

SYSIO_TEST_BEGIN(escaped_key_round_trips_at_the_limit)
   const auto key = nul_heavy(100, 54);
   be_key_stream stream;
   stream << key;
   be_key_reader reader(stream.data(), stream.size());
   std::string decoded;
   reader >> decoded;
   CHECK_EQUAL(decoded == key, true)
SYSIO_TEST_END

// A wrapper aborts on such a key before it reaches the host, on a read as on a write.
SYSIO_TEST_BEGIN(wrappers_refuse_an_escaped_key_past_the_buffer)
   begin_case();
   long_table t(owner);
   const long_key key{nul_heavy(60, 190)};
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { t.find(key); }))
   CHECK_ASSERT("be_key_stream: key too large", ([&]() { t.emplace(payer, key, text_val{"v"}); }))
   CHECK_EQUAL(host().total(), 0u)
SYSIO_TEST_END

// key_buf holds 64 bytes inline and the stack read matches it: 64 fits in the first read and 65 needs
// a second, on a handle and on a seek past the key.
SYSIO_TEST_BEGIN(keys_at_the_inline_edge)
   begin_case();
   long_table t(owner);
   const uint32_t inline_cap = sysio::kv::key_buf::inline_cap;
   const std::string fits(inline_cap - 2, 'a');
   const std::string over(inline_cap - 1, 'b');
   CHECK_EQUAL(encoded_size(fits), inline_cap)
   CHECK_EQUAL(encoded_size(over), inline_cap + 1)
   t.emplace(payer, long_key{fits}, text_val{"one"});
   t.emplace(payer, long_key{over}, text_val{"two"});
   host().clear_counts();

   auto it = t.begin();
   CHECK_EQUAL(it.key().name == fits, true)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)

   host().clear_counts();
   ++it;
   CHECK_EQUAL(it.key().name == over, true)
   CHECK_EQUAL(it->text == "two", true)
   CHECK_EQUAL(host().calls("kv_it_next"), 1u)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)

   // Both survive a copy, and a step from it by key, through key_buf's inline and heap storage.
   auto copy = it;
   --copy;
   CHECK_EQUAL(copy.key().name == fits, true)
   auto copy_of_first = copy;
   ++copy_of_first;
   CHECK_EQUAL(copy_of_first.key().name == over, true)
SYSIO_TEST_END

// The same edge on both keys of an index entry.
SYSIO_TEST_BEGIN(secondary_keys_at_the_inline_edge)
   begin_case();
   long_index_table t(owner);
   const uint32_t inline_cap = sysio::kv::key_buf::inline_cap;
   const std::string fits(inline_cap - 2, 'a');
   const std::string over(inline_cap - 1, 'b');
   t.emplace(payer, row_key{1}, text_val{fits});
   t.emplace(payer, row_key{2}, text_val{over});
   auto idx = t.get_index<"bytext"_n>();

   auto keys = idx.key_begin();
   CHECK_EQUAL(keys->sec_key == fits, true)
   ++keys;
   CHECK_EQUAL(keys->sec_key == over, true)
   CHECK_EQUAL(idx.find(over)->text == over, true)
   CHECK_EQUAL(idx.upper_bound(fits).key().id, 2u)
SYSIO_TEST_END

// ser_buf and the value stack hold 256 bytes: 256 reads once and 257 reads again at full size, on a
// lookup and on a scan.
SYSIO_TEST_BEGIN(values_at_the_stack_edge)
   begin_case();
   text_table t(owner);
   const uint32_t stack = sysio::kv::kv_value_stack_size;
   const auto fits = packing_to(stack);
   const auto over = packing_to(stack + 1);
   CHECK_EQUAL(sysio::pack_size(fits), stack)
   CHECK_EQUAL(sysio::pack_size(over), stack + 1)
   t.emplace(payer, row_key{1}, fits);
   t.emplace(payer, row_key{2}, over);

   host().clear_counts();
   CHECK_EQUAL(t.find({1})->text == fits.text, true)
   CHECK_EQUAL(host().calls("kv_get"), 1u)

   host().clear_counts();
   CHECK_EQUAL(t.find({2})->text == over.text, true)
   CHECK_EQUAL(host().calls("kv_get"), 2u)

   host().clear_counts();
   auto it = t.begin();
   CHECK_EQUAL(it->text == fits.text, true)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)

   host().clear_counts();
   ++it;
   CHECK_EQUAL(it->text == over.text, true)
   CHECK_EQUAL(host().calls("kv_it_next"), 1u)
   CHECK_EQUAL(host().calls("kv_it_read"), 1u)
SYSIO_TEST_END

// A value written back at the edge: the write path serializes through the same ser_buf.
SYSIO_TEST_BEGIN(value_writes_at_the_stack_edge)
   begin_case();
   text_table t(owner);
   const uint32_t stack = sysio::kv::kv_value_stack_size;
   t.emplace(payer, row_key{1}, packing_to(stack));
   t.modify(payer, row_key{1}, [&](text_val& v) { v = packing_to(stack + 1); });
   CHECK_EQUAL(t.get({1}).text == packing_to(stack + 1).text, true)
   t.upsert(payer, row_key{1}, packing_to(stack));
   CHECK_EQUAL(t.get({1}).text == packing_to(stack).text, true)
SYSIO_TEST_END

// The longest key the encoder makes has a successor past ser_buf's stack, on every path that seeks one.
SYSIO_TEST_BEGIN(successor_of_the_longest_key_takes_the_heap)
   begin_case();
   long_table t(owner);
   const std::string longest(be_key_stream::buf_cap - 2, 'm');
   const std::string after(be_key_stream::buf_cap - 2, 'n');
   CHECK_EQUAL(encoded_size(longest) + 1 > sysio::kv::kv_value_stack_size, true)
   t.emplace(payer, long_key{longest}, text_val{"one"});
   t.emplace(payer, long_key{after}, text_val{"two"});

   CHECK_EQUAL(t.upper_bound(long_key{longest}).key().name == after, true)

   auto found = t.find(long_key{longest});
   ++found;
   CHECK_EQUAL(found.key().name == after, true)

   auto next = t.erase(t.find(long_key{longest}));
   CHECK_EQUAL(next.key().name == after, true)
SYSIO_TEST_END

// On a scoped table the scope takes eight of the same bytes.
SYSIO_TEST_BEGIN(successor_of_the_longest_scoped_key_takes_the_heap)
   begin_case();
   using scoped_long_table = kv::scoped_table<"scopedlong"_n, long_key, text_val>;
   scoped_long_table t(owner, 7);
   const std::string longest(be_key_stream::buf_cap - sizeof(uint64_t) - 2, 'm');
   const std::string after(be_key_stream::buf_cap - sizeof(uint64_t) - 2, 'n');
   t.emplace(payer, long_key{longest}, text_val{"one"});
   t.emplace(payer, long_key{after}, text_val{"two"});

   CHECK_EQUAL(t.upper_bound(long_key{longest}).key().name == after, true)
   auto found = t.find(long_key{longest});
   ++found;
   CHECK_EQUAL(found.key().name == after, true)
SYSIO_TEST_END

// The largest key the chain allows is 512 bytes, and it can be all 0xFF. The seek past every key has to
// sort above it: one that equalled it would land on that row, and prev would then skip it.
SYSIO_TEST_BEGIN(end_decrement_reaches_a_key_of_all_ff_at_the_cap)
   begin_case();
   plain_table t(owner);
   const std::string top(sysio::kv::kv_key_max_bytes, '\xFF');
   host().seed(owner.value, sysio::kv::compute_table_id("plain"_n.value), top, stored_row_val());

   auto it = t.end();
   --it;
   CHECK_EQUAL(it != t.end(), true)
   CHECK_EQUAL(t.rbegin() != t.rend(), true)
SYSIO_TEST_END

// The same on an index, whose entries sort by the whole secondary key.
SYSIO_TEST_BEGIN(end_decrement_reaches_a_secondary_key_of_all_ff_at_the_cap)
   begin_case();
   indexed_table t(owner);
   const uint32_t index_id = sysio::kv::compute_sec_table_id("indexed"_n.value, "bybal"_n.value);
   const auto pri = indexed_table::make_unscoped_key(row_key{1});
   const std::string pri_key(pri.data(), pri.size());
   host().seed(owner.value, sysio::kv::compute_table_id("indexed"_n.value), pri_key, stored_row_val());
   host().seed_entry(owner.value, index_id, std::string(sysio::kv::kv_key_max_bytes, '\xFF'), pri_key);
   auto idx = t.get_index<"bybal"_n>();

   auto it = idx.end();
   --it;
   CHECK_EQUAL(it != idx.end(), true)
   CHECK_EQUAL(it.key().id, 1u)
   auto keys = idx.key_end();
   --keys;
   CHECK_EQUAL(keys != idx.key_end(), true)
SYSIO_TEST_END

// The last key's primary_key() plus one, or 0 on an empty table, in two calls rather than four.
SYSIO_TEST_BEGIN(available_primary_key_on_kv_table)
   begin_case();
   auto_table t(owner);
   CHECK_EQUAL(t.available_primary_key(), 0u)
   t.emplace(payer, auto_key{4}, row_val{1, payer});
   host().clear_counts();
   CHECK_EQUAL(t.available_primary_key(), 5u)
   CHECK_EQUAL(host().calls("kv_it_create"), 1u)
   CHECK_EQUAL(host().calls("kv_it_prev"), 1u)
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(be_key_reader_uint128_roundtrip);
   SYSIO_TEST(be_key_reader_int128_zero);
   SYSIO_TEST(be_key_reader_int128_negative_one);
   SYSIO_TEST(be_key_reader_int128_max);
   SYSIO_TEST(find_hit_is_one_host_call);
   SYSIO_TEST(find_miss_is_one_host_call);
   SYSIO_TEST(default_code_uses_dispatcher_global);
   SYSIO_TEST(find_large_value_rereads_once);
   SYSIO_TEST(find_then_erase_is_two_host_calls);
   SYSIO_TEST(find_then_erase_with_index_adds_only_the_entry);
   SYSIO_TEST(erase_result_is_the_next_row);
   SYSIO_TEST(erase_result_settles_on_every_read);
   SYSIO_TEST(erase_of_a_moved_handle_steps_it);
   SYSIO_TEST(erase_of_an_lvalue_hands_its_handle_on);
   SYSIO_TEST(erase_loop_is_two_host_calls_a_row);
   SYSIO_TEST(erased_lvalue_stays_on_its_row);
   SYSIO_TEST(erased_lvalue_keeps_an_owed_step);
   SYSIO_TEST(erase_result_steps_both_ways);
   SYSIO_TEST(copy_of_an_erase_result_owes_its_own_step);
   SYSIO_TEST(erase_by_key_on_a_foreign_handle);
   SYSIO_TEST(erase_loops_clear_the_table);
   SYSIO_TEST(erase_result_skips_a_key_rewritten_before_use);
   SYSIO_TEST(erase_result_resolves_at_first_use);
   SYSIO_TEST(found_iterator_opens_a_handle_to_step);
   SYSIO_TEST(found_iterator_steps_past_its_erased_row);
   SYSIO_TEST(found_iterator_steps_back_from_erased_last_row);
   SYSIO_TEST(copy_makes_no_host_call);
   SYSIO_TEST(end_iterator_steps_back_to_the_last_row);
   SYSIO_TEST(reverse_iteration_visits_every_row);
   SYSIO_TEST(reverse_iterator_reference_outlives_the_read);
   SYSIO_TEST(reverse_scan_is_one_host_call_a_row);
   SYSIO_TEST(reverse_reads_make_no_host_call);
   SYSIO_TEST(reverse_iterator_base_is_one_row_past);
   SYSIO_TEST(reverse_iterator_from_a_forward_iterator);
   SYSIO_TEST(reverse_iterator_steps_both_ways);
   SYSIO_TEST(reverse_iterator_edges_abort);
   SYSIO_TEST(reverse_iteration_of_an_empty_table);
   SYSIO_TEST(reverse_decrement_of_an_empty_table_aborts);
   SYSIO_TEST(reverse_iterator_rebuilds_from_the_erase_result);
   SYSIO_TEST(reverse_iterator_holds_its_row_through_an_erase);
   SYSIO_TEST(reverse_erase_loop_is_two_host_calls_a_row);
   SYSIO_TEST(reverse_iterator_does_not_see_a_later_row);
   SYSIO_TEST(reverse_iteration_stays_in_scope);
   SYSIO_TEST(reverse_scan_rereads_a_row_past_the_buffers);
   SYSIO_TEST(default_constructed_iterator_cannot_step_back);
   SYSIO_TEST(end_iterator_messages_are_unchanged);
   SYSIO_TEST(modify_keeps_the_iterator_current);
   SYSIO_TEST(find_then_modify_is_two_host_calls);
   SYSIO_TEST(index_modify_there_and_back_through_one_iterator);
   SYSIO_TEST(modify_through_a_stale_copy_moves_the_entry_from_the_stored_row);
   SYSIO_TEST(modify_through_an_iterator_reads_an_indexed_row_first);
   SYSIO_TEST(modify_through_a_foreign_iterator_needs_a_row_at_the_receiver);
   SYSIO_TEST(modify_through_an_iterator_refuses_a_gone_row);
   SYSIO_TEST(sixteen_indices_store_move_and_erase);
   SYSIO_TEST(checksum_keys_read_back);
   SYSIO_TEST(scoped_find_and_erase_stay_in_scope);
   SYSIO_TEST(secondary_erase_result_unused_makes_no_step);
   SYSIO_TEST(secondary_erase_result_steps_both_ways);
   SYSIO_TEST(secondary_erase_result_is_the_next_entry);
   SYSIO_TEST(secondary_erase_result_stays_in_scope);
   SYSIO_TEST(emplace_is_one_host_call);
   SYSIO_TEST(erase_by_key_is_one_host_call);
   SYSIO_TEST(erase_by_key_with_index_reads_the_row);
   SYSIO_TEST(begin_and_bounds_are_two_host_calls);
   SYSIO_TEST(begin_of_an_empty_table_is_end);
   SYSIO_TEST(scan_row_is_one_host_call);
   SYSIO_TEST(reverse_step_from_end_is_two_host_calls);
   SYSIO_TEST(index_scan_row_is_two_host_calls);
   SYSIO_TEST(key_only_index_scan_is_one_host_call_a_row);
   SYSIO_TEST(contains_is_one_host_call);
   SYSIO_TEST(upsert_without_an_index_is_one_host_call);
   SYSIO_TEST(upsert_with_an_index_keeps_entries_in_step);
   SYSIO_TEST(global_costs_one_host_call_each);
   SYSIO_TEST(global_remove_aborts_in_a_read_only_transaction);
   SYSIO_TEST(read_only_transaction_refuses_each_wrapper_write);
   SYSIO_TEST(scope_iteration_costs);
   SYSIO_TEST(emplace_exists_raises_the_contract_message);
   SYSIO_TEST(emplace_with_same_payer_reports_the_duplicate);
   SYSIO_TEST(erase_by_key_raises_the_contract_message);
   SYSIO_TEST(modify_by_key_raises_the_contract_message);
   SYSIO_TEST(stale_iterator_writes_are_refused);
   SYSIO_TEST(stale_secondary_iterator_write_is_refused);
   SYSIO_TEST(index_statuses_raise_named_messages);
   SYSIO_TEST(scan_rereads_a_value_past_the_buffer);
   SYSIO_TEST(long_keys_survive_every_path);
   SYSIO_TEST(long_secondary_keys_are_reread);
   SYSIO_TEST(short_stored_row_traps);
   SYSIO_TEST(long_stored_row_reads_its_leading_fields);
   SYSIO_TEST(secondary_bounds_stay_in_scope);
   SYSIO_TEST(get_moves_the_value_out);
   SYSIO_TEST(escaped_key_past_the_buffer_aborts);
   SYSIO_TEST(escaped_key_fits_to_the_last_byte);
   SYSIO_TEST(escaped_key_after_a_scope_counts_the_scope);
   SYSIO_TEST(escaped_key_round_trips_at_the_limit);
   SYSIO_TEST(wrappers_refuse_an_escaped_key_past_the_buffer);
   SYSIO_TEST(keys_at_the_inline_edge);
   SYSIO_TEST(secondary_keys_at_the_inline_edge);
   SYSIO_TEST(values_at_the_stack_edge);
   SYSIO_TEST(value_writes_at_the_stack_edge);
   SYSIO_TEST(successor_of_the_longest_key_takes_the_heap);
   SYSIO_TEST(successor_of_the_longest_scoped_key_takes_the_heap);
   SYSIO_TEST(end_decrement_reaches_a_key_of_all_ff_at_the_cap);
   SYSIO_TEST(end_decrement_reaches_a_secondary_key_of_all_ff_at_the_cap);
   SYSIO_TEST(available_primary_key_on_kv_table);
   return has_failed();
}
