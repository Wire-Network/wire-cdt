/*
 * Regression test for https://github.com/EOSIO/sysio.cdt/issues/602.
 *
 * Verifies that an aliased type can be used as a variant template arg.
 */

#include <sysio/sysio.hpp>

using namespace sysio;

using str = std::string;

class [[sysio::contract]] aliased_type_variant_template_arg : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void hi(std::variant<uint64_t,str> v) {
      if (std::holds_alternative<uint64_t>(v)) {
      } else if (std::holds_alternative<str>(v)) {
      }
   }
};
