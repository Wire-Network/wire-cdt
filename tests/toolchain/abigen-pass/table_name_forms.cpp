// Three spellings that must keep working, each of which stopped at some point in this branch.
//
//   a named attribute written inside a MACRO -- its expansion range ends at the invocation, so
//     the probe for `(` found nothing, the attribute read as bare, and the name was dropped.
//     `#define NAMED_TABLE [[sysio::table("x")]]` published the decoded hash of its `_i`
//     parameter. The argument is still at the SPELLING location, inside the macro body.
//
//   a DOTTED name -- `.` is part of the `_n` alphabet (`.12345a-z`), singleton_contract already
//     publishes `smpl.conf5`, and it round-trips the annotation encoding. Restricting the name
//     to the C++ identifier charset rejected a spelling that has always been valid.
//
//   a [[sysio::kv_key]] naming a struct at NAMESPACE SCOPE, from a row nested in the contract
//     class. The lookup checked types nested in the row and the row's immediate context and
//     stopped there, so a struct C++ finds without difficulty was reported as absent -- and
//     once the miss became an error, that was a build failure on working code.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

#define NAMED_TABLE [[sysio::table("macro_table_name")]]

// Complete here, and used by a row declared inside the contract class below.
struct logical_key {
   name owner;
   SYSLIB_SERIALIZE(logical_key, (owner))
};

class [[sysio::contract("table_name_forms")]] table_name_forms : public contract {
public:
   using contract::contract;

   struct NAMED_TABLE macro_row {
      uint64_t v;
      SYSLIB_SERIALIZE(macro_row, (v))
   };

   struct [[sysio::table("foo.bar")]] dotted_row {
      uint64_t v;
      SYSLIB_SERIALIZE(dotted_row, (v))
   };

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   struct [[sysio::table, sysio::kv_key("logical_key")]] val {
      uint64_t balance;
      SYSLIB_SERIALIZE(val, (balance))
   };

   using named  = sysio::singleton<"macro_table_name"_i, macro_row>;
   using dotted = sysio::singleton<"foo.bar"_n, dotted_row>;
   using rows   = kv::table<"rows"_n, phys_key, val>;

   [[sysio::action]]
   void test() {
      named  a(get_self(), get_self().value);
      dotted b(get_self(), get_self().value);
      rows   c(get_self());
      a.set({1}, get_self());
      b.set({2}, get_self());
      c.emplace(get_self(), {3}, {30});
   }
};
