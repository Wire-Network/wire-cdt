// Two annotated row structs sharing an unqualified name must not be conflated.
//
// ns1::row and ns2::row both serialise as ABI type `row`, so matching an annotation to its
// tables by `type` cannot tell them apart -- the mistake that deleted a live table in an earlier
// design (bare_table_namespaces). The descriptor carries the QUALIFIED name instead, so each
// annotation sees one table over its own struct and renames it.
//
// Matching on the unqualified name would see `row` backing two tables and refuse both renames,
// publishing `one` and `two` instead.
//
// Expected: first (ns1::row) and second (ns2::row).
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

namespace ns1 {
   struct [[sysio::table("first"), sysio::contract("named_table_namespaces")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

namespace ns2 {
   struct [[sysio::table("second"), sysio::contract("named_table_namespaces")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

class [[sysio::contract("named_table_namespaces")]] named_table_namespaces : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      multi_index<"one"_n, ns1::row> a(get_self(), get_self().value);
      multi_index<"two"_n, ns2::row> b(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
   }
};
