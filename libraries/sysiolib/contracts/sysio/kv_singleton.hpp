#pragma once
#include "kv_multi_index.hpp"
#include "kv_cached.hpp"
#include "system.hpp"

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

         /// Single lookup: returns true and populates \p out when the row exists.
         /// Present so generic wrappers (kv::cached_value) can load in one probe.
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
