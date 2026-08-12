/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Coverage for sysio::kv::cached_value and its kv::cached_global alias (kv_cached.hpp). The
 *  scoped sysio::cached_kv_singleton alias is covered by the kv_singleton_tests WASM contract
 *  -- see the note above main().
 *
 *  The behaviour under test is WRITE SUPPRESSION. The classic singleton idiom caches state in
 *  a contract member and writes it back from the contract destructor, which the generated
 *  dispatcher runs after every action -- so even a pure query issues a kv_set, and the chain
 *  rejects that inside a read-only transaction with
 *
 *     cannot store a KV record when executing a readonly transaction
 *
 *  cached_value fixes this by only writing when a mutating call actually ran. Nearly every
 *  case below therefore asserts on an exact store-call COUNT, not just on the resulting value:
 *  a cache that happens to hold the right data but still writes on a read path would pass a
 *  value-only test and reintroduce the bug.
 *
 *  Two backings are exercised:
 *    - counting_store, a minimal Store that records every call, for the generic semantics.
 *    - the real kv::global over mocked KV intrinsics, for integration (key encoding, both
 *      serialization paths, payer propagation).
 */

#include <sysio/tester.hpp>
#include <sysio/kv_global.hpp>
#include <sysio/serialize.hpp>

#include <algorithm>
#include <cstring>
#include <map>
#include <string>
#include <tuple>
#include <vector>

using namespace sysio;
using namespace sysio::native;

namespace {

// ---------------------------------------------------------------------------
// Payload types
// ---------------------------------------------------------------------------

/// Fixed-serializable payload: no padding, so sizeof == pack_size and kv::global takes its
/// zero-copy memcpy path.
struct pod_state {
   uint64_t counter = 0;
   uint64_t flags   = 0;

   SYSLIB_SERIALIZE(pod_state, (counter)(flags))
};
inline bool operator==(const pod_state& a, const pod_state& b) {
   return a.counter == b.counter && a.flags == b.flags;
}
static_assert(sysio::kv::is_fixed_serializable_v<pod_state>,
              "pod_state must exercise kv::global's fixed-serializable path");

/// Variable-length payload: forces kv::global's pack/unpack path, and -- when packed larger
/// than kv_value_stack_size (256) -- its heap re-read branch.
struct blob_state {
   std::string           label;
   std::vector<uint64_t> values;

   SYSLIB_SERIALIZE(blob_state, (label)(values))
};
inline bool operator==(const blob_state& a, const blob_state& b) {
   return a.label == b.label && a.values == b.values;
}
static_assert(!sysio::kv::is_fixed_serializable_v<blob_state>,
              "blob_state must exercise kv::global's packed path");

/// A blob whose packed size comfortably exceeds kv_value_stack_size.
blob_state make_big_blob(uint64_t seed) {
   blob_state b;
   b.label.assign(400, static_cast<char>('a' + (seed % 26)));
   for (uint64_t i = 0; i < 20; ++i) b.values.push_back(seed + i);
   return b;
}

// ---------------------------------------------------------------------------
// counting_store -- minimal Store for the generic cached_value semantics
// ---------------------------------------------------------------------------

/// Satisfies cached_value's Store requirements and records every call. State is static
/// because cached_value owns its Store by value; call counters::reset() to start a case.
struct counting_store {
   using value_type = pod_state;

   struct counters {
      uint32_t   gets    = 0;
      uint32_t   sets    = 0;
      uint32_t   removes = 0;
      bool       present = false;
      pod_state  val{};
      sysio::name payer{};

      static counters& get() {
         static counters inst;
         return inst;
      }

      /// Start a case with an empty store.
      static void reset() { get() = counters{}; }

      /// Start a case with \p seeded already stored.
      static void seed(const pod_state& seeded) {
         reset();
         get().present = true;
         get().val     = seeded;
      }
   };

   bool try_get(pod_state& out) const {
      auto& c = counters::get();
      ++c.gets;
      if (!c.present) return false;
      out = c.val;
      return true;
   }

   void set(const pod_state& v, sysio::name payer) {
      auto& c = counters::get();
      ++c.sets;
      c.val     = v;
      c.payer   = payer;
      c.present = true;
   }

   void remove() {
      auto& c = counters::get();
      ++c.removes;
      c.present = false;
   }
};

using counting_cache = sysio::kv::cached_value<counting_store>;

// ---------------------------------------------------------------------------
// Mocked KV intrinsics -- faithful stand-in for the chain's KV store
// ---------------------------------------------------------------------------

/// Mirrors apply_context's kv_get / kv_set / kv_erase / kv_contains semantics, including the
/// asymmetry that matters here: reads take a `code` parameter, writes have none and always
/// land on the current receiver.
struct mock_kv {
   using row_key = std::tuple<uint64_t, uint32_t, std::string>;   // code, table_id, key

   std::map<row_key, std::string> rows;
   uint64_t receiver = 0;

   uint32_t gets     = 0;
   uint32_t sets     = 0;
   uint32_t erases   = 0;
   uint32_t contains = 0;
   uint64_t last_payer = 0;

   void reset(uint64_t who) {
      rows.clear();
      receiver   = who;
      gets = sets = erases = contains = 0;
      last_payer = 0;
   }
};

mock_kv& mock_store() {
   static mock_kv inst;
   return inst;
}

std::string as_key(const void* key, uint32_t key_size) {
   return std::string(static_cast<const char*>(key), key_size);
}

/// Install the mocked intrinsics. Idempotent; call once per case after mock_store().reset().
void install_kv_intrinsics() {
   intrinsics::set_intrinsic<intrinsics::current_receiver>(
      []() -> capi_name { return mock_store().receiver; });

   // No code parameter: writes always target the current receiver.
   intrinsics::set_intrinsic<intrinsics::kv_set>(
      [](uint32_t table_id, uint64_t payer, const void* key, uint32_t key_size,
         const void* value, uint32_t value_size) -> int64_t {
         auto& m = mock_store();
         ++m.sets;
         m.last_payer = payer;
         m.rows[mock_kv::row_key{m.receiver, table_id, as_key(key, key_size)}] =
            std::string(static_cast<const char*>(value), value_size);
         return 0;
      });

   // Returns the FULL stored size even when the caller's buffer is smaller, which is what
   // drives kv::global's heap re-read branch. -1 when absent.
   intrinsics::set_intrinsic<intrinsics::kv_get>(
      [](uint32_t table_id, capi_name code, const void* key, uint32_t key_size,
         void* value, uint32_t value_size) -> int32_t {
         auto& m = mock_store();
         ++m.gets;
         auto itr = m.rows.find(mock_kv::row_key{code, table_id, as_key(key, key_size)});
         if (itr == m.rows.end()) return -1;
         const auto sz = static_cast<uint32_t>(itr->second.size());
         if (value_size == 0) return static_cast<int32_t>(sz);
         const auto copy_size = std::min(value_size, sz);
         if (copy_size > 0) std::memcpy(value, itr->second.data(), copy_size);
         return static_cast<int32_t>(sz);
      });

   intrinsics::set_intrinsic<intrinsics::kv_erase>(
      [](uint32_t table_id, const void* key, uint32_t key_size) -> int64_t {
         auto& m = mock_store();
         ++m.erases;
         m.rows.erase(mock_kv::row_key{m.receiver, table_id, as_key(key, key_size)});
         return 0;
      });

   intrinsics::set_intrinsic<intrinsics::kv_contains>(
      [](uint32_t table_id, capi_name code, const void* key, uint32_t key_size) -> int32_t {
         auto& m = mock_store();
         ++m.contains;
         return m.rows.count(mock_kv::row_key{code, table_id, as_key(key, key_size)}) ? 1 : 0;
      });
}

constexpr auto test_code = "testacct"_n;
constexpr auto payer_a   = "payerone"_n;
constexpr auto payer_b   = "payertwo"_n;

/// Reset the mock store and install the intrinsics for a case.
void begin_kv_case() {
   mock_store().reset(test_code.value);
   install_kv_intrinsics();
}

using pod_global   = sysio::kv::global<"cfg"_n, pod_state>;
using pod_cached   = sysio::kv::cached_global<"cfg"_n, pod_state>;
using blob_global  = sysio::kv::global<"blob"_n, blob_state>;
using blob_cached  = sysio::kv::cached_global<"blob"_n, blob_state>;

} // namespace

// ===========================================================================
// Group 1 -- generic cached_value semantics over counting_store
// ===========================================================================

/// The core regression: reading through the cache must not write, not even at destruction.
SYSIO_TEST_BEGIN(cached_read_never_writes)
   counting_store::counters::seed(pod_state{7, 3});
   {
      counting_cache c;
      CHECK_EQUAL(c.exists(), true)
      CHECK_EQUAL(c.get(), (pod_state{7, 3}))
      CHECK_EQUAL(c.get_or_default(pod_state{99, 99}), (pod_state{7, 3}))
      CHECK_EQUAL(c.dirty(), false)
   }   // destructor runs here -- this is where the old idiom wrote
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
SYSIO_TEST_END

/// Reads beyond the first must not touch the store again.
SYSIO_TEST_BEGIN(cached_load_happens_once)
   counting_store::counters::seed(pod_state{1, 1});
   {
      counting_cache c;
      CHECK_EQUAL(c.exists(), true)
      const auto after_first = counting_store::counters::get().gets;
      CHECK_EQUAL(after_first, 1u)

      (void)c.get();
      (void)c.get_or_default(pod_state{});
      (void)c.exists();
      CHECK_EQUAL(counting_store::counters::get().gets, after_first)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// An absent row reads cleanly and still writes nothing.
SYSIO_TEST_BEGIN(cached_absent_row_reads_without_writing)
   counting_store::counters::reset();
   {
      counting_cache c;
      CHECK_EQUAL(c.exists(), false)
      CHECK_EQUAL(c.get_or_default(pod_state{5, 6}), (pod_state{5, 6}))
      CHECK_EQUAL(counting_store::counters::get().gets, 1u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// Many mutations collapse into exactly one write, carrying the final value and last payer.
SYSIO_TEST_BEGIN(cached_modify_defers_single_write)
   counting_store::counters::seed(pod_state{10, 0});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter += 5; });
      c.modify(payer_b, [](pod_state& s) { s.counter += 2; s.flags = 42; });
      CHECK_EQUAL(c.dirty(), true)
      // Nothing has reached the store yet.
      CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{17, 42}))
   CHECK_EQUAL(counting_store::counters::get().payer, payer_b)
SYSIO_TEST_END

/// A mutation is visible to later reads on the same handle without re-reading the store.
SYSIO_TEST_BEGIN(cached_reads_see_pending_mutation)
   counting_store::counters::seed(pod_state{4, 0});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 88; });
      const auto gets_before = counting_store::counters::get().gets;
      CHECK_EQUAL(c.get(), (pod_state{88, 0}))
      CHECK_EQUAL(counting_store::counters::get().gets, gets_before)
   }
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{88, 0}))
SYSIO_TEST_END

/// set() creates an absent row, still deferred.
SYSIO_TEST_BEGIN(cached_set_creates_and_defers)
   counting_store::counters::reset();
   {
      counting_cache c;
      c.set(pod_state{3, 9}, payer_a);
      CHECK_EQUAL(c.exists(), true)
      CHECK_EQUAL(c.get(), (pod_state{3, 9}))
      CHECK_EQUAL(counting_store::counters::get().sets, 0u)
      // set() seeds the cache outright, so it must not need a store read at all.
      CHECK_EQUAL(counting_store::counters::get().gets, 0u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{3, 9}))
SYSIO_TEST_END

/// upsert() seeds from the default when absent, then applies the mutation.
SYSIO_TEST_BEGIN(cached_upsert_creates_from_default)
   counting_store::counters::reset();
   {
      counting_cache c;
      c.upsert(payer_a, pod_state{100, 1}, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{101, 1}))
SYSIO_TEST_END

/// upsert() on an existing row ignores the default and mutates what is stored.
SYSIO_TEST_BEGIN(cached_upsert_modifies_existing)
   counting_store::counters::seed(pod_state{50, 7});
   {
      counting_cache c;
      c.upsert(payer_a, pod_state{100, 1}, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{51, 7}))
SYSIO_TEST_END

/// remove() discards a pending write rather than writing then erasing.
SYSIO_TEST_BEGIN(cached_remove_cancels_pending_write)
   counting_store::counters::seed(pod_state{1, 1});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 999; });
      c.remove();
      CHECK_EQUAL(c.exists(), false)
      CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
   CHECK_EQUAL(counting_store::counters::get().present, false)
SYSIO_TEST_END

/// A bare remove() on an untouched handle defers too.
SYSIO_TEST_BEGIN(cached_remove_defers)
   counting_store::counters::seed(pod_state{1, 1});
   {
      counting_cache c;
      c.remove();
      CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
SYSIO_TEST_END

/// flush() applies the change once; a second flush and the destructor must not repeat it.
SYSIO_TEST_BEGIN(cached_flush_is_idempotent)
   counting_store::counters::seed(pod_state{2, 2});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 20; });
      c.flush();
      CHECK_EQUAL(counting_store::counters::get().sets, 1u)
      CHECK_EQUAL(c.dirty(), false)
      c.flush();
      CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   }   // destructor must not write a third time
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
SYSIO_TEST_END

/// A mutation after an explicit flush is a fresh debt and gets its own write.
SYSIO_TEST_BEGIN(cached_modify_after_flush_writes_again)
   counting_store::counters::seed(pod_state{0, 0});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 1; });
      c.flush();
      c.modify(payer_a, [](pod_state& s) { s.counter = 2; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 2u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{2, 0}))
SYSIO_TEST_END

/// modify() refuses to invent a row -- upsert() is the creating form.
SYSIO_TEST_BEGIN(cached_modify_absent_asserts)
   counting_store::counters::reset();
   CHECK_ASSERT("singleton does not exist", ([]() {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 1; });
   }))
SYSIO_TEST_END

/// get() on an absent row asserts, with the caller's message when supplied.
SYSIO_TEST_BEGIN(cached_get_absent_asserts)
   counting_store::counters::reset();
   CHECK_ASSERT("singleton does not exist", ([]() {
      counting_cache c;
      (void)c.get();
   }))
   CHECK_ASSERT("config not initialized", ([]() {
      counting_cache c;
      (void)c.get("config not initialized");
   }))
SYSIO_TEST_END

/// Mutating a removed handle is a caller bug, not a silent resurrection.
SYSIO_TEST_BEGIN(cached_mutate_after_remove_asserts)
   counting_store::counters::seed(pod_state{1, 1});
   CHECK_ASSERT("singleton mutated after remove()", ([]() {
      counting_cache c;
      c.remove();
      c.upsert(payer_a, pod_state{}, [](pod_state& s) { s.counter = 1; });
   }))
SYSIO_TEST_END

// ===========================================================================
// Group 2 -- kv::cached_global over the real kv::global
// ===========================================================================

/// The read-only regression, end to end: seed a row, read it through cached_global, let the
/// handle destruct, and require that no kv_set ever happened. This is precisely the sequence
/// that fails on chain today when a contract writes its singleton from the destructor.
SYSIO_TEST_BEGIN(cached_global_read_issues_no_write)
   begin_kv_case();
   pod_global(test_code).set(pod_state{11, 22}, payer_a);
   const auto sets_after_seed = mock_store().sets;
   CHECK_EQUAL(sets_after_seed, 1u)

   {
      pod_cached c(test_code);
      CHECK_EQUAL(c.exists(), true)
      CHECK_EQUAL(c.get(), (pod_state{11, 22}))
      CHECK_EQUAL(c.get_or_default(pod_state{}), (pod_state{11, 22}))
   }
   CHECK_EQUAL(mock_store().sets, sets_after_seed)    // no write at destruction
   CHECK_EQUAL(mock_store().erases, 0u)
   // cached_value loads via try_get, so it never pays for a separate kv_contains probe.
   CHECK_EQUAL(mock_store().contains, 0u)
SYSIO_TEST_END

/// A deferred write lands exactly once and is visible to an independent kv::global handle.
SYSIO_TEST_BEGIN(cached_global_deferred_write_roundtrip)
   begin_kv_case();
   pod_global(test_code).set(pod_state{1, 0}, payer_a);
   const auto sets_after_seed = mock_store().sets;

   {
      pod_cached c(test_code);
      c.modify(payer_b, [](pod_state& s) { s.counter = 5; });
      c.modify(payer_b, [](pod_state& s) { s.flags = 6; });
      CHECK_EQUAL(mock_store().sets, sets_after_seed)          // still nothing written
   }
   CHECK_EQUAL(mock_store().sets, sets_after_seed + 1)         // exactly one write
   CHECK_EQUAL(mock_store().last_payer, payer_b.value)         // payer forwarded to kv_set
   CHECK_EQUAL(pod_global(test_code).get(), (pod_state{5, 6}))
SYSIO_TEST_END

/// set() through the cache creates a row that was never there.
SYSIO_TEST_BEGIN(cached_global_set_creates_row)
   begin_kv_case();
   {
      pod_cached c(test_code);
      CHECK_EQUAL(c.exists(), false)
      c.set(pod_state{77, 88}, payer_a);
      CHECK_EQUAL(mock_store().sets, 0u)
   }
   CHECK_EQUAL(mock_store().sets, 1u)
   CHECK_EQUAL(pod_global(test_code).exists(), true)
   CHECK_EQUAL(pod_global(test_code).get(), (pod_state{77, 88}))
SYSIO_TEST_END

/// remove() through the cache erases the row, deferred like every other change.
SYSIO_TEST_BEGIN(cached_global_remove_erases_row)
   begin_kv_case();
   pod_global(test_code).set(pod_state{1, 2}, payer_a);
   {
      pod_cached c(test_code);
      c.remove();
      CHECK_EQUAL(mock_store().erases, 0u)
   }
   CHECK_EQUAL(mock_store().erases, 1u)
   CHECK_EQUAL(pod_global(test_code).exists(), false)
SYSIO_TEST_END

/// The packed (non-fixed-serializable) path, including a payload big enough to force
/// kv::global's heap re-read. Reads must still issue no write.
SYSIO_TEST_BEGIN(cached_global_blob_roundtrip)
   begin_kv_case();
   const auto seeded = make_big_blob(1);
   CHECK_EQUAL(sysio::pack_size(seeded) > sysio::kv::kv_value_stack_size, true)

   blob_global(test_code).set(seeded, payer_a);
   const auto sets_after_seed = mock_store().sets;

   {
      blob_cached c(test_code);
      CHECK_EQUAL(c.get(), seeded)
      const auto gets_after_load = mock_store().gets;
      (void)c.get();                                   // cached: no further store reads
      CHECK_EQUAL(mock_store().gets, gets_after_load)
   }
   CHECK_EQUAL(mock_store().sets, sets_after_seed)             // read-only: still no write

   {
      blob_cached c(test_code);
      c.modify(payer_b, [](blob_state& b) { b.values.push_back(4242); });
   }
   CHECK_EQUAL(mock_store().sets, sets_after_seed + 1)
   auto expected = seeded;
   expected.values.push_back(4242);
   CHECK_EQUAL(blob_global(test_code).get(), expected)
SYSIO_TEST_END

/// upsert() creates through the real store when the row is absent.
SYSIO_TEST_BEGIN(cached_global_upsert_creates_row)
   begin_kv_case();
   {
      pod_cached c(test_code);
      c.upsert(payer_a, pod_state{9, 9}, [](pod_state& s) { s.counter += 1; });
      CHECK_EQUAL(mock_store().sets, 0u)
   }
   CHECK_EQUAL(mock_store().sets, 1u)
   CHECK_EQUAL(pod_global(test_code).get(), (pod_state{10, 9}))
SYSIO_TEST_END

/// Two sequential handles behave like two sequential actions: the first flush is visible to
/// the second handle.
SYSIO_TEST_BEGIN(cached_global_sequential_handles_observe_flush)
   begin_kv_case();
   pod_global(test_code).set(pod_state{0, 0}, payer_a);
   {
      pod_cached c(test_code);
      c.modify(payer_a, [](pod_state& s) { s.counter = 1; });
   }
   {
      pod_cached c(test_code);
      CHECK_EQUAL(c.get(), (pod_state{1, 0}))
      c.modify(payer_a, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(pod_global(test_code).get(), (pod_state{2, 0}))
SYSIO_TEST_END

// ===========================================================================
// Group 3 -- Store conformance
// ===========================================================================

static_assert(std::is_same_v<pod_cached::value_type, pod_state>,
              "cached_global must expose the store's payload type");
static_assert(!std::is_copy_constructible_v<pod_cached>,
              "cached_value must not be copyable -- two handles would own conflicting writes");

// The scoped sysio::cached_kv_singleton is covered by the kv_singleton_tests WASM contract
// instead of here: kv_singleton.hpp pulls in contracts/sysio/system.hpp, whose
// is_feature_activated declaration conflicts with the C-API one the native tester already
// declares, so the scoped singleton cannot be included in a native unit test at all.

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(cached_read_never_writes)
   SYSIO_TEST(cached_load_happens_once)
   SYSIO_TEST(cached_absent_row_reads_without_writing)
   SYSIO_TEST(cached_modify_defers_single_write)
   SYSIO_TEST(cached_reads_see_pending_mutation)
   SYSIO_TEST(cached_set_creates_and_defers)
   SYSIO_TEST(cached_upsert_creates_from_default)
   SYSIO_TEST(cached_upsert_modifies_existing)
   SYSIO_TEST(cached_remove_cancels_pending_write)
   SYSIO_TEST(cached_remove_defers)
   SYSIO_TEST(cached_flush_is_idempotent)
   SYSIO_TEST(cached_modify_after_flush_writes_again)
   SYSIO_TEST(cached_modify_absent_asserts)
   SYSIO_TEST(cached_get_absent_asserts)
   SYSIO_TEST(cached_mutate_after_remove_asserts)

   SYSIO_TEST(cached_global_read_issues_no_write)
   SYSIO_TEST(cached_global_deferred_write_roundtrip)
   SYSIO_TEST(cached_global_set_creates_row)
   SYSIO_TEST(cached_global_remove_erases_row)
   SYSIO_TEST(cached_global_blob_roundtrip)
   SYSIO_TEST(cached_global_upsert_creates_row)
   SYSIO_TEST(cached_global_sequential_handles_observe_flush)

   return has_failed();
}
