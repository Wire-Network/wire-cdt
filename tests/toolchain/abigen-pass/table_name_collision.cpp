// Two different tables under one ABI name are reported, not dropped in silence.
//
// abi_table is ordered by NAME, so the second insertion into the set is discarded. That happens
// inside the plugin, before any descriptor is written, so no cdt-codegen check -- not the
// table_id collision pass, not the annotation resolver -- can see that a table went missing. The
// contract writes two tables and the ABI describes one.
//
// It became reachable when an `_i` name could equal an `_n` one: `"cfg"_n` encodes to a name and
// `"cfg"_i` hashes to a different table_id, and once the `_i` spelling is recovered both entries
// ask to be called `cfg`. The ABI genuinely cannot hold both, so the entry is still dropped --
// but the author is told which two collided, with the table_id and row type of each.
//
// Repeated instantiations of ONE table are the ordinary case -- a table constructed in several
// actions -- and must stay silent. `dup` below is instantiated twice for that reason.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/multi_index.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

class [[sysio::contract("table_name_collision")]] table_name_collision : public contract {
public:
   using contract::contract;

   struct arow {
      uint64_t v;
      SYSLIB_SERIALIZE(arow, (v))
   };

   struct [[sysio::table]] drow {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(drow, (id))
   };

   using by_n = sysio::singleton<"cfg"_n, arow>;
   using by_i = sysio::singleton<"cfg"_i, uint64_t>;

   [[sysio::action]]
   void test() {
      by_n a(get_self(), get_self().value);
      by_i b(get_self(), get_self().value);
      a.set({1}, get_self());
      b.set(2, get_self());
   }

   [[sysio::action]]
   void one() { multi_index<"dup"_n, drow> t(get_self(), get_self().value); t.emplace(get_self(), [&](auto& r){ r.id = 1; }); }

   [[sysio::action]]
   void two() { multi_index<"dup"_n, drow> t(get_self(), get_self().value); t.emplace(get_self(), [&](auto& r){ r.id = 2; }); }
};
