// A row in an anonymous namespace is a different type in every translation unit.
//
// `____row` carries the row struct's qualified name so cdt-codegen can match an annotation to
// the tables over its row across translation units -- which means it has to name the same type
// wherever it appears. An anonymous-namespace struct does not: it prints as
// `(anonymous namespace)::row` in every TU while being a distinct type in each.
//
// So two unrelated rows were grouped as one. Each annotation then looked like it named two
// tables, both were refused with "can name only one table, but ... is the row type of 2", and
// both tables kept their raw parameters -- `one` and `two` rather than `first` and `second`.
//
// The other half of the same rule: `shared_row` is ONE declaration in a header both TUs
// include. Each TU gets a distinct type from it, but it is a single declaration carrying a
// single annotation, so the two tables over it must group as one. Keying the marker on the main
// FILE rather than on the declaration split them instead, and the merge refused the link with
// `orig already defined` -- a table that had linked fine before. The declaration's spelling
// location answers both halves: one header declaration has one, two declarations never share
// one.
//
// And the third: `a/samebase.hpp` and `b/samebase.hpp` share a basename and declare an anonymous
// `same` at the same offset. Keying on the basename to absorb the spelling difference above
// collapsed these two distinct declarations into one, and both annotations were refused as
// naming two tables apiece. The canonical path settles both -- alternate spellings of one file
// agree, different files do not.
//
// Expected: alpha, bravo, first, orig, second -- and no warnings.
#include <sysio/sysio.hpp>
#include "named_table_internal_linkage_aux/shared_row.hpp"
#include "named_table_internal_linkage_aux/a/samebase.hpp"
#include <sysio/multi_index.hpp>

using namespace sysio;

namespace {
   struct [[sysio::table("first"), sysio::contract("named_table_internal_linkage")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

class [[sysio::contract("named_table_internal_linkage")]] named_table_internal_linkage : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      sysio::multi_index<"one"_n, row> t(get_self(), get_self().value);
      sysio::multi_index<"orig"_n, shared_row> s(get_self(), get_self().value);
      sysio::multi_index<"one"_n, same> u(get_self(), get_self().value);
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
      s.emplace(get_self(), [&](auto& r) { r.id = 2; });
      u.emplace(get_self(), [&](auto& r) { r.id = 3; });
   }
};
