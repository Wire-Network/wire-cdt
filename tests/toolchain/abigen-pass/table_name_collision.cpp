// Two different tables under one ABI name are reported, not dropped in silence.
//
// abi_table is ordered by NAME, so the second insertion into the set is discarded. That happens
// inside the plugin, before any descriptor is written, so no cdt-codegen check -- not the
// table_id collision pass, not the annotation resolver -- can see that a table went missing. The
// contract writes two tables and the ABI describes one.
//
// Two kv::tables over one row differing only in their KEY struct are the reachable shape: same
// name, same table_id, same row type, same ABI type. Comparing only those four called them the
// same table and kept whichever came first, so the published key layout depended on declaration
// order and nothing said so.
//
// Repeated instantiations of ONE table are the ordinary case -- a table constructed in several
// actions -- and must stay silent. `dup` below is instantiated twice for that reason.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("table_name_collision")]] table_name_collision : public contract {
public:
   using contract::contract;

   struct by_id    { uint64_t id;    SYSLIB_SERIALIZE(by_id, (id)) };
   struct by_owner { name     owner; SYSLIB_SERIALIZE(by_owner, (owner)) };

   struct [[sysio::table]] row {
      uint64_t balance;
      SYSLIB_SERIALIZE(row, (balance))
   };

   struct [[sysio::table]] drow {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(drow, (id))
   };

   using keyed_by_id    = kv::table<"same"_n, by_id, row>;
   using keyed_by_owner = kv::table<"same"_n, by_owner, row>;

   [[sysio::action]]
   void test() {
      keyed_by_id    a(get_self());
      keyed_by_owner b(get_self());
   }

   [[sysio::action]]
   void one() { multi_index<"dup"_n, drow> t(get_self(), get_self().value); t.emplace(get_self(), [&](auto& r){ r.id = 1; }); }

   [[sysio::action]]
   void two() { multi_index<"dup"_n, drow> t(get_self(), get_self().value); t.emplace(get_self(), [&](auto& r){ r.id = 2; }); }
};
