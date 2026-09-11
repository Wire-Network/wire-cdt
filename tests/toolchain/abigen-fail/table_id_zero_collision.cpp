// Two tables whose ids both hash to zero collide like any other pair.
//
// `"rzy2"_n` and `"s3hm"_n` are ordinary names that compute_table_id both maps to 0. While the
// descriptor omitted a zero id, neither entry carried one, the collision check's
// `has_key("table_id")` guard skipped both, and a contract writing two tables into one runtime
// id built and linked without a word -- the exact outcome that check exists to prevent.
//
// The same guard nested the secondary-index loop under the table's own id, so a table without
// one took its indexes out of the check too. A table an annotation DECLARES has no id, since
// only an instantiation carries one, so that was reachable without any zero involved.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("table_id_zero_collision")]] table_id_zero_collision : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] first_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(first_row, (id))
   };

   struct [[sysio::table]] second_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(second_row, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"rzy2"_n, first_row>  a(get_self(), get_self().value);
      multi_index<"s3hm"_n, second_row> b(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
   }
};
