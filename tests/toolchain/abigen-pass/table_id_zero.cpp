// A table_id of zero is an id, not the absence of one.
//
// compute_table_id DJB2-hashes the eight big-endian bytes of the table parameter and truncates
// to uint16_t, so zero is one of the 65536 values it returns -- `"rzy2"_n` reaches it. The
// descriptor wrote the key only `if (t.table_id != 0)`, which reads a real id as a missing one:
// the entry shipped without a table_id, and everything downstream that asks `has_key("table_id")`
// -- the collision check most of all -- skipped the table entirely. Two tables aliasing runtime
// id 0 therefore validated clean. abigen-fail/table_id_zero_collision is that pair.
//
// Presence is tracked separately from value now. Only an instantiation carries an id at all, so
// absence still has to be representable -- a table an annotation declares has none -- which is
// why the fix is a flag rather than a sentinel.
//
// Expected: "table_id": 0 present in the entry.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("table_id_zero")]] table_id_zero : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"rzy2"_n, row> t(get_self(), get_self().value);
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
