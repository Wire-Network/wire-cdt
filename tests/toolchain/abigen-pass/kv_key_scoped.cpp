// A [[sysio::kv_key]] override replaces the LOGICAL key, not the physical prefix.
//
// kv::scoped_table puts a `scope` ahead of the key fields and kv::table does not, so the prefix
// is a property of the TABLE while the override is a property of the ROW. add_kv_table composes
// [scope] + logical for exactly that reason.
//
// cdt-codegen then applied the annotation's key metadata to every table over the struct by
// replacing the whole array, and the prefix went with it: descriptors that read
// [scope, account_id] were published as [account_id], so a client encoded an eight-byte-shorter
// key than the runtime writes -- and got no error, just no rows.
//
// Two tables, because that is what makes the case reachable without the rename hiding it: the
// annotation can name only one of them, so it is refused as a rename and the key fold is the
// only part of it that still applies. The warning below is expected.
//
// The second pair goes further: its override's OWN first field is called `scope`. The guard that
// preserves the prefix recognises it by that name, so a logical field sharing it looked like the
// prefix, suppressed it, and published a key eight bytes short whose first element was typed
// `uint64` instead of `name` -- the same failure, reached through the fix for it. A layout that
// already ends with the override is left alone now, which needs no name match at all.
//
// Expected: the first pair keeps ["scope", "account_id"]; the second keeps
// ["scope", "scope", "account_id"], the physical prefix ahead of the override entire.
#include <sysio/sysio.hpp>
#include <sysio/kv_scoped_table.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_key_scoped")]] kv_key_scoped : public contract {
public:
   using contract::contract;

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   struct [[sysio::table("aliasname"), sysio::kv_key("logical_key")]] val {
      struct logical_key {
         uint64_t account_id;
         SYSLIB_SERIALIZE(logical_key, (account_id))
      };
      uint64_t balance;
      SYSLIB_SERIALIZE(val, (balance))
   };

   struct [[sysio::table("secondalias"), sysio::kv_key("shadow_key")]] shadow {
      struct shadow_key {
         uint64_t scope;        // shares the physical prefix's name, and is not it
         uint64_t account_id;
         SYSLIB_SERIALIZE(shadow_key, (scope)(account_id))
      };
      uint64_t balance;
      SYSLIB_SERIALIZE(shadow, (balance))
   };

   using scoped_a = kv::scoped_table<"scopeda"_n, phys_key, val>;
   using scoped_b = kv::scoped_table<"scopedb"_n, phys_key, val>;
   using shadow_a = kv::scoped_table<"shadowa"_n, phys_key, shadow>;
   using shadow_b = kv::scoped_table<"shadowb"_n, phys_key, shadow>;

   [[sysio::action]]
   void test() {
      scoped_a a(get_self(), 1);
      scoped_b b(get_self(), 1);
      shadow_a c(get_self(), 1);
      shadow_b d(get_self(), 1);
      a.emplace(get_self(), {1}, {100});
      b.emplace(get_self(), {2}, {200});
      c.emplace(get_self(), {3}, {300});
      d.emplace(get_self(), {4}, {400});
   }
};
