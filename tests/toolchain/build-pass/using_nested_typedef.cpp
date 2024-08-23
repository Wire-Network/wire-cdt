/*
 * Regression test for https://github.com/EOSIO/sysio.cdt/issues/600
 *
 * Verifies that nested typedefs build.
 */

#include <sysio/sysio.hpp>
using namespace sysio;

namespace foo {
//using str = std::string;
typedef std::string str;
}

class [[sysio::contract]] using_nested_typedef : public contract {
public:
   using contract::contract;
   [[sysio::action]]
   void hi(foo::str s) {
      print(s);
   }
};
