#include <sysio/sysio.hpp>
#include <sysio/permission.hpp>

using namespace sysio;

class [[sysio::contract]] permission_tests : public contract {
public:
   using contract::contract;

   // Verify get_permission() convenience wrapper compiles and links.
   // The intrinsic returns -1 when the permission is not found on-chain,
   // so get_permission() returns std::nullopt.
   [[sysio::action]]
   void getperm( name account, name perm ) {
      auto rec = get_permission( account, perm );
      if( rec ) {
         print("found perm=", rec->perm_name, " parent=", rec->parent,
               " threshold=", rec->auth.threshold,
               " keys=", rec->auth.keys.size(),
               " accounts=", rec->auth.accounts.size());
      } else {
         print("not found");
      }
   }

   // Verify the raw C intrinsic compiles and links.
   [[sysio::action]]
   void getpermraw( name account, name perm ) {
      char buf[256];
      int32_t sz = internal_use_do_not_use::get_permission_lower_bound(
         account.value, perm.value, buf, sizeof(buf) );
      if( sz > 0 && static_cast<uint32_t>(sz) <= sizeof(buf) ) {
         auto rec = unpack<permission_record>( buf, static_cast<size_t>(sz) );
         print("raw perm=", rec.perm_name);
      } else {
         print("raw not found, sz=", sz);
      }
   }

   // Verify all the new types are usable in contract code.
   [[sysio::action]]
   void checktypes() {
      perm_key_weight kw;
      kw.weight = 1;

      perm_level_weight lw;
      lw.permission = permission_level{ "alice"_n, "active"_n };
      lw.weight = 1;

      perm_authority auth;
      auth.threshold = 1;
      auth.keys.push_back( kw );
      auth.accounts.push_back( lw );

      permission_record rec;
      rec.perm_name = "active"_n;
      rec.parent    = "owner"_n;
      rec.auth      = auth;

      // Round-trip serialize / deserialize
      auto packed = pack( rec );
      auto rec2   = unpack<permission_record>( packed );

      check( rec2.perm_name == "active"_n, "perm_name mismatch" );
      check( rec2.parent    == "owner"_n,  "parent mismatch" );
      check( rec2.auth.threshold == 1,     "threshold mismatch" );
      check( rec2.auth.keys.size() == 1,   "keys size mismatch" );
      check( rec2.auth.accounts.size() == 1, "accounts size mismatch" );
      check( rec2.auth.accounts[0].permission.actor == "alice"_n, "account actor mismatch" );

      print("types ok");
   }
};
