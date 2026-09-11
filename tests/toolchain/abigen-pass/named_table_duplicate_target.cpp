// Two row structs cannot both be renamed to one ABI table name, and neither may be picked.
//
// abi_table_annotation was ordered by `name` alone, so the second of two annotations sharing a
// name never entered the set: one row was renamed, the other silently kept its table parameter,
// nothing was reported, and swapping the two declarations swapped which one won. Ordering by
// (name, row) keeps both, and the resolver refuses them together -- naming the other row in
// each warning, since a contract author reading one of them needs to know what it collided
// with.
//
// Cross-TU it was worse than silent. The merge identifies annotations by `name`, as it does
// every other section, so two differing records under one name were a malformed ABI:
//
//   Error, ABI structs malformed : xshared already defined
//
// -- a dead link rather than a diagnostic, and the same source produced one or the other
// depending only on how the two rows were spread over files. Annotations merge by (name, row)
// now, so both reach the resolver and both get the same answer wherever they were written.
// Deciding link-wide questions link-wide is why they are carried at all rather than applied
// where they are found.
//
// The x_* pair is declared in a header PER TRANSLATION UNIT for that reason. In a shared header
// the per-TU set drops one of the two before the merge is reached, both descriptors record the
// same survivor, and the merge has nothing to reconcile -- so a shared header exercises the
// comparator (the s_* pair) and not the merge.
//
// Expected: every table keeps its own parameter -- sfirst, ssecond, xfirst, xsecond -- and four
// warnings, one per refused annotation.
#include "named_table_duplicate_target_aux/rows.hpp"
#include "named_table_duplicate_target_aux/x_main.hpp"
#include <sysio/multi_index.hpp>

class [[sysio::contract("named_table_duplicate_target")]] named_table_duplicate_target : public sysio::contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      sysio::multi_index<"sfirst"_n,  s_one_row> a(get_self(), get_self().value);
      sysio::multi_index<"ssecond"_n, s_two_row> b(get_self(), get_self().value);
      sysio::multi_index<"xfirst"_n,  x_one_row> c(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
      c.emplace(get_self(), [&](auto& r) { r.id = 3; });
   }
};
