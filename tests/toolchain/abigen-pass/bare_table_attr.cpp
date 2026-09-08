// A bare [[sysio::table]] names no table, so the table's name comes from the instantiation.
//
// This is the standard Antelope idiom -- `struct [[eosio::table]] account` beside
// `multi_index<"accounts"_n, account>` -- and it is what a ported contract arrives with. abigen
// used the ROW STRUCT's name as a placeholder for the annotation, then emitted that placeholder
// alongside the entry the instantiation produced: two tables for one, the extra one under a
// table_id nothing ever writes to, so get_table_rows for it returned nothing.
//
// Expected here: one entry, "accounts", carrying the secondary index. `unused` covers the other
// direction -- a struct annotated but NEVER instantiated contributes no table at all, because
// nothing names one and nothing can read or write it. That is a deliberate behaviour change:
// such a struct previously got an entry under its own name.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("bare_table_attr")]] bare_table_attr : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] account {
      uint64_t id;
      name     owner;
      uint64_t primary_key() const { return id; }
      uint64_t by_owner()    const { return owner.value; }
      SYSLIB_SERIALIZE(account, (id)(owner))
   };

   struct [[sysio::table]] unused {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(unused, (id))
   };

   typedef multi_index<"accounts"_n, account,
      indexed_by<"byowner"_n, const_mem_fun<account, uint64_t, &account::by_owner>>
   > accounts;

   [[sysio::action]]
   void test() {
      accounts tbl(get_self(), get_self().value);
      tbl.emplace(get_self(), [&](auto& r) { r.id = 1; r.owner = "alice"_n; });
   }
};
