/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Coverage for sysio::kv::cached_value and its kv::cached_global alias (kv_cached.hpp). The
 *  scoped sysio::cached_kv_singleton alias is covered on chain by kv_cached_contract, driven from
 *  tests/integration/kv_cached_tests.cpp -- see the note above main().
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

/// Default provider for the defaults cases.
///
/// Counts its invocations, because the property under test is not only "an absent row reads as the
/// default" but "the provider is consulted lazily and at most once" -- that is what keeps an action
/// which never touches the singleton from paying for it being a member.
struct default_probe {
   static constexpr pod_state value{111, 222};

   static uint32_t& calls() {
      static uint32_t n = 0;
      return n;
   }
   static void reset() { calls() = 0; }

   static pod_state make() {
      ++calls();
      return value;
   }
};

/// Same store, but the type carries defaults. Contrast with counting_cache, which does not.
using defaulted_cache = sysio::kv::cached_value<counting_store, &default_probe::make>;

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
         auto k = mock_kv::row_key{m.receiver, table_id, as_key(key, key_size)};
         // apply_context::kv_set asserts a valid payer on the CREATE branch; an update may legally
         // carry payer 0 (kv::same_payer), which means "keep billing whoever owns the row". Without
         // this guard the mock silently accepts a malformed create that the chain would reject.
         if (m.rows.find(k) == m.rows.end())
            sysio::check(payer != 0, "must specify a valid account to pay for new record");
         m.rows[k] = std::string(static_cast<const char*>(value), value_size);
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
         auto k = mock_kv::row_key{m.receiver, table_id, as_key(key, key_size)};
         // apply_context::kv_erase asserts the row exists. Issuing an erase for an absent row is a
         // real defect -- it aborts the transaction on chain -- so the mock must not absorb it.
         sysio::check(m.rows.find(k) != m.rows.end(), "Key not found in `kv_erase`");
         m.rows.erase(k);
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
/// Same table, but the type carries defaults -- for the remove() cases, where what a handle owes
/// the store differs between the two.
using pod_cached_defaulted = sysio::kv::cached_global<"cfg"_n, pod_state, &default_probe::make>;
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

/// A handle constructed with defaults costs nothing until something asks for the value.
///
/// This is the reason defaults belong on the handle rather than in a seed call: seeding from a
/// contract constructor reads the store on EVERY action, and an eagerly evaluated default argument
/// also runs whatever host calls the default itself needs -- on actions that never look at the
/// singleton. Here an untouched handle must issue no store read and never call the provider.
SYSIO_TEST_BEGIN(cached_defaults_cost_nothing_until_used)
   counting_store::counters::reset();
   default_probe::reset();
   {
      defaulted_cache c;
      // deliberately no get()/exists()/modify()
   }
   CHECK_EQUAL(counting_store::counters::get().gets, 0u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(default_probe::calls(), 0u)
SYSIO_TEST_END

/// An absent row reads as the default, and doing so owes NO write.
///
/// The distinction this pins down is the one that made read-only view actions fail: seeding through
/// set() would leave the handle dirty, so the value would be flushed from every action including a
/// pure query. Materializing the default must leave dirty() false and produce no set at destruction.
SYSIO_TEST_BEGIN(cached_defaults_materialize_without_owing_a_write)
   counting_store::counters::reset();
   default_probe::reset();
   {
      defaulted_cache c;
      CHECK_EQUAL(c.exists(), true)                    // a value is available...
      CHECK_EQUAL(c.get(), default_probe::value)
      CHECK_EQUAL(c.dirty(), false)                    // ...but nothing is owed
      CHECK_EQUAL(counting_store::counters::get().gets, 1u)
      CHECK_EQUAL(default_probe::calls(), 1u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)   // still never stored
SYSIO_TEST_END

/// A stored row wins over the default, and the provider is never called.
SYSIO_TEST_BEGIN(cached_defaults_yield_to_a_stored_row)
   counting_store::counters::seed(pod_state{7, 3});
   default_probe::reset();
   {
      defaulted_cache c;
      CHECK_EQUAL(c.get(), (pod_state{7, 3}))
      CHECK_EQUAL(c.dirty(), false)
   }
   CHECK_EQUAL(default_probe::calls(), 0u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// Repeated reads consult the provider once, as load() runs once.
SYSIO_TEST_BEGIN(cached_defaults_provider_runs_at_most_once)
   counting_store::counters::reset();
   default_probe::reset();
   {
      defaulted_cache c;
      // get_or_default is ill-formed on a defaulted handle (see the static_assert in
      // kv_cached.hpp), so exists()/get() are the read paths available here.
      (void)c.exists();
      (void)c.get();
      (void)c.get();
      (void)c.get();
      CHECK_EQUAL(counting_store::counters::get().gets, 1u)
      CHECK_EQUAL(default_probe::calls(), 1u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// The first genuine mutation is what persists the row, carrying defaults plus the change.
SYSIO_TEST_BEGIN(cached_defaults_reach_storage_on_first_mutation)
   counting_store::counters::reset();
   default_probe::reset();
   {
      defaulted_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 5; });
      CHECK_EQUAL(c.dirty(), true)
      CHECK_EQUAL(counting_store::counters::get().sets, 0u)   // still deferred
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   // counter carries the mutation; flags carries the seeded default, proving the write is not a
   // zero-initialized struct with the change laid on top.
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{5, default_probe::value.flags}))
   CHECK_EQUAL(counting_store::counters::get().payer, payer_a)
SYSIO_TEST_END

/// modify() on an absent row asserts without defaults, but a defaulted handle has a value to
/// modify -- so the two constructors genuinely differ in behaviour, not just in cost.
SYSIO_TEST_BEGIN(cached_defaults_make_modify_legal_on_an_absent_row)
   counting_store::counters::reset();
   default_probe::reset();
   {
      counting_cache plain;
      CHECK_EQUAL(plain.exists(), false)
   }
   {
      defaulted_cache c;
      CHECK_EQUAL(c.exists(), true)
      c.modify(payer_a, [](pod_state& s) { s.flags = 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
SYSIO_TEST_END

/// remove() on a defaulted handle whose row was never written owes the store NOTHING.
///
/// The trap is that exists() is true here -- the default is available -- so deciding the erase off
/// it would send Store::remove() after a row nobody ever wrote, and leave dirty() claiming a debt
/// that does not exist. What the erase must key on is whether a row is STORED, which is a different
/// question the moment defaults enter. Being inert also makes the call safe inside a read-only
/// transaction, exactly as it is on a handle without defaults.
SYSIO_TEST_BEGIN(cached_defaults_remove_absent_owes_nothing)
   counting_store::counters::reset();
   default_probe::reset();
   {
      defaulted_cache c;
      CHECK_EQUAL(c.get(), default_probe::value)
      c.remove();
      c.remove();                                   // idempotent, and no second provider call
      CHECK_EQUAL(c.dirty(), false)                 // nothing stored, so nothing owed
      CHECK_EQUAL(c.exists(), true)                 // the default is still available...
      CHECK_EQUAL(c.get(), default_probe::value)    // ...and still what reads see
      CHECK_EQUAL(default_probe::calls(), 1u)       // load() consulted it; remove() had no need to
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)
SYSIO_TEST_END

/// remove() on a defaulted handle holding a STORED row erases it and resets to the default.
///
/// The erase is owed, because a row really is there. What differs from a handle without defaults is
/// what the handle reads as afterwards: the type promises get() never asserts for an unwritten row,
/// and a removed row IS unwritten, so the value returns to the default rather than the handle going
/// absent. The provider is consulted once, by the reset.
SYSIO_TEST_BEGIN(cached_defaults_remove_resets_a_stored_row_to_the_default)
   counting_store::counters::seed(pod_state{7, 3});
   default_probe::reset();
   {
      defaulted_cache c;
      CHECK_EQUAL(c.get(), (pod_state{7, 3}))       // the stored row wins while it is there
      CHECK_EQUAL(default_probe::calls(), 0u)       // ...so the provider was never needed
      c.remove();
      CHECK_EQUAL(c.dirty(), true)                  // the erase is owed
      CHECK_EQUAL(c.exists(), true)                 // a value is still available
      CHECK_EQUAL(c.get(), default_probe::value)    // reset: reads as an unwritten row does
      CHECK_EQUAL(counting_store::counters::get().removes, 0u)   // still deferred
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)
   CHECK_EQUAL(default_probe::calls(), 1u)
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

/// modify_or_create() seeds from the default when absent, then applies the mutation.
SYSIO_TEST_BEGIN(cached_modify_or_create_seeds_default)
   counting_store::counters::reset();
   {
      counting_cache c;
      c.modify_or_create(payer_a, pod_state{100, 1}, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val, (pod_state{101, 1}))
SYSIO_TEST_END

/// modify_or_create() on an existing row ignores the default and mutates what is stored.
SYSIO_TEST_BEGIN(cached_modify_or_create_mutates_existing)
   counting_store::counters::seed(pod_state{50, 7});
   {
      counting_cache c;
      c.modify_or_create(payer_a, pod_state{100, 1}, [](pod_state& s) { s.counter += 1; });
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

/// remove() is idempotent. The second call finds the row already absent per the cache -- absent
/// BECAUSE of the first remove() -- and must keep the erase it owes instead of reading that state
/// as "nothing was ever here" and cancelling it, which would leave the stored row untouched while
/// the handle went on reporting it gone.
SYSIO_TEST_BEGIN(cached_remove_is_idempotent)
   counting_store::counters::seed(pod_state{4, 5});
   {
      counting_cache c;
      c.remove();
      c.remove();
      CHECK_EQUAL(c.dirty(), true)
      CHECK_EQUAL(c.exists(), false)
      CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)

   // Same rule when the first remove() also had a pending write to discard: the write stays
   // cancelled and the erase still survives the second call.
   counting_store::counters::seed(pod_state{6, 7});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 999; });
      c.remove();
      c.remove();
      CHECK_EQUAL(c.dirty(), true)
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)
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

// NOTE for every CHECK_ASSERT case below: the native tester implements sysio_assert with longjmp,
// so the handle's destructor -- and therefore its flush() -- does NOT run when the assert fires.
// Each case must assert the store counters itself; without that it verifies only the message text,
// and an implementation that dropped or duplicated the pending change would pass unchanged.

/// modify() refuses to invent a row -- modify_or_create() is the creating form.
SYSIO_TEST_BEGIN(cached_modify_absent_asserts)
   counting_store::counters::reset();
   CHECK_ASSERT("singleton does not exist", ([]() {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 1; });
   }))
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)
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
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
SYSIO_TEST_END

/// Mutating a removed handle is a caller bug, not a silent resurrection.
SYSIO_TEST_BEGIN(cached_mutate_after_remove_asserts)
   counting_store::counters::seed(pod_state{1, 1});
   CHECK_ASSERT("singleton mutated after remove()", ([]() {
      counting_cache c;
      c.remove();
      c.modify_or_create(payer_a, pod_state{}, [](pod_state& s) { s.counter = 1; });
   }))
   // The rejected call must not have written, and because the longjmp skipped the destructor the
   // pending erase was never committed either -- the seeded row is still there, untouched.
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, true)
   CHECK_EQUAL(counting_store::counters::get().val.counter, 1u)

   // modify() after remove() reports the erase, not the generic "does not exist".
   counting_store::counters::seed(pod_state{1, 1});
   CHECK_ASSERT("singleton mutated after remove()", ([]() {
      counting_cache c;
      c.remove();
      c.modify(payer_a, [](pod_state& s) { s.counter = 2; });
   }))

   // A callback that removes the row through the same handle must not have its erase silently
   // converted back into a write of the value it just retired.
   counting_store::counters::seed(pod_state{1, 1});
   CHECK_ASSERT("singleton removed from inside a mutation callback", ([]() {
      counting_cache c;
      c.modify(payer_a, [&c](pod_state& s) { s.counter = 9; c.remove(); });
   }))
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// remove() on a row that is not there owes nothing, so an action that only erases an absent
/// singleton issues no host write and stays legal inside a read-only transaction. Leaving it to
/// the store's own short-circuit would make that legality depend on chain data instead.
SYSIO_TEST_BEGIN(cached_remove_absent_is_noop)
   counting_store::counters::reset();
   {
      counting_cache c;
      c.remove();
      CHECK_EQUAL(c.dirty(), false)
      CHECK_EQUAL(c.exists(), false)
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 0u)
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
SYSIO_TEST_END

/// same_payer must not overwrite a real payer recorded earlier in the same action. The deferred
/// write coalesces both calls into one store(), and a create billed to payer 0 is rejected on chain.
SYSIO_TEST_BEGIN(cached_same_payer_preserves_recorded_payer)
   constexpr sysio::name same_payer{};
   counting_store::counters::reset();
   {
      counting_cache c;
      c.set(pod_state{1, 1}, payer_a);
      c.modify(same_payer, [](pod_state& s) { s.counter = 2; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().payer, payer_a)

   // With no real payer anywhere in the action, same_payer is passed through untouched so the
   // host can keep billing whoever owns the row.
   counting_store::counters::seed(pod_state{5, 5});
   {
      counting_cache c;
      c.modify(same_payer, [](pod_state& s) { s.counter = 6; });
   }
   CHECK_EQUAL(counting_store::counters::get().payer, same_payer)

   // A later real payer still wins over an earlier one.
   counting_store::counters::seed(pod_state{5, 5});
   {
      counting_cache c;
      c.modify(payer_a, [](pod_state& s) { s.counter = 6; });
      c.modify(payer_b, [](pod_state& s) { s.counter = 7; });
   }
   CHECK_EQUAL(counting_store::counters::get().payer, payer_b)

   // set() obeys the same rule as modify(). Uncached, set(v, payer_a) then set(w, same_payer) is
   // two writes -- a create billed to payer_a, then an update that legally carries same_payer. The
   // cache coalesces them into ONE write, so letting same_payer through would bill a CREATE to
   // payer 0, which the host rejects outright.
   counting_store::counters::reset();
   {
      counting_cache c;
      c.set(pod_state{1, 1}, payer_a);
      c.set(pod_state{2, 2}, same_payer);
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().payer, payer_a)
   CHECK_EQUAL(counting_store::counters::get().val.counter, 2u)
SYSIO_TEST_END

/// seed_if_absent supplies defaults for reading WITHOUT owing a write -- the property that lets a
/// contract materialize defaults in its constructor and still serve read-only queries.
SYSIO_TEST_BEGIN(cached_seed_if_absent_does_not_dirty)
   counting_store::counters::reset();
   {
      counting_cache c;
      c.seed_if_absent(pod_state{42, 7});
      CHECK_EQUAL(c.dirty(), false)
      CHECK_EQUAL(c.exists(), true)
      CHECK_EQUAL(c.get().counter, 42u)
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 0u)
   CHECK_EQUAL(counting_store::counters::get().present, false)

   // On an existing row the seed is ignored, and a later mutation writes the STORED value plus the
   // change -- not the default.
   counting_store::counters::seed(pod_state{5, 5});
   {
      counting_cache c;
      c.seed_if_absent(pod_state{42, 7});
      CHECK_EQUAL(c.get().counter, 5u)
      c.modify(payer_a, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().val.counter, 6u)

   // Seeded defaults do reach storage once something genuinely mutates.
   counting_store::counters::reset();
   {
      counting_cache c;
      c.seed_if_absent(pod_state{42, 7});
      c.modify(payer_a, [](pod_state& s) { s.counter += 1; });
   }
   CHECK_EQUAL(counting_store::counters::get().sets, 1u)
   CHECK_EQUAL(counting_store::counters::get().val.counter, 43u)
   CHECK_EQUAL(counting_store::counters::get().val.flags, 7u)
SYSIO_TEST_END

/// A reference handed out by get() must stay valid across a remove(): the row is gone but the
/// cached object is not destroyed, so the caller's reference does not dangle.
SYSIO_TEST_BEGIN(cached_reference_survives_remove)
   counting_store::counters::seed(pod_state{11, 3});
   {
      counting_cache c;
      const pod_state& ref = c.get();
      c.remove();
      CHECK_EQUAL(c.exists(), false)
      CHECK_EQUAL(ref.counter, 11u)      // reading through the reference is still defined
      CHECK_EQUAL(ref.flags, 3u)
   }
   CHECK_EQUAL(counting_store::counters::get().removes, 1u)
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
/// The same sequence against the real kv::global and the mocked host. This is the end-to-end form
/// of the payer rule: the mock enforces apply_context's "must specify a valid account to pay for new
/// record" on the create branch, so if the coalesced write carried same_payer through, this case
/// would abort inside the intrinsic rather than merely record the wrong payer.
SYSIO_TEST_BEGIN(cached_global_set_then_same_payer_set_creates_row)
   constexpr sysio::name same_payer{};
   begin_kv_case();
   {
      pod_cached c(test_code);
      CHECK_EQUAL(c.exists(), false)
      c.set(pod_state{1, 1}, payer_a);       // would CREATE the row
      c.set(pod_state{2, 2}, same_payer);    // legal uncached: the second write is an update
      CHECK_EQUAL(mock_store().sets, 0u)
   }
   CHECK_EQUAL(mock_store().sets, 1u)
   CHECK_EQUAL(mock_store().last_payer, payer_a.value)
   CHECK_EQUAL(pod_global(test_code).get(), (pod_state{2, 2}))
SYSIO_TEST_END

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

/// The double-remove case driven through the real kv::global and the mocked host. This is the
/// sharper of the two: it asserts the row is actually gone from storage rather than merely reported
/// gone by the handle, which is exactly what a dropped erase would get wrong.
SYSIO_TEST_BEGIN(cached_global_double_remove_erases_row)
   begin_kv_case();
   pod_global(test_code).set(pod_state{3, 4}, payer_a);
   {
      pod_cached c(test_code);
      c.remove();
      c.remove();
      CHECK_EQUAL(mock_store().erases, 0u)
   }
   CHECK_EQUAL(mock_store().erases, 1u)
   CHECK_EQUAL(pod_global(test_code).exists(), false)
SYSIO_TEST_END

/// The defaulted remove-absent case driven through the real kv::global and the mocked host.
///
/// Sharper than the counting_store form, and for a specific reason: kv::global::remove() probes with
/// kv_contains before kv_erase, so a Store::remove() that should never have been issued is absorbed
/// there and leaves the erase counter at zero either way. The kv_contains it costs is what makes the
/// wrong call visible at all -- which is also the whole point about store conformance, since the
/// documented Store contract promises no such probe and the chain's own kv_erase aborts on a missing
/// key.
SYSIO_TEST_BEGIN(cached_global_defaults_remove_absent_touches_no_store)
   begin_kv_case();
   default_probe::reset();
   {
      pod_cached_defaulted c(test_code);
      CHECK_EQUAL(c.get(), default_probe::value)
      c.remove();
      CHECK_EQUAL(c.dirty(), false)
   }
   CHECK_EQUAL(mock_store().contains, 0u)     // Store::remove() was never called
   CHECK_EQUAL(mock_store().erases, 0u)
   CHECK_EQUAL(mock_store().sets, 0u)
   CHECK_EQUAL(pod_global(test_code).exists(), false)
SYSIO_TEST_END

/// A defaulted handle still erases a row that IS stored, and reads as the default afterwards.
SYSIO_TEST_BEGIN(cached_global_defaults_remove_erases_a_stored_row)
   begin_kv_case();
   pod_global(test_code).set(pod_state{11, 22}, payer_a);
   default_probe::reset();
   {
      pod_cached_defaulted c(test_code);
      CHECK_EQUAL(c.get(), (pod_state{11, 22}))
      c.remove();
      CHECK_EQUAL(c.get(), default_probe::value)
      CHECK_EQUAL(mock_store().erases, 0u)          // deferred
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

/// modify_or_create() creates through the real store when the row is absent.
SYSIO_TEST_BEGIN(cached_global_modify_or_create_creates_row)
   begin_kv_case();
   {
      pod_cached c(test_code);
      c.modify_or_create(payer_a, pod_state{9, 9}, [](pod_state& s) { s.counter += 1; });
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

/// A stored row whose size does not match the fixed-serializable payload must be rejected outright.
/// kv_get fills min(buffer, stored) bytes but reports the full stored size, so copying sizeof(T) out
/// of a short row splices whatever the contract's linear memory held into the payload. Deterministic
/// -- every node builds the same wrong value -- but it reads as authoritative stored data, so trap.
SYSIO_TEST_BEGIN(global_rejects_wrong_sized_row)
   begin_kv_case();
   {
      pod_global g{test_code};
      g.set(pod_state{1, 2}, payer_a);
   }
   // Shorten the stored row, as an incompatible earlier version of the payload would have left it.
   for (auto& row : mock_store().rows) row.second.resize(row.second.size() - 1);

   CHECK_ASSERT("kv::global: stored value size does not match the fixed-serializable payload", ([]() {
      pod_global g{test_code};
      pod_state  out;
      (void)g.try_get(out);
   }))

   // The cached wrapper loads through the same path, so it inherits the rejection.
   CHECK_ASSERT("kv::global: stored value size does not match the fixed-serializable payload", ([]() {
      pod_cached c{test_code};
      (void)c.exists();
   }))
SYSIO_TEST_END

static_assert(std::is_same_v<pod_cached::value_type, pod_state>,
              "cached_global must expose the store's payload type");
static_assert(!std::is_copy_constructible_v<pod_cached>,
              "cached_value must not be copyable -- two handles would own conflicting writes");

// The scoped sysio::cached_kv_singleton is exercised by kv_cached_contract, driven from
// tests/integration/kv_cached_tests.cpp -- NOT by kv_singleton_tests, which predates this feature
// and drives sysio::singleton.
//
// It is not covered here because kv_singleton is backed by kv_multi_index, whose find() needs the
// kv_it_* iterator intrinsics on top of the four this file mocks. (Including the header natively is
// no longer the obstacle: kv_singleton.hpp used to pull in contracts/sysio/system.hpp, whose
// is_feature_activated declaration collides with the C-API one the native tester declares, and that
// include has been removed as unused.) Extending the mock with a positioned iterator would bring
// the scoped alias into this natively-run suite, which matters because the integration suite is
// gated behind ENABLE_INTEGRATION_TESTS and does not run in CI.

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   SYSIO_TEST(cached_read_never_writes)
   SYSIO_TEST(cached_load_happens_once)
   SYSIO_TEST(cached_absent_row_reads_without_writing)
   SYSIO_TEST(cached_defaults_cost_nothing_until_used)
   SYSIO_TEST(cached_defaults_materialize_without_owing_a_write)
   SYSIO_TEST(cached_defaults_yield_to_a_stored_row)
   SYSIO_TEST(cached_defaults_provider_runs_at_most_once)
   SYSIO_TEST(cached_defaults_reach_storage_on_first_mutation)
   SYSIO_TEST(cached_defaults_make_modify_legal_on_an_absent_row)
   SYSIO_TEST(cached_defaults_remove_absent_owes_nothing)
   SYSIO_TEST(cached_defaults_remove_resets_a_stored_row_to_the_default)
   SYSIO_TEST(cached_modify_defers_single_write)
   SYSIO_TEST(cached_reads_see_pending_mutation)
   SYSIO_TEST(cached_set_creates_and_defers)
   SYSIO_TEST(cached_modify_or_create_seeds_default)
   SYSIO_TEST(cached_modify_or_create_mutates_existing)
   SYSIO_TEST(cached_remove_cancels_pending_write)
   SYSIO_TEST(cached_remove_defers)
   SYSIO_TEST(cached_remove_is_idempotent)
   SYSIO_TEST(cached_flush_is_idempotent)
   SYSIO_TEST(cached_modify_after_flush_writes_again)
   SYSIO_TEST(cached_modify_absent_asserts)
   SYSIO_TEST(cached_get_absent_asserts)
   SYSIO_TEST(cached_mutate_after_remove_asserts)
   SYSIO_TEST(cached_remove_absent_is_noop)
   SYSIO_TEST(cached_same_payer_preserves_recorded_payer)
   SYSIO_TEST(cached_seed_if_absent_does_not_dirty)
   SYSIO_TEST(cached_reference_survives_remove)

   SYSIO_TEST(cached_global_read_issues_no_write)
   SYSIO_TEST(cached_global_deferred_write_roundtrip)
   SYSIO_TEST(cached_global_set_creates_row)
   SYSIO_TEST(cached_global_set_then_same_payer_set_creates_row)
   SYSIO_TEST(cached_global_remove_erases_row)
   SYSIO_TEST(cached_global_double_remove_erases_row)
   SYSIO_TEST(cached_global_defaults_remove_absent_touches_no_store)
   SYSIO_TEST(cached_global_defaults_remove_erases_a_stored_row)
   SYSIO_TEST(cached_global_blob_roundtrip)
   SYSIO_TEST(cached_global_modify_or_create_creates_row)
   SYSIO_TEST(cached_global_sequential_handles_observe_flush)
   SYSIO_TEST(global_rejects_wrong_sized_row)

   return has_failed();
}
