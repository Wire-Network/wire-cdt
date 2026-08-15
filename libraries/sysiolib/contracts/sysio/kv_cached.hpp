#pragma once
/**
 * sysio::kv::cached_value -- write-deferring cache over a KV singleton store.
 *
 * Loads the stored value at most once and serves every read from that cache. Each pending change
 * is written back exactly once, by flush() or by destruction, and only when a mutating call
 * actually ran -- so an action that only reads never writes. Note that flush() RE-ARMS the handle:
 * it commits the pending change and clears it, so a later mutation owes a second write. N explicit
 * flush/mutate cycles therefore produce N writes; leaving the commit to the destructor produces
 * exactly one per action.
 *
 * WHY THIS EXISTS
 *
 * The classic singleton idiom caches state in a contract member and persists it from the
 * contract destructor:
 *
 *    struct my_contract : sysio::contract {
 *       my_contract(...) { _gstate = _global.get(); }
 *       ~my_contract()   { _global.set(_gstate, get_self()); }   // unconditional write
 *    };
 *
 * That destructor runs after EVERY action, because the generated dispatcher instantiates
 * the contract as a temporary and destroys it as soon as the action returns. So every
 * action -- including one that only reads -- issues a kv_set, and the chain rejects any
 * write inside a read-only transaction:
 *
 *    cannot store a KV record when executing a readonly transaction
 *
 * The failure is confusing because it surfaces only on the SUCCESS path: an action that
 * aborts early via sysio::check traps before the destructor runs and reports its own
 * error, while an action that returns normally dies in the destructor, making it look
 * like the return path is at fault.
 *
 * cached_value removes the hazard by construction rather than by remembering to guard: an
 * action that only reads never dirties the cache, so it never issues a kv_set, so it is
 * legal inside a read-only transaction.
 *
 * STORE REQUIREMENTS
 *
 * Any singleton-shaped store works. It must expose:
 *
 *    using value_type = T;
 *    bool try_get(value_type& out) const;      // false when absent
 *    void set(const value_type& val, name payer);
 *    void remove();
 *
 * Both sysio::kv::global (unscoped) and sysio::kv_singleton (scoped) satisfy this; see
 * kv::cached_global and sysio::cached_kv_singleton for the ready-made aliases.
 *
 * VISIBILITY
 *
 * Nothing reaches the store before flush(). A second handle opened on the same table
 * during the same action therefore does NOT observe uncommitted changes -- exactly as a
 * contract-member cache behaves today. Keep one cached handle per table per action, and
 * do not mix it with direct writes to the same table. Inline actions are unaffected: they
 * are queued during apply() and executed after it returns, so the flush always lands
 * first.
 *
 * Usage:
 *    kv::cached_global<"global"_n, my_state> _global{get_self()};
 *
 *    const auto& s = _global.get();                    // read, no write
 *    _global.modify(get_self(), [](auto& s) {          // deferred
 *       s.counter++;
 *    });
 *    // written back exactly once, at destruction
 */

#include <sysio/check.hpp>
#include <sysio/name.hpp>

#include <cstdint>
#include <optional>
#include <type_traits>
#include <utility>

namespace sysio { namespace kv {

/**
 * Write-deferring cache over a singleton-shaped KV store.
 *
 * @tparam Store       backing store satisfying the requirements documented above.
 * @tparam MakeDefault supplies the value a never-written row reads as; nullptr for none.
 *
 * MakeDefault is a template parameter rather than a constructor argument so the call stays direct
 * and a handle without defaults compiles to exactly what it did before this parameter existed. It
 * also makes DEFAULTS A PROPERTY OF THE SINGLETON TYPE, which is where they belong: one singleton
 * cannot have two different notions of what an unwritten row means.
 *
 * WHY NOT SEED IN THE CONTRACT CONSTRUCTOR. That was the previous idiom, and it charges every
 * action for a singleton most actions never touch: it reads the row unconditionally, and an
 * eagerly-evaluated default argument also runs whatever host calls the default itself needs. Here
 * nothing happens until something asks for the value -- no store read, no provider call.
 *
 * WHAT DEFAULTS CHANGE. On a handle that carries them, exists() reports whether a VALUE IS
 * AVAILABLE, not whether a row is stored -- true even on a chain where nothing was ever written,
 * because the default is available. get() correspondingly never asserts. Three consequences worth
 * knowing:
 *
 *   - the old "create it if missing" idiom, `if (!h.exists()) h.set(defaults, payer);`, becomes a
 *     no-op rather than a hidden per-action write. That is the point: the idiom this parameter
 *     replaces cannot quietly come back.
 *   - a contract that genuinely needs "has anyone configured this yet" cannot ask this handle. Use
 *     a singleton without defaults for that question, or record it in the payload.
 *   - remove() reads as RESET TO DEFAULTS. It erases the stored row and the handle goes on serving
 *     what an unwritten row reads as, which is what keeps get()'s never-asserts property true after
 *     one. It owes the store nothing when no row was ever written.
 *
 * WHAT DEFAULTS TAKE AWAY. The three calls that accept an explicit default -- get_or_default(),
 * modify_or_create() and seed_if_absent() -- are ILL-FORMED on a handle carrying MakeDefault, and
 * say so through a static_assert naming the replacement. Each asks "what should this read as when
 * absent", which is the question MakeDefault has already answered on the type. Accepting both would
 * let one singleton hold two notions of what an unwritten row means, and the argument would lose
 * SILENTLY: the caller's default is simply discarded, and the resulting value is wrong rather than
 * rejected. A compile error is the only outcome that cannot be missed.
 *
 * Handles without defaults (MakeDefault = nullptr, the default) are entirely unaffected: exists()
 * keeps meaning "a row is stored", get() still asserts when absent, and all three calls above
 * behave exactly as before.
 */
template<typename Store, typename Store::value_type (*MakeDefault)() = nullptr>
class cached_value {
public:
   /// Payload type, taken from the backing store so it never has to be repeated.
   using value_type = typename Store::value_type;

private:
   using T = value_type;

   /// The single change owed to the store, applied by flush().
   enum class pending_op : uint8_t {
      none,    ///< nothing to write back
      write,   ///< _cache must be stored
      erase    ///< the row must be erased
   };

   /// Whether the STORE holds a row, as far as this handle knows.
   ///
   /// A separate question from both _present ("is a value available", which defaults make
   /// unconditionally true) and _pending ("what is owed", which is `none` when the removed row was
   /// never written). Only `present` justifies an erase.
   enum class row_state : uint8_t {
      unknown,   ///< set() skipped the store read; resolve_row_state() settles it on demand
      present,   ///< a row is stored
      absent     ///< no row is stored
   };

public:
   /**
    * Construct a handle. All arguments are forwarded to the backing store, so this works
    * over both the unscoped store (code) and the scoped one (code, scope).
    */
   /// The enable_if keeps this from outcompeting the deleted copy constructor when the
   /// argument is a non-const cached_value lvalue, so copying reports "deleted" rather than
   /// a confusing failure inside the store's constructor.
   template<typename... Args,
            typename = std::enable_if_t<
               !std::disjunction_v<std::is_same<std::decay_t<Args>, cached_value>...>>>
   explicit cached_value(Args&&... args) : _store(std::forward<Args>(args)...) {}

   /// Non-copyable: two handles on one table would each own a conflicting pending write.
   cached_value(const cached_value&)            = delete;
   cached_value& operator=(const cached_value&) = delete;

   /// Applies the pending change, if any.
   ~cached_value() { flush(); }

   /// True if a value is available, accounting for a pending set()/remove(). Without defaults that
   /// is "the row exists"; with them it is always true, because the default is available even when
   /// nothing was ever stored.
   bool exists() const {
      load();
      return _present;
   }

   /**
    * Cached value. Asserts if the row is absent.
    *
    * Returns a REFERENCE into the cache, where kv::global::get() and kv_singleton::get() both
    * return by value. Binding it (`const auto& s = h.get();`) therefore aliases the live cache
    * rather than taking a snapshot: a later modify()/set()/modify_or_create() through this handle
    * is visible through \p s. That is intentional -- copying a singleton payload on every read is
    * what this class exists to avoid -- but it means a reference held across a mutation reports
    * the new value, not the value that was read. Copy it if you need a snapshot. remove() keeps
    * the cached object alive precisely so an outstanding reference never dangles.
    *
    * @param msg assert message used when absent.
    */
   const T& get(const char* msg = "singleton does not exist") const {
      load();
      sysio::check(_present, msg);
      return *_cache;
   }

   /// Cached value, or \p def when absent. Never creates the row and never dirties.
   ///
   /// Unavailable on a handle carrying MakeDefault: there is no absent case left for \p def to
   /// answer, so every call would return the type's default and discard the argument in silence.
   T get_or_default(const T& def) const {
      static_assert(MakeDefault == nullptr,
                    "get_or_default() is unavailable on a cached_value carrying MakeDefault: get() "
                    "already returns the type's default for an unwritten row, so this argument "
                    "could only be discarded. Call get().");
      load();
      return _present ? *_cache : def;
   }

   /**
    * Mutate the value in place. Asserts if the row is absent -- use modify_or_create() to
    * create. The write is deferred to flush()/destruction.
    *
    * @param payer account billed for the row when the change is written back.
    * @param f     callable receiving a mutable reference to the cached value.
    */
   template<typename Lambda>
   void modify(sysio::name payer, Lambda&& f) {
      load();
      // Distinguish "never existed" from "you erased it earlier in this action". Without this the
      // second case reports the first case's message, which sends the reader hunting for a missing
      // row that their own remove() retired.
      sysio::check(!_removed, "singleton mutated after remove()");
      sysio::check(_present, "singleton does not exist");
      mutate(payer, std::forward<Lambda>(f));
   }

   /**
    * Mutate the value in place, seeding the cache from \p def first when the row is absent.
    * The write is deferred to flush()/destruction.
    *
    * NOT named upsert, deliberately. kv::table::upsert(payer, key, default_value, updater)
    * stores default_value VERBATIM on the insert path and never invokes the updater there --
    * callers pass a fully-populated default and treat the lambda as update-only. This does the
    * opposite: \p def only seeds the cache and \p f runs in every case, so a blank default plus
    * a lambda that fills it in is the idiomatic call. Two functions in sysio::kv with one name
    * and opposite insert semantics would be applied interchangeably by mistake, and the
    * mistake is silent -- the row is written either way, just with different contents.
    *
    * Unavailable on a handle carrying MakeDefault: the absent case \p def exists to seed cannot
    * arise there, so \p def would be dropped and the mutation would run on the type's default
    * instead. modify() is the whole of what this call still means on such a handle.
    */
   template<typename Lambda>
   void modify_or_create(sysio::name payer, const T& def, Lambda&& f) {
      static_assert(MakeDefault == nullptr,
                    "modify_or_create() is unavailable on a cached_value carrying MakeDefault: the "
                    "type already defines what an unwritten row reads as, leaving no absent case "
                    "for this default to seed. Call modify().");
      load();
      // Checked BEFORE seeding: on the removed path this call is rejected, and it must not leave
      // the handle holding a seeded value it never gets to write.
      sysio::check(!_removed, "singleton mutated after remove()");
      if (!_present) {
         _cache   = def;
         _present = true;
      }
      mutate(payer, std::forward<Lambda>(f));
   }

   /**
    * Materialize \p def into the cache when the row is absent, WITHOUT owing a write.
    *
    * This is the safe form of "give me defaults on a chain where nobody has written the row yet".
    * Seeding through set()/modify_or_create() would mark the handle dirty, so every action --
    * including a pure query -- would flush a kv_set and be refused inside a read-only transaction,
    * which is the exact failure this class exists to prevent. After seeding, reads see \p def and
    * the defaults reach storage only when an action genuinely mutates something.
    *
    * Unavailable on a handle carrying MakeDefault: load() already seeds from the provider on the
    * same terms -- present, clean, no write owed -- so this call could only be a no-op.
    */
   void seed_if_absent(const T& def) {
      static_assert(MakeDefault == nullptr,
                    "seed_if_absent() is unavailable on a cached_value carrying MakeDefault: load() "
                    "already materializes the type's default without owing a write, so this call "
                    "would do nothing. Delete it.");
      load();
      if (!_present) {
         _cache   = def;
         _present = true;
      }
   }

   /**
    * Replace the value outright. Creates the row if absent. Deferred.
    *
    * Passing kv::same_payer keeps whatever payer this handle already recorded, so set(v, payer)
    * followed by set(w, same_payer) still bills the one coalesced write to payer -- see
    * record_payer().
    */
   void set(const T& val, sysio::name payer) {
      _loaded  = true;
      _present = true;
      _removed = false;   // this value supersedes any removal recorded earlier in the action
      _cache   = val;
      record_payer(payer);
      _pending = pending_op::write;
   }

   /**
    * Erase the row, discarding any pending write. Deferred.
    *
    * Probes the store first so that erasing a row that is not there costs nothing and stays legal
    * inside a read-only transaction. Without the probe, both backing stores short-circuit an erase
    * of an absent row at flush time, so whether this action issued a write -- and therefore whether
    * it was legal read-only -- would depend on chain data rather than on the code.
    *
    * What is owed is decided by _row -- whether a row is STORED -- never by exists(). On a handle
    * carrying defaults the two differ: a value is always available, and owing the erase off that
    * would send Store::remove() after a row nobody ever wrote. Both shipped stores re-probe and
    * absorb it, but the documented Store contract promises no such thing and the chain's own
    * kv_erase aborts on a missing key -- and dirty() would meanwhile report a debt that does not
    * exist. When set() left presence unresolved, this call resolves it rather than assuming.
    *
    * Removal itself is recorded in _removed, separately from what the store is owed, because those
    * two came apart the moment an erase stopped being the only outcome: a row that was never
    * written is genuinely removed and owes nothing. Guards that mean "has remove() been called"
    * -- including the one protecting a mutation callback -- read _removed.
    *
    * On a handle carrying defaults this reads as RESET TO DEFAULTS: the stored row goes, and the
    * handle keeps serving what an unwritten row reads as, so get() stays non-asserting exactly as
    * the type promises. Without defaults the handle reports the row gone, as before.
    *
    * The cached object is deliberately NOT destroyed: the absence is recorded in the flags, and
    * keeping it alive means a reference handed out by an earlier get() never dangles.
    *
    * Idempotent: calling this twice owes the same single erase as calling it once, and consults the
    * default provider no more often than once.
    */
   void remove() {
      load();
      // The removal this call records is _removed, NOT the pending erase: a row that was never
      // written owes no erase, so _pending cannot answer "did someone remove this". Every guard
      // that means "has remove() been called" reads _removed. Re-entering is a no-op, which keeps
      // the call idempotent and the default provider consulted at most once.
      if (_removed) return;
      _removed = true;
      // set() may have left presence unresolved, and only a stored row can be erased.
      resolve_row_state();
      // A pending write never reached the store, so there is nothing to undo -- drop it.
      const bool discarded_write = _pending == pending_op::write;
      const bool stored          = _row == row_state::present;
      _pending = stored ? pending_op::erase : pending_op::none;
      if constexpr (MakeDefault != nullptr) {
         // Re-materialize only when the cache can have diverged from the default -- it was loaded
         // from a stored row, or a mutation changed it -- so repeated removes on a never-written
         // singleton do not re-run the provider.
         if (stored || discarded_write) _cache = MakeDefault();
         _present = true;
      } else {
         _present = false;
      }
   }

   /// True when a change is owed to the store.
   bool dirty() const { return _pending != pending_op::none; }

   /// Apply the pending change, if any. Idempotent.
   void flush() {
      const auto op = _pending;
      // Cleared before the store call: a rejected write aborts the transaction anyway, so
      // there is no state to retry, and this keeps a manual flush() followed by the
      // destructor from attempting the same write twice.
      _pending = pending_op::none;
      // flush() RE-ARMS the handle, so the removal it just committed is spent: a later mutation is
      // a fresh operation on the current state, not one applied "after remove()".
      _removed = false;
      // _row tracks what the STORE holds, so it moves with the change this call applies -- otherwise
      // a mutate/flush/remove cycle would decide the next erase off a stale reading.
      switch (op) {
         case pending_op::write: _store.set(*_cache, _payer); _row = row_state::present; break;
         case pending_op::erase: _store.remove();             _row = row_state::absent;  break;
         case pending_op::none:                                                          break;
      }
   }

private:
   /// Shared tail of modify()/modify_or_create(): apply \p f and record the debt.
   template<typename Lambda>
   void mutate(sysio::name payer, Lambda&& f) {
      sysio::check(!_removed, "singleton mutated after remove()");
      f(*_cache);
      // Re-checked AFTER f(): the callback holds a reference to this handle's cache and may call
      // remove() through it. Recording a write here would resurrect the value the caller just
      // retired, and the caller would get no diagnostic.
      //
      // The guard reads _removed rather than the pending erase, because removing a row that was
      // never written owes no erase -- and on THAT path the caller was silently getting a write.
      sysio::check(!_removed, "singleton removed from inside a mutation callback");
      record_payer(payer);
      _pending = pending_op::write;
   }

   /**
    * Record the account to bill for the pending write.
    *
    * A default-constructed name is kv::same_payer -- "bill whoever already owns the row" -- so it
    * must never displace a real payer recorded earlier in this action. Every call that dirties the
    * handle coalesces into a SINGLE kv_set, and the host rejects payer 0 when that kv_set creates
    * the row; uncached, the create and the update were separate writes and only the update could
    * legally carry same_payer. With no real payer recorded, 0 reaches the host untouched, which is
    * what makes an update keep its existing payer and a create fail exactly as it does uncached.
    *
    * Every path that dirties the handle records its payer HERE rather than assigning _payer
    * directly, so a new write path cannot reintroduce the divergence by forgetting the rule.
    */
   void record_payer(sysio::name payer) {
      if (payer.value != 0) _payer = payer;
   }

   /// Populate the cache from the store. At most one store read per handle.
   ///
   /// When the row is absent and this type carries defaults, MakeDefault supplies the value here.
   /// That leaves the handle PRESENT but CLEAN -- exactly what seed_if_absent does, except it
   /// happens on first use rather than at construction, so an action that never touches the
   /// singleton performs no store read and never calls the provider. The `if constexpr` means a
   /// handle without defaults emits none of this.
   void load() const {
      if (_loaded) return;
      _loaded = true;
      T val;
      if (_store.try_get(val)) {
         _cache   = std::move(val);
         _present = true;
         _row     = row_state::present;
      } else {
         _row = row_state::absent;
         if constexpr (MakeDefault != nullptr) {
            _cache   = MakeDefault();
            _present = true;
         }
      }
   }

   /// Settle whether a row is stored, for the one path that can reach remove() without knowing.
   ///
   /// set() deliberately skips the store read, so it leaves _row unknown. Guessing either way is
   /// wrong: guess `present` and an erase goes out for a row that was never there; guess `absent`
   /// and a set-then-remove silently leaves the row it was about to overwrite in place. So resolve
   /// it, and only here -- a handle that never removes after a blind set pays nothing.
   ///
   /// try_get is the only presence query the Store contract exposes, so this costs a read rather
   /// than a cheaper contains-style probe. A read is legal inside a read-only transaction, which is
   /// what matters: resolving cannot make an otherwise-legal action illegal. The value is
   /// discarded -- _cache already holds what set() put there.
   void resolve_row_state() const {
      if (_row != row_state::unknown) return;
      T scratch;
      _row = _store.try_get(scratch) ? row_state::present : row_state::absent;
   }

   Store                    _store;
   mutable std::optional<T> _cache;
   sysio::name              _payer{};
   mutable bool             _loaded  = false;   ///< has the store been consulted yet
   mutable bool             _present = false;   ///< is a value available, per cache
   /// Has remove() been called since the last set()/flush() -- the LOGICAL removal, which is not
   /// the same as owing an erase: removing a row that was never written owes the store nothing.
   /// Every "was this removed" guard reads this; _pending answers only "what does the store owe".
   bool                     _removed = false;
   mutable row_state        _row     = row_state::unknown;
   pending_op               _pending = pending_op::none;
};

}} // namespace sysio::kv
