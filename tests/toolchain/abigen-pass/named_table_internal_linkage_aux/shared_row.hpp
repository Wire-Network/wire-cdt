#pragma once
#include <sysio/sysio.hpp>

// ONE declaration, included by both translation units. Each gets its own distinct type -- an
// unnamed namespace is per-TU -- but it is a single declaration, and the annotation on it is a
// single annotation, so the two tables over it must group as one.
namespace {
   struct [[sysio::table, sysio::contract("named_table_internal_linkage")]] shared_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(shared_row, (id))
   };
}
