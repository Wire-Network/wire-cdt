// Part A of the multidir_contract regression contract: one source subdirectory (multidir/part_a/).
// Defines the `seta` action handler and calls a helper compiled in part_b/, forcing a genuine
// cross-subdirectory link edge. See multidir/shared/multidir_contract.hpp for the design rationale.
#include "../shared/multidir_contract.hpp"

void multidir_contract::seta(uint64_t v) {
   // Calls the helper defined in the OTHER subdirectory (part_b/), so both objects must link.
   sysio::check(multidir_doubled(v) == v * 2, "multidir helper mismatch");
}
