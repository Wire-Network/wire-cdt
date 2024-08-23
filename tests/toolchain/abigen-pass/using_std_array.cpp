#include <sysio/sysio.hpp>
#include <sysio/print.hpp>
#include <array>

using std::array;
using namespace sysio;

class[[sysio::contract("using_std_array")]] using_std_array : public contract
{
public:
   using contract::contract;

   [[sysio::action]] void hi(name user) {
      require_auth(user);
      print("Hello, ", user);
   }

   struct [[sysio::table]] greeting {
      uint64_t id;
      array<int, 32> t;
      uint64_t primary_key() const { return id; }
   };
   typedef multi_index<"greeting"_n, greeting> greeting_index;
};
