// A table row is a struct. Everything else is refused, with one message.
//
// This is the rule upstream CDT has always had -- its add_table() takes a CXXRecordDecl and
// dereferences it, so a non-struct row was never supported there either -- and keeping it means
// a contract ported from another Antelope chain behaves here exactly as it did there. It also
// keeps the ABI self-describing: a table's rows are named fields, not a bare scalar or a
// container whose shape lives only in the C++.
//
// Wire briefly described some of these. The cost was not worth it: a scalar row crashed the
// compiler outright, containers needed template-sugar reconstruction that mis-declared nested
// ones, `variant` and `tuple` aborted on a canonical argument pack, and `binary_extension`
// produced a `uint64$` table type the chain refuses at set_abi. Each is one line of contract
// code away from a shape that works -- wrap the value in a struct -- and the struct is what the
// ABI needs in order to describe it at all.
//
// Every row below is refused by the same diagnostic, reported against the contract class: an
// implicit specialization's own location is the template inside sysiolib, which tells the
// author nothing about their source.
#include <sysio/sysio.hpp>
#include <sysio/binary_extension.hpp>
#include <sysio/crypto.hpp>
#include <sysio/singleton.hpp>
#include <map>
#include <string>
#include <variant>
#include <vector>

using namespace sysio;

class [[sysio::contract("row_must_be_a_struct")]] row_must_be_a_struct : public contract {
public:
   using contract::contract;

   using scalar    = sysio::singleton<"scalarrow"_n, uint64_t>;                       // a builtin
   using text      = sysio::singleton<"textrow"_n,   std::string>;                    // a builtin, and a class
   using digest    = sysio::singleton<"digestrow"_n, sysio::checksum256>;             // fixed_bytes<32>
   using list      = sysio::singleton<"listrow"_n,   std::vector<uint64_t>>;          // a container
   using nested    = sysio::singleton<"nestedrow"_n, std::map<uint64_t, std::vector<uint64_t>>>;
   using choice    = sysio::singleton<"choicerow"_n, std::variant<uint64_t, bool>>;   // a canonical pack
   using extension = sysio::singleton<"extrow"_n,    sysio::binary_extension<uint64_t>>;

   [[sysio::action]]
   void test() {
      scalar    a(get_self(), get_self().value);
      text      b(get_self(), get_self().value);
      digest    c(get_self(), get_self().value);
      list      d(get_self(), get_self().value);
      nested    e(get_self(), get_self().value);
      choice    f(get_self(), get_self().value);
      extension g(get_self(), get_self().value);
   }
};
