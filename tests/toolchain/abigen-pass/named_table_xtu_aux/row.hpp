#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// A NAMED [[sysio::table]] row in a header shared by two translation units, whose name differs
// from the table parameter that instantiates it. That difference is the whole point: when the
// two agree, every TU derives the same table_id and nothing collides.
struct [[sysio::table("cfg"), sysio::contract("named_table_xtu")]] config_row {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(config_row, (id))
};
