#include <sysio/sysio.hpp>
#include <sysio/kv_indexed_table.hpp>

using namespace sysio;

// Test: kv::indexed_table with [[sysio::kv_key]] generates correct ABI
// key metadata for multi-field key structs.
class [[sysio::contract("kv_indexed_table_key")]] kv_indexed_table_key : public contract {
   public:
      using contract::contract;

      // Multi-field key struct
      struct order_key {
         uint8_t     tag;
         std::string market;
         uint64_t    seq;
         SYSLIB_SERIALIZE(order_key, (tag)(market)(seq))
      };

      struct [[sysio::table("orders"), sysio::kv_key("order_key")]] order_val {
         name     trader;
         uint64_t price;
         uint64_t get_price() const { return price; }
         SYSLIB_SERIALIZE(order_val, (trader)(price))
      };

      using orders = kv::indexed_table<"orders"_n, order_key, order_val,
         kv::index<"byprice"_n, const_mem_fun<order_val, uint64_t, &order_val::get_price>>
      >;

      [[sysio::action]]
      void test() {}
};
