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

   // An empty argument encodes identically to no argument, so every later check reads it as a
   // bare attribute and the validation meant for a written name is never reached -- an `_i`
   // table with this annotation published the decode of its hash. Refused where the difference
   // is still visible.
   struct [[sysio::table("")]] emptied {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(emptied, (id))
   };

   // The GNU spelling takes the other parse path entirely -- clang hands it a cooked
   // StringLiteral -- so an empty check confined to the C++11 branch never saw it.
   struct __attribute__((sysio_table(""))) gnu_emptied {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(gnu_emptied, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"a"_n, spliced> a(get_self(), get_self().value);
      multi_index<"b"_n, escaped> b(get_self(), get_self().value);
      multi_index<"c"_n, spaced>  c(get_self(), get_self().value);
      multi_index<"d"_n, emptied> d(get_self(), get_self().value);
      multi_index<"e"_n, gnu_emptied> e(get_self(), get_self().value);
   }
};
