// A rename frees the name its table was holding, and the next rename is entitled to it.
//
// Each annotation was decided against a `taken` set that the previous decision had already
// mutated, so the answer depended on visit order. With physical tables `alpha` and `bravo` and
// annotations alpha->bravo, bravo->charlie:
//
//   bravo first   -> bravo becomes charlie, alpha becomes bravo   {bravo, charlie}
//   alpha first   -> bravo is taken, refused; bravo becomes charlie {alpha, charlie}
//
// Both are self-consistent, and only the first uses the names the contract asked for. Across
// translation units that order is descriptor merge order, so identical sources could produce
// either ABI depending on which .desc the link happened to read first.
//
// The set is resolved to a fixed point now: a request waits until its target is free, and a
// target held by a table that is itself renamed away does come free. Sweeping until a sweep
// changes nothing reaches {bravo, charlie} from either end. A cycle never comes free and is
// refused with the ordinary occupied-name warning, which is the honest answer -- the ABI cannot
// hold the swap either.
//
// Both links in one TU (`s*`), and the same chain split across two (`x*`), because the fixed
// point and the merge order are separate ways to get the order wrong.
//
// Expected, and no warnings: sbravo, scharlie, xbravo, xcharlie.
#include "named_table_rename_chain_aux/rows.hpp"
#include <sysio/multi_index.hpp>

class [[sysio::contract("named_table_rename_chain")]] named_table_rename_chain : public sysio::contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      sysio::multi_index<"salpha"_n, s_a_row> a(get_self(), get_self().value);
      sysio::multi_index<"sbravo"_n, s_b_row> b(get_self(), get_self().value);
      sysio::multi_index<"xalpha"_n, x_a_row> c(get_self(), get_self().value);
      a.emplace(get_self(), [&](auto& r) { r.id = 1; });
      b.emplace(get_self(), [&](auto& r) { r.id = 2; });
      c.emplace(get_self(), [&](auto& r) { r.id = 3; });
   }
};
