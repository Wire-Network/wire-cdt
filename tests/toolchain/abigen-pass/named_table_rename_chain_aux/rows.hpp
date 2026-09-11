#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// Same translation unit: salpha -> sbravo, and sbravo -> scharlie.
struct [[sysio::table("sbravo"), sysio::contract("named_table_rename_chain")]] s_a_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(s_a_row, (id))
};

struct [[sysio::table("scharlie"), sysio::contract("named_table_rename_chain")]] s_b_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(s_b_row, (id))
};

// The same chain, with its two links in different translation units.
struct [[sysio::table("xbravo"), sysio::contract("named_table_rename_chain")]] x_a_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_a_row, (id))
};

struct [[sysio::table("xcharlie"), sysio::contract("named_table_rename_chain")]] x_b_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(x_b_row, (id))
};
