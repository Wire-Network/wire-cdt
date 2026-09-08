// One row struct, two tables. Each instantiation must get its own ABI entry.
//
// A bare [[sysio::table]] used to emit a placeholder entry named after the ROW STRUCT, to be
// pruned once a real table for that struct appeared. That is unsound here: the placeholder is
// named `account`, and so is one of the two real tables, so the two collided in the by-name set
// and the "placeholder" WAS the live table. The prune then saw `accounts` as proof it had been
// superseded and deleted it -- the contract wrote two tables and the ABI described one.
//
// No placeholder is emitted now, so each instantiation stands on its own. Expected: both
// `account` (15140) and `accounts` (25660).
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("bare_table_two_names")]] bare_table_two_names : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] account {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(account, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"account"_n,  account> a(get_self(), get_self().value);
      multi_index<"accounts"_n, account> b(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
   }
};
