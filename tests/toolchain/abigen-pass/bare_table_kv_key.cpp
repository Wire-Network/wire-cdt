// A bare [[sysio::table]] carrying [[sysio::kv_key]] must not lose that metadata to the prune.
//
// The placeholder entry is where the ctables path resolves kv_key into key_names/key_types. An
// earlier fix dropped the whole placeholder once a real table existed, taking the override with
// it: the surviving entry fell back to the PHYSICAL key struct's field name (`raw_id`) instead
// of the logical one the annotation names (`account_id`), silently changing the ABI clients and
// SHiP key decoding read.
//
// Expected: one table `realname`, with key_names ["account_id"] -- the override, not the
// physical field.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("bare_table_kv_key")]] bare_table_kv_key : public contract {
public:
   using contract::contract;

   struct phys_key {
      uint64_t raw_id;
      SYSLIB_SERIALIZE(phys_key, (raw_id))
   };

   struct abi_key {
      uint64_t account_id;
      SYSLIB_SERIALIZE(abi_key, (account_id))
   };

   struct [[sysio::table, sysio::kv_key("abi_key")]] val {
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
