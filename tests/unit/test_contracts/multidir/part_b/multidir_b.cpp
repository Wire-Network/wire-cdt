// Part B of the multidir_contract regression contract: a different source subdirectory
// (multidir/part_b/). Defines the `setb` action handler and the `multidir_doubled` helper that
// part_a/ calls. See multidir/shared/multidir_contract.hpp for the design rationale.
#include "../shared/multidir_contract.hpp"

uint64_t multidir_doubled(uint64_t v) {
   return v * 2;
}

void multidir_contract::setb(uint64_t v) {
   sysio::check(v != 0, "setb requires a nonzero value");
}
