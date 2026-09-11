// A [[sysio::kv_key]] override declared INSIDE the value row must be found.
//
// add_kv_table searched only the enclosing context, while add_table -- reading the identical
// attribute -- searched nested types first and then the enclosing one. A key struct nested in
// the value row was therefore invisible to the kv::table path, which fell back to the PHYSICAL
// key and advertised its field name. The two paths now search in the same order.
//
// Expected: key_names ["account_id"], the logical override, not the physical "raw_id".
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_key_nested")]] kv_key_nested : public contract {
public:
   using contract::contract;

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   struct [[sysio::table("realname"), sysio::kv_key("abi_key")]] val {
      struct abi_key {
         uint64_t account_id;
         SYSLIB_SERIALIZE(abi_key, (account_id))
      };
      uint64_t balance;
      SYSLIB_SERIALIZE(val, (balance))
   };

   using accounts = kv::table<"realname"_n, phys_key, val>;

   [[sysio::action]]
   void test() {
      accounts tbl(get_self());
      tbl.emplace(get_self(), {1}, {100});
   }
};
