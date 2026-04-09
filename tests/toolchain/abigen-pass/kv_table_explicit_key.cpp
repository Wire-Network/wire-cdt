#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

// Actual key struct used by the template
struct item_key {
   uint64_t category;
   uint64_t id;
   SYSLIB_SERIALIZE(item_key, (category)(id))
};

// Override key struct for ABI — different field names to prove override works
struct item_key_abi {
   uint64_t cat;
   uint64_t item_id;
   SYSLIB_SERIALIZE(item_key_abi, (cat)(item_id))
};

// [[sysio::kv_key("item_key_abi")]] overrides the auto-derived key from item_key
struct [[sysio::table("items"), sysio::kv_key("item_key_abi")]] item_val {
   std::string name;
   uint64_t    price;
   SYSLIB_SERIALIZE(item_val, (name)(price))
};

class [[sysio::contract("kv_table_explicit_key")]] kv_table_explicit_key : public contract {
public:
   using contract::contract;
   using items = kv::table<"items"_n, item_key, item_val>;

   [[sysio::action]]
   void test() {
      items tbl(get_self());
      tbl.emplace(get_self(), {1, 1}, {"widget", 100});
   }
};
