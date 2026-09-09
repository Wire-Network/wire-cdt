#include "rows.hpp"
#include <sysio/multi_index.hpp>

// The far end of the cross-TU chain: this TU holds `xbravo`, which is the name the OTHER
// TU's annotation is waiting for. Neither side can see the chain it is part of.
[[sysio::action]] void chain_other() {
   sysio::multi_index<"xbravo"_n, x_b_row> b(sysio::name{}, 0);
   (void)b;
}
