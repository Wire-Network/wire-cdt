#include <sysio/sysio.hpp>
using namespace sysio;

CONTRACT hello : public contract {
   public:
      using contract::contract;

      ACTION hi( name nm ) {
         print_f("Name : %\n", nm);
      }
};
