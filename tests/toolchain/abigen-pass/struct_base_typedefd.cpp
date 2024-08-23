/*
 * Regression test for https://github.com/EOSIO/sysio.cdt/issues/601.
 *
 * Verifies that a struct can inherit from a typedef'd class/struct.
 */

#include <sysio/sysio.hpp>

using namespace sysio;

struct foo {
   int value;
};

using bar = foo;

struct baz : bar {
};

class [[sysio::contract]] struct_base_typedefd : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void hi(baz b) {
      print(b.value);
   }
};
