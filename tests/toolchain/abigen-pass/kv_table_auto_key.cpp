#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

struct order_key {
   std::string region;
   uint64_t    seq;
   SYSLIB_SERIALIZE(order_key, (region)(seq))
};

// No [[sysio::table]], no [[sysio::kv_key]] — everything derived from template
struct order_val {
   uint64_t amount;
   name     buyer;
   SYSLIB_SERIALIZE(order_val, (amount)(buyer))
};

class [[sysio::contract("kv_table_auto_key")]] kv_table_auto_key : public contract {
public:
   using contract::contract;
   using orders = kv::table<"orders"_n, order_key, order_val>;

   [[sysio::action]]
   void test() {
      orders tbl(get_self());
      tbl.emplace(get_self(), {"us", 1}, {100, "alice"_n});
   }
};
