#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// Two rows in ONE translation unit, both asking for `sshared`.
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

// The same clash, with its two tables in different translation units.
struct [[sysio::table("xshared"), sysio::contract("named_table_duplicate_target")]] x_one_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_one_row, (id))
};

struct [[sysio::table("xshared"), sysio::contract("named_table_duplicate_target")]] x_two_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_two_row, (id))
};
