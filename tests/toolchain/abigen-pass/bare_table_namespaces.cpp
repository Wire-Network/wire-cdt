// Two declarations sharing an unqualified name must not affect each other's tables.
//
// `ns1::row` and `ns2::row` both serialise as ABI type `row` -- the ABI cannot tell them apart,
// and never could. Any rule that decided which entries to remove by comparing that emitted
// `type` string therefore mistook one for the other: an earlier prune deleted `ns1::row`'s entry
// on the strength of `ns2::row`'s `archive` table.
//
// Nothing is removed now. `ns1::row` is annotated but never instantiated, so it contributes no
// table -- a table nothing can read or write is not described -- and `archive` is unaffected by
// its presence. Expected: `archive` alone, for the second reason and not the first.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

namespace ns1 {
   struct [[sysio::table, sysio::contract("bare_table_namespaces")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

namespace ns2 {
   struct [[sysio::table("archive"), sysio::contract("bare_table_namespaces")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

class [[sysio::contract("bare_table_namespaces")]] bare_table_namespaces : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      multi_index<"archive"_n, ns2::row> t(get_self(), get_self().value);
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
