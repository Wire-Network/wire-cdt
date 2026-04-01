#include <sysio/sysio.hpp>
#include <sysio/action.hpp>
#include <sysio/name.hpp>
#include <sysio/kv_multi_index.hpp>

#include "get_code_hash_table.hpp"

class [[sysio::contract]] get_code_hash_tests : public contract {
public:
   using contract::contract;

   using hash_table = kv_multi_index<name("code.hash"), code_hash>;

   // Write this code's hash to database
   [[sysio::action]]
   void theaction() {
      require_auth(get_self());
      hash_table hashes(get_self(), get_self().value);

      auto hash = get_code_hash(get_self());
      check(hash != checksum256(), "Code hash should not be null");

      hashes.emplace(get_self(), [&hash](auto& t) {
         t.id = 0;
         t.hash = hash;
      });
   }
};

