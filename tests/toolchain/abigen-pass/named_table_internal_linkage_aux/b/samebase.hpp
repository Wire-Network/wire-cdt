#pragma once
#include <sysio/sysio.hpp>

// One of a PAIR of headers sharing a basename, each declaring an anonymous `same` at the same
// offset. A basename-keyed identity collapses the two into one declaration; a canonical path
// keeps them apart.
namespace {
   struct [[sysio::table("bravo"), sysio::contract("named_table_internal_linkage")]] same {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(same, (id))
   };
}
