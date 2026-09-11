#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// The other half, in a header only the second translation unit includes.
struct [[sysio::table("xshared"), sysio::contract("named_table_duplicate_target")]] x_two_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_two_row, (id))
};
