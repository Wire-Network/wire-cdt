// A function-like macro cannot supply a sysio attribute's argument.
//
// The argument is read as source text, and at the macro's spelling location the token is the
// PARAMETER, not the literal the caller passed -- there is nothing there to read. An
// object-like macro is fine (abigen-pass/table_name_forms) because its body holds the literal.
//
// Refused rather than silently dropped: dropping it left the attribute looking bare, and an
// `_i` table then published the decode of its hash. The diagnostic says a macro parameter is
// the problem, since the author did write a plain literal, just not where this can see it.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

#define NAMED_TABLE(n) [[sysio::table(n)]]

class [[sysio::contract("attr_arg_from_macro")]] attr_arg_from_macro : public contract {
public:
   using contract::contract;

   struct NAMED_TABLE("fn_macro_table") row {
      uint64_t v;
      SYSLIB_SERIALIZE(row, (v))
   };

   using t = sysio::singleton<"fn_macro_table"_i, row>;

   [[sysio::action]]
   void test() {
      t x(get_self(), get_self().value);
      x.set({1}, get_self());
   }
};
