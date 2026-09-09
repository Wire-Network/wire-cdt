// A std::variant row is refused with a diagnostic, not an abort.
//
// This is the shape that made the resugar whitelist dangerous. `variant` and `tuple` hold their
// canonical template arguments as a single Pack, and get_template_argument() handles only Type,
// Integral and Expression -- a Pack falls through to CDT_INTERNAL_ERROR, which terminates the
// compiler:
//
//   Wrong type of template specialization
//   terminate called after throwing an instance of 'sysio::cdt::internal_error'
//
// Both are off the whitelist, so a canonical variant row keeps the ordinary path: it translates
// to a name the document does not declare, and the describability guard reports it against the
// contract class. abigen-fail/kv_table_scalar_key pins the same rule for a scalar kv::table key;
// this branch introduced an abort in that same family and this is what keeps it out.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
#include <string>
#include <variant>

using namespace sysio;

class [[sysio::contract("variant_row")]] variant_row : public contract {
public:
   using contract::contract;

   using v = sysio::singleton<"vtbl"_n, std::variant<uint64_t, std::string>>;

   [[sysio::action]]
   void test() {
      v t(get_self(), get_self().value);
      t.set(uint64_t{1}, get_self());
   }
};
