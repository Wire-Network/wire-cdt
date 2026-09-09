// A sysio attribute argument must be one plain string literal.
//
// The argument is read as source text, not as the compiler's cooked value, so an escape reaches
// the ABI uncooked and adjacent literals are truncated to the first -- each naming a table at
// another string's table_id. It is then encoded as `sysio_table(arg)` and split back out on
// [\s,]+, so whitespace and commas do not survive either.
//
// Refused in the attribute handler, before any of it becomes an ABI name.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("attr_arg_not_plain")]] attr_arg_not_plain : public contract {
public:
   using contract::contract;

   struct [[sysio::table("con" "catenated")]] spliced {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(spliced, (id))
   };

   struct [[sysio::table("esc\x61ped")]] escaped {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(escaped, (id))
   };

   struct [[sysio::table("has space")]] spaced {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(spaced, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"a"_n, spliced> a(get_self(), get_self().value);
      multi_index<"b"_n, escaped> b(get_self(), get_self().value);
      multi_index<"c"_n, spaced>  c(get_self(), get_self().value);
   }
};
