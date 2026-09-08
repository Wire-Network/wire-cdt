#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// config_row backs one table in each translation unit, so neither TU alone can see that the
// annotation names two.
struct [[sysio::table("cfg"), sysio::contract("named_table_split")]] config_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(config_row, (id))
};

// a_row's annotation asks for `alpha`, which the OTHER translation unit's table over b_row
// already holds -- again invisible from either side alone.
struct [[sysio::table("alpha"), sysio::contract("named_table_split")]] a_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(a_row, (id))
};

struct [[sysio::table, sysio::contract("named_table_split")]] b_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(b_row, (id))
};
