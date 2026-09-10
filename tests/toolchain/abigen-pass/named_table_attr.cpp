// What [[sysio::table("name")]] on a row struct names, and when it cannot name anything.
//
// The annotation renames the table the ABI publishes over that struct. It has to: a `_i`-named
// table's raw value is a DJB2 hash rather than a name encoding, so name_to_string() renders it
// as garbage and the readable name lives only in the annotation (#115).
//
// The rename used to be applied where each instantiation was added, before the rest of the
// translation unit was known. One annotation can name only ONE table, so a struct backing two
// gave both entries that same name -- and abi_table orders by name alone, so the second landed
// on the first in the set and was dropped. The contract wrote two tables, the ABI described one,
// under the table_id of whichever instantiation abigen happened to reach first. It is the
// collision in bare_table_two_names arrived at from the other side: there the placeholder shared
// a name with a real table, here two real tables are given one.
//
// The same reasoning bars renaming onto a name another table already holds -- the two would
// collapse just as surely, and the survivor's `type` would describe one row layout while its
// table_id addressed the other table's rows. And because the annotation is also where
// [[sysio::kv_key]] is resolved into key_names/key_types, an annotation that names nothing must
// still hand that metadata to the tables it could not name, or dropping it would silently revert
// them to the physical key layout.
//
// Five cases in one translation unit:
//   `renamed`  -- one instantiation. The annotation names it; "orig" is not published.
//   `declared` -- annotated, never instantiated. Nothing else names a table for this struct, so
//                 the annotation is the table -- but it carries no table_id, because only an
//                 instantiation has one and there is none. (A BARE [[sysio::table]] in the same
//                 position produces nothing at all -- see bare_table_attr -- because there
//                 abigen would be inventing the name rather than publishing one the author
//                 wrote. See named_table_xtu for why a guessed id is worse than none.)
//   `shared`   -- two instantiations, and a [[sysio::kv_key]] override. Neither is renamed: each
//                 keeps its own table parameter and BOTH carry the override's key names, and the
//                 annotation is published as nothing, with a warning saying so. Publishing it as
//                 well would add a table under a table_id nothing writes to.
//                 The override replaces the LOGICAL key and leaves the physical `scope` in
//                 front of it -- these are kv_multi_index tables, whose key is
//                 [scope:8B BE][pk:8B BE]. Replacing the whole array published ["account_id"]
//                 and lost the scope, so a client encoded eight bytes less than the runtime
//                 writes; kv_key_scoped pins the same defect on kv::scoped_table.
//   `taken`    -- one instantiation, but `taken` is another row struct's table. The annotation
//                 is refused rather than applied, with its own warning; both live tables keep
//                 their own names, and neither is dropped.
//   `occupied` -- annotated, never instantiated, and the name is another row struct's table.
//                 Publishing it would put two tables called `occupied` in one ABI.
//
// Expected: declared, first, occupied (holder2_row's), orphan, renamed, second, taken.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("named_table_attr")]] named_table_attr : public contract {
public:
   using contract::contract;

   struct [[sysio::table("renamed")]] one_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(one_row, (id))
   };

   // The key struct and the typedef its field names are dependencies of the ANNOTATION, not of
   // any instantiated table -- there is no instantiation here. validate_struct() consulted
   // kv_key_structs from inside its loop over instantiated tables, so with `declared` as the
   // only table over this row the loop never ran, both were pruned, and the entry published
   // key_types naming a type the document does not define.
   using lone_id = uint64_t;
   struct lone_key {
      lone_id account;
      SYSLIB_SERIALIZE(lone_key, (account))
   };

   struct [[sysio::table("declared"), sysio::kv_key("lone_key")]] lone_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(lone_row, (id))
   };

   struct shared_key {
      uint64_t account_id;
      SYSLIB_SERIALIZE(shared_key, (account_id))
   };

   struct [[sysio::table("shared"), sysio::kv_key("shared_key")]] shared_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(shared_row, (id))
   };

   struct [[sysio::table("taken")]] collide_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(collide_row, (id))
   };

   struct [[sysio::table]] holder_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(holder_row, (id))
   };

   struct [[sysio::table("occupied")]] lone2_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(lone2_row, (id))
   };

   struct [[sysio::table]] holder2_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(holder2_row, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"orig"_n,   one_row>     a(get_self(), get_self().value);
      multi_index<"first"_n,  shared_row>  b(get_self(), get_self().value);
      multi_index<"second"_n, shared_row>  c(get_self(), get_self().value);
      multi_index<"orphan"_n, collide_row> d(get_self(), get_self().value);
      multi_index<"taken"_n,  holder_row>  e(get_self(), get_self().value);
      multi_index<"occupied"_n, holder2_row> f(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
      c.emplace(get_self(), [&](auto& r) { r.id = 3; });
      d.emplace(get_self(), [&](auto& r) { r.id = 4; });
      e.emplace(get_self(), [&](auto& r) { r.id = 5; });
      f.emplace(get_self(), [&](auto& r) { r.id = 6; });
   }
};
