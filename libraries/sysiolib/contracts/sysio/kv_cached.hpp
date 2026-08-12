#pragma once
/**
 * sysio::kv::cached_value -- write-deferring cache over a KV singleton store.
 *
 * Loads the stored value at most once, serves every read from that cache, and writes back
 * at most once -- on flush() or destruction -- and only when a mutating call actually ran.
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
    * Mutate the value in place. Asserts if the row is absent -- use upsert() to create.
    * The write is deferred to flush()/destruction.
    *
    * @param payer account billed for the row when the change is written back.
    * @param f     callable receiving a mutable reference to the cached value.
    */
   template<typename Lambda>
   void modify(sysio::name payer, Lambda&& f) {
      load();
      sysio::check(_present, "singleton does not exist");
      mutate(payer, std::forward<Lambda>(f));
   }

   /**
    * Mutate the value in place, seeding the cache from \p def first when the row is
    * absent. The write is deferred to flush()/destruction.
    */
   template<typename Lambda>
   void upsert(sysio::name payer, const T& def, Lambda&& f) {
      load();
      if (!_present) {
         _cache   = def;
         _present = true;
      }
      mutate(payer, std::forward<Lambda>(f));
   }

   /// Replace the value outright. Creates the row if absent. Deferred.
   void set(const T& val, sysio::name payer) {
      _loaded  = true;
      _present = true;
      _cache   = val;
      _payer   = payer;
      _pending = pending_op::write;
   }

   /// Erase the row, discarding any pending write. Deferred.
   void remove() {
      _loaded  = true;
      _present = false;
      _cache.reset();
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
   /// Shared tail of modify()/upsert(): apply \p f and record the debt.
   template<typename Lambda>
   void mutate(sysio::name payer, Lambda&& f) {
      sysio::check(_pending != pending_op::erase, "singleton mutated after remove()");
      f(*_cache);
      _payer   = payer;
      _pending = pending_op::write;
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
