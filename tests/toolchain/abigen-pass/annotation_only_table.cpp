// A table an annotation DECLARES carries its own dependencies.
//
// [[sysio::table("name")]] on a row nothing instantiates is the table: cdt-codegen publishes an
// entry for it after the merge, with the key metadata the annotation resolved. Those types have
// to survive validate_struct(), and the check that kept them -- kv_key_structs -- sat inside the
// loop over INSTANTIATED tables, a condition that never depended on the loop variable.
//
// So this contract is the whole point of the fixture: the annotation-declared table is its ONLY
// table, the loop never runs, and the key struct and the typedef its field names were pruned
// out from under an entry that names them. Any other table in the contract makes the loop run
// once and hides it, which is why this case cannot live in named_table_attr.
//
// Expected: `declared` with key_types ["lone_id"], `lone_key` in structs, `lone_id` in types.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("annotation_only_table")]] annotation_only_table : public contract {
public:
   using contract::contract;

   using lone_id = uint64_t;

   struct lone_key {
      lone_id account;
      SYSLIB_SERIALIZE(lone_key, (account))
   };

   struct [[sysio::table("declared"), sysio::kv_key("lone_key")]] lone_row {
      uint64_t v;
      SYSLIB_SERIALIZE(lone_row, (v))
   };

   [[sysio::action]]
   void test() {}
};
