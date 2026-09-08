// The second translation unit. Named to sort before named_table_split.cpp so it is the merge
// accumulator, the harder order.
#include "row.hpp"
#include <sysio/multi_index.hpp>

[[sysio::action]] void other() {
   sysio::multi_index<"cfgtwo"_n, config_row> a(sysio::name{}, 0);
   sysio::multi_index<"alpha"_n,  b_row>      b(sysio::name{}, 0);
   (void)a; (void)b;
}
