#include "x_other.hpp"
#include <sysio/multi_index.hpp>

// The second half of the cross-TU clash. Neither TU can see that the other wants the same
// name, which is the whole reason the decision does not belong in abigen.
[[sysio::action]] void dup_other() {
   sysio::multi_index<"xsecond"_n, x_two_row> b(sysio::name{}, 0);
   (void)b;
}
