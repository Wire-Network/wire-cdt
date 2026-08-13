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
 * @tparam Store backing store satisfying the requirements documented above.
 */
template<typename Store>
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

   /// True if the row exists, accounting for a pending set()/remove().
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
   T get_or_default(const T& def) const {
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
      sysio::check(_pending != pending_op::erase, "singleton mutated after remove()");
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
    */
   template<typename Lambda>
   void modify_or_create(sysio::name payer, const T& def, Lambda&& f) {
      load();
      // Checked BEFORE seeding: on the erase path this call is rejected, and it must not leave the
      // handle holding a seeded value it never gets to write.
      sysio::check(_pending != pending_op::erase, "singleton mutated after remove()");
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
    */
   void seed_if_absent(const T& def) {
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
    * The cached object is deliberately NOT destroyed: _present already records the row as gone, and
    * keeping it alive means a reference handed out by an earlier get() never dangles.
    *
    * Idempotent: calling this twice owes the same single erase as calling it once.
    */
   void remove() {
      load();
      if (!_present) {
         // Absent per the cache -- but WHY it is absent decides what is owed. An erase this handle
         // already recorded is the reason _present is false, and cancelling it here would leave the
         // stored row in place while the handle went on reporting it gone. Only a row that was
         // never there discards the debt, and there the debt can only be a pending write.
         if (_pending != pending_op::erase) _pending = pending_op::none;
         return;
      }
      _present = false;
      _pending = pending_op::erase;
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
      switch (op) {
         case pending_op::write: _store.set(*_cache, _payer); break;
         case pending_op::erase: _store.remove();             break;
         case pending_op::none:                               break;
      }
   }

private:
   /// Shared tail of modify()/modify_or_create(): apply \p f and record the debt.
   template<typename Lambda>
   void mutate(sysio::name payer, Lambda&& f) {
      sysio::check(_pending != pending_op::erase, "singleton mutated after remove()");
      f(*_cache);
      // Re-checked AFTER f(): the callback holds a reference to this handle's cache and may call
      // remove() through it. Recording a write here would convert that erase back into a store of
      // the value the caller just retired, and the caller would get no diagnostic.
      sysio::check(_pending != pending_op::erase, "singleton removed from inside a mutation callback");
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
   void load() const {
      if (_loaded) return;
      _loaded = true;
      T val;
      if (_store.try_get(val)) {
         _cache   = std::move(val);
         _present = true;
      }
   }

   Store                    _store;
   mutable std::optional<T> _cache;
   sysio::name              _payer{};
   mutable bool             _loaded  = false;   ///< has the store been consulted yet
   mutable bool             _present = false;   ///< does the row exist, per cache
   pending_op               _pending = pending_op::none;
};

}} // namespace sysio::kv
