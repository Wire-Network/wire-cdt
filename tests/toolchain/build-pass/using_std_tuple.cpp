/*
 * Regression test for https://github.com/EOSIO/sysio.cdt/issues/558
 *
 * Verifies that a class/function can be used from the std namespace
 */

#include <sysio/sysio.hpp>
#include <sysio/print.hpp>
#include <tuple>

using std::tuple;
using namespace sysio;

class[[sysio::contract("hello")]] hello : public contract
{
public:
   using contract::contract;

   [[sysio::action]] void hi(name user) {
      require_auth(user);
      print("Hello, ", user);
   }

   struct [[sysio::table]] greeting {
      uint64_t id;
      tuple<int, int> t;
      uint64_t primary_key() const { return id; }
   };
   typedef multi_index<"greeting"_n, greeting> greeting_index;
};
