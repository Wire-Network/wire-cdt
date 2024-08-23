#include <sysio/sysio.hpp>

class [[sysio::contract]] separate_cpp_hpp : sysio::contract {
public:
   using contract::contract;

   [[sysio::action]] void act();
};
