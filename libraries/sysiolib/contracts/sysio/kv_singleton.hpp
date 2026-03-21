#pragma once
#include "kv_table.hpp"
#include "system.hpp"

namespace sysio {

   /**
    * KV-backed singleton. Drop-in replacement for sysio::singleton.
    * Uses sysio::kv::table instead of legacy multi_index.
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

      typedef sysio::kv::table<SingletonName, row> table;

      public:

         kv_singleton( name code, uint64_t scope ) : _t( code, scope ) {}

         bool exists() const {
            return _t.find( pk_value ) != _t.end();
         }

         T get() const {
            auto itr = _t.find( pk_value );
            sysio::check( itr != _t.end(), "singleton does not exist" );
            return itr->value;
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

} /// namespace sysio
