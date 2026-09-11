// A [[sysio::table("name")]] whose table is instantiated in ANOTHER translation unit.
//
// The rename is resolved per descriptor, so a TU that only sees the annotation cannot know the
// table's real table_id -- that comes from the template parameter, which lives in the TU that
// instantiates it. abigen used to derive one from the annotation STRING instead, which is right
// only when the parameter happens to spell the same name. Here it does not: the two descriptors
// then disagreed on cfg's table_id and ABIMerger refused the link outright.
//
//   Error, ABI structs malformed : cfg already defined
//
// It was a regression from two correct changes meeting: #115 made the annotation's name win, so
// both TUs emit an entry called `cfg`, and #112 made table_is_same compare table_id strictly, so
// the disagreement throws rather than merging silently. Before them this linked and produced the
// real table beside a phantom.
//
// Nothing is guessed now. A descriptor with no instantiation omits table_id, absence is not a
// difference, and the merge takes the real value from the TU that has it.
//
// Expected: `cfg` alone, at 49879 -- f("cfgtbl"_n), the parameter's id, not f("cfg"_n) = 28383.
#include "named_table_xtu_aux/row.hpp"
#include <sysio/multi_index.hpp>

class [[sysio::contract("named_table_xtu")]] named_table_xtu : public sysio::contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      sysio::multi_index<"cfgtbl"_n, config_row> tbl(get_self(), get_self().value);
      tbl.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
