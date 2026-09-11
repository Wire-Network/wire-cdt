// A [[sysio::kv_key]] override has to be DECLARED, not merely protected from pruning.
//
// add_kv_table put the override's name into kv_key_structs -- the set validate_struct consults
// to keep a struct alive -- but never emitted the struct and never ran its fields through
// add_type. Keeping an entry in a set it never joined does nothing, so the ABI published
// key_types naming a type the document does not define:
//
//   key_types: ["logical_id"]      structs: [phys_key, test, val]
//
// and query-key decoding had nothing to resolve. add_table's identical branch has always
// emitted both; this path only ever did half of it.
//
// A BARE attribute is what makes it reachable in the first place. add_table returns as soon as
// it sees an unnamed [[sysio::table]] -- there is no table for it to describe -- so the
// override never reaches the branch that would have declared it, and add_kv_table is the only
// path left that resolves the attribute at all.
//
// Expected: `abi_key` and `logical_id` both present in structs, and key_types naming
// `logical_id`.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_key_type_closure")]] kv_key_type_closure : public contract {
public:
   using contract::contract;

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   // A key field whose type is a contract struct rather than a builtin: the part that has to
   // travel with the override.
   struct logical_id {
      uint64_t hi;
      uint64_t lo;
      SYSLIB_SERIALIZE(logical_id, (hi)(lo))
   };

   struct [[sysio::table, sysio::kv_key("abi_key")]] val {
      struct abi_key {
         logical_id account;
         SYSLIB_SERIALIZE(abi_key, (account))
      };
      uint64_t balance;
      SYSLIB_SERIALIZE(val, (balance))
   };

   using accounts = kv::table<"realname"_n, phys_key, val>;

   [[sysio::action]]
   void test() {
      accounts tbl(get_self());
      tbl.emplace(get_self(), {1}, {100});
   }
};
