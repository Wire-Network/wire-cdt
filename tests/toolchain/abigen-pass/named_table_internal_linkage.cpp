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
// Expected: first, second -- and no warnings.
#include <sysio/sysio.hpp>
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
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
