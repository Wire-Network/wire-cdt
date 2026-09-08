// A bare [[sysio::table]] must not survive into the ABI when ANOTHER translation unit is the
// one that names the table.
//
// abigen emits a placeholder entry named after the row struct for a bare attribute, because the
// real name comes from the multi_index / kv::table that instantiates it -- and that may be in a
// different TU, so no single Clang invocation can decide. An earlier fix suppressed the
// placeholder within one invocation, which held for a single-source contract and silently
// failed here: the second TU re-added it and the descriptor merge kept both `accounts` and
// `account`.
//
// Expected: `accounts` alone. The second source is passed through compile_flags and lives in a
// subdirectory, so the suite does not discover it as a test of its own.
#include "bare_table_xtu_aux/row.hpp"
#include <sysio/multi_index.hpp>

typedef sysio::multi_index<"accounts"_n, account> accounts;

class [[sysio::contract("bare_table_xtu")]] bare_table_xtu : public sysio::contract {
public:
   using contract::contract;

   [[sysio::action]]
   void test() {
      accounts tbl(get_self(), get_self().value);
      tbl.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
