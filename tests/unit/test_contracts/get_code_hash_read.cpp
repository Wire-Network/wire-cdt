#include <sysio/sysio.hpp>
#include <sysio/action.hpp>
#include <sysio/name.hpp>
#include <sysio/kv_multi_index.hpp>

#include "get_code_hash_table.hpp"

class [[sysio::contract]] get_code_hash_tests : public contract {
public:
   using contract::contract;

   using hash_table = kv_multi_index<name("code.hash"), code_hash>;

   // Read the old code's hash from database and verify new code's hash differs
   [[sysio::action]]
   void theaction() {
      require_auth(get_self());
      hash_table hashes(get_self(), get_self().value);

      auto hash = get_code_hash(get_self());
      check(hash != checksum256(), "Code hash should not be null");

      auto record = hashes.get(0, "Unable to find recorded hash");
      check(hash != record.hash, "Code hash has not changed");
      sysio::print("Old hash: ", record.hash, "; new hash: ", hash);
   }
};

