#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// Two rows in ONE translation unit, both asking for `sshared`. Ordered by name alone, the
// second never entered abi_table_annotation's set and the resolver never saw it.
struct [[sysio::table("sshared"), sysio::contract("named_table_duplicate_target")]] s_one_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(s_one_row, (id))
};

struct [[sysio::table("sshared"), sysio::contract("named_table_duplicate_target")]] s_two_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(s_two_row, (id))
};
