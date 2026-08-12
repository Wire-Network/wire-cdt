#pragma once
#include "kv_multi_index.hpp"
#include "kv_cached.hpp"
// Deliberately does NOT include system.hpp: nothing here (nor in kv_multi_index.hpp or
// kv_table.hpp) uses any symbol it declares, and its is_feature_activated declaration collides
// with the C-API one, which made this header unusable from a native unit test that also includes
// the C API -- so the scoped alias below could not be covered by the natively-run suite.

namespace sysio {

   /**
    * KV-backed singleton. Drop-in replacement for sysio::singleton.
    * Backed by kv_multi_index (single-row table with fixed primary key).
    */
   template<name::raw SingletonName, typename T>
   class kv_singleton
   {
      constexpr static uint64_t pk_value = static_cast<uint64_t>(SingletonName);

      struct row {
         T value;
         uint64_t primary_key() const { return pk_value; }
         SYSLIB_SERIALIZE( row, (value) )
      };

      typedef kv_multi_index<SingletonName, row> table;

      public:

         /// Payload type. Lets generic wrappers (kv::cached_value) deduce it without repetition.
         using value_type = T;

         kv_singleton( name code, uint64_t scope ) : _t( code, scope ) {}

         bool exists() const {
            return _t.find( pk_value ) != _t.end();
         }

         T get() const {
            auto itr = _t.find( pk_value );
            sysio::check( itr != _t.end(), "singleton does not exist" );
            return itr->value;
         }

         /// Returns true and populates \p out when the row exists, in one call.
         ///
         /// One CALL, not one intrinsic: this goes through kv_multi_index::find, which costs
         /// several billed host calls on the row-exists path. It exists so kv::cached_value has a
         /// non-asserting load that does not pay exists()-then-get() on top of that, not because it
         /// is cheap. (kv::global::try_get genuinely is a single kv_get.)
         bool try_get( T& out ) const {
            auto itr = _t.find( pk_value );
            if( itr == _t.end() ) return false;
            out = itr->value;
            return true;
         }

         T get_or_default( const T& def = T() ) const {
            auto itr = _t.find( pk_value );
            return itr != _t.end() ? itr->value : def;
         }

         T get_or_create( name bill_to_account, const T& def = T() ) {
            auto itr = _t.find( pk_value );
            if (itr != _t.end()) return itr->value;
            _t.emplace(bill_to_account, [&](row& r) { r.value = def; });
            return def;
         }

         void set( const T& value, name bill_to_account ) {
            auto itr = _t.find( pk_value );
            if( itr != _t.end() ) {
               _t.modify(itr, bill_to_account, [&](row& r) { r.value = value; });
            } else {
               _t.emplace(bill_to_account, [&](row& r) { r.value = value; });
            }
         }

         void remove( ) {
            auto itr = _t.find( pk_value );
            if( itr != _t.end() ) {
               _t.erase(itr);
            }
         }

      private:
         table _t;
   };

   /**
    * Write-deferring kv_singleton.
    *
    * Reads never issue a kv_set, so an action that only reads the singleton stays legal
    * inside a read-only transaction. This is the drop-in for a contract ported from the
    * classic idiom of caching the value in a member and writing it back from the contract
    * destructor -- that pattern makes every action, including pure queries, fail read-only
    * execution. See kv_cached.hpp for the rationale and the deferred-write visibility rules.
    */
   template<name::raw SingletonName, typename T>
   using cached_kv_singleton = kv::cached_value<kv_singleton<SingletonName, T>>;

} /// namespace sysio
