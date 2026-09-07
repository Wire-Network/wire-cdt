// A kv::global named with the _i literal keeps its readable [[sysio::table]] name in the ABI.
//
// add_table() decoded the raw template parameter with name_to_string(), which is right for _n
// -- there the raw IS a name encoding -- and wrong for _i, whose raw is a DJB2 hash. The ABI
// then carried a decoded-hash name ('5p.tmiidfxofh') beside the annotated one. Above 13
// characters both hashed to one table_id and the link failed with a table_id collision the
// author could not clear by renaming, because the twin derives from whatever name is chosen.
// kv::table never had this: add_kv_table() already preferred the annotation.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

class [[sysio::contract("hash_id_global")]] hash_id_global : public contract {
public:
   using contract::contract;

   struct [[sysio::table("app_configuration")]] app_config {
      uint64_t threshold;
      SYSLIB_SERIALIZE(app_config, (threshold))
   };

   [[sysio::action]]
   void test() {
      kv::global<"app_configuration"_i, app_config> cfg(get_self());
      cfg.set(app_config{1}, get_self());
   }
};
