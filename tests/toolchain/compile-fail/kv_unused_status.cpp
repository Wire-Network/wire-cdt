// A status-returning KV write must not be dropped by accident: the host now returns a duplicate key
// or a missing row instead of throwing, so ignoring the result turns a failed write into a silent
// success. kv.h marks all five writes warn_unused_result; this pins that each one keeps the mark.
#include <sysio/kv_utils.hpp>

void drops_statuses() {
   kv_set(0, 1, "k", 1, "v", 1, kv_set_upsert);
   kv_erase(0, "k", 1);
   kv_idx_store(0, 1, "p", 1, "s", 1);
   kv_idx_update(0, 1, "p", 1, "s", 1, "t", 1);
   kv_idx_erase(0, "p", 1, "s", 1);
}

extern "C" void apply(uint64_t, uint64_t, uint64_t) { drops_statuses(); }
