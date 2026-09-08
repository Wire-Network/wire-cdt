// The second translation unit: it sees the annotated row and never instantiates a table over
// it, so its descriptor carries the annotation with no table_id to go with it.
//
// Named to sort BEFORE named_table_xtu.cpp: cdt-codegen merges .desc files in sorted order, so
// this is the accumulator, and the real table_id has to be filled IN rather than merely kept.
// With the other order the merge succeeds whether or not it can fill one in.
#include "row.hpp"

[[sysio::action]] void other(uint64_t x) { (void)x; }
