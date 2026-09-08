// The second translation unit: it SEES the annotated row and never instantiates a table over
// it, so its descriptor carries the placeholder entry with nothing to supersede it. The prune
// therefore cannot be decided per-invocation -- it has to happen after the descriptors merge.
#include "row.hpp"

[[sysio::action]] void other(uint64_t x) { (void)x; }
