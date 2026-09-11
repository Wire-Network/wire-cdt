// The two conditions a [[sysio::table("name")]] has to meet are LINK-WIDE facts.
//
// It renames one table, and only into a name no other table holds. Both were checked per
// translation unit, where neither is knowable, so each TU renamed on its partial view and the
// descriptors disagreed -- and abi_table is compared by name, so the merge refused the link:
//
//   Error, ABI structs malformed : cfg already defined
//
// The same source in ONE translation unit produced the right answer all along, which is what
// made it a knowledge problem rather than a rule problem. abigen records the annotation now
// (____table_annotations) and cdt-codegen applies it once every descriptor is in.
//
// Two shapes, one contract:
//   config_row -- one table per TU, two link-wide, so the annotation names neither
//   a_row      -- one table, but `alpha` is b_row's table in the other TU, so it is refused
//
// Expected: alpha (b_row), bravo (a_row), cfgone, cfgtwo -- every table the contract writes,
// each under its own parameter, and two warnings.
#include "named_table_split_aux/row.hpp"
#include <sysio/multi_index.hpp>

class [[sysio::contract("named_table_split")]] named_table_split : public sysio::contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      sysio::multi_index<"cfgone"_n, config_row> a(get_self(), get_self().value);
      sysio::multi_index<"bravo"_n,  a_row>      b(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
   }
};
