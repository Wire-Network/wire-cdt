// A [[sysio::kv_key]] override has to be visible where the table is instantiated.
//
// This is the translation unit whose key layout reaches the ABI, so it is the one that has to
// resolve the override. Falling back to the physical key used to be a warning, and the ABI then
// advertised field names the author did not ask for.
//
// No later pass can repair it. A second TU that happens to see the struct completed -- the row
// forward-declares it, say -- enriches only the ANNOTATION during the descriptor merge, while
// the live table keeps the physical names it was built with. Reconstructing the override onto an
// instantiated table afterwards means carrying the physical key's shape through the descriptor
// and reasoning about which leading fields are physical, which is the machinery this branch
// removed for getting that question wrong twice.
//
// So: an error, where the include is missing.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_key_not_visible")]] kv_key_not_visible : public contract {
public:
   using contract::contract;

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   struct logical_key;   // declared, never defined

   struct [[sysio::table, sysio::kv_key("logical_key")]] val {
      uint64_t balance;
      SYSLIB_SERIALIZE(val, (balance))
   };

   using rows = kv::table<"rows"_n, phys_key, val>;

   [[sysio::action]]
   void test() {
      rows t(get_self());
      t.emplace(get_self(), {1}, {10});
   }
};
