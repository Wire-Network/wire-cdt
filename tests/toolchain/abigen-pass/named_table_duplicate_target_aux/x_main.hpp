#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// Half of the cross-TU clash, in a header only the main translation unit includes -- so this
// annotation reaches the descriptor merge without ever meeting its rival in a set. A SHARED
// header would not do: there the per-TU set drops one of the pair before the merge is reached,
// both descriptors record the same survivor, and the merge sees nothing to reconcile.
struct [[sysio::table("xshared"), sysio::contract("named_table_duplicate_target")]] x_one_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_one_row, (id))
};
