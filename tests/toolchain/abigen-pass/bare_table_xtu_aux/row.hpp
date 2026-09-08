#pragma once
#include <sysio/sysio.hpp>
using namespace sysio;

// A bare [[sysio::table]] row, in a header shared by two translation units. Only one of them
// instantiates a table over it -- which is the whole point of this fixture.
struct [[sysio::table, sysio::contract("bare_table_xtu")]] account {
   uint64_t id;
   uint64_t primary_key() const { return id; }
   SYSLIB_SERIALIZE(account, (id))
};
