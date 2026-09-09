// A dotted table name is valid and must stay valid.
//
// `.` is part of the `_n` alphabet (`.12345a-z`) and singleton_contract already publishes
// `smpl.conf5`, so the charset a [[sysio::table("name")]] is checked against is letters, digits,
// underscore and dot -- not the C++ identifier set, which rejected a spelling that has always
// worked. abigen-fail/table_name_charset is the other side of it.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

class [[sysio::contract("table_name_forms")]] table_name_forms : public contract {
public:
   using contract::contract;

   struct [[sysio::table("foo.bar")]] dotted_row {
      uint64_t v;
      SYSLIB_SERIALIZE(dotted_row, (v))
   };

   using dotted = sysio::singleton<"foo.bar"_n, dotted_row>;

   [[sysio::action]]
   void test() {
      dotted b(get_self(), get_self().value);
      b.set({2}, get_self());
   }
};
