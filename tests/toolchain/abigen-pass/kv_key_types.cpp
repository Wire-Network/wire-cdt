#include <sysio/sysio.hpp>
#include <sysio/kv_multi_index.hpp>

using namespace sysio;

// Test: kv_multi_index tables get auto key metadata,
// and [[sysio::kv_key]] provides custom key metadata.
//
// `custom` is annotated but never instantiated, so it carries no table_id: only an instantiation
// has one. See named_table_xtu for why guessing it from the annotation string is worse than
// omitting it.
class [[sysio::contract("kv_key_types")]] kv_key_types : public contract {
   public:
      using contract::contract;

      // Auto key metadata via kv_multi_index template
      struct [[sysio::table]] kvrow {
         uint64_t    id;
         std::string data;
         uint64_t primary_key() const { return id; }
      };
      typedef kv_multi_index<"kvrows"_n, kvrow> kvrows_table;

      // Custom key metadata via [[sysio::kv_key]]
      struct my_key {
         std::string region;
         uint64_t    id;
         SYSLIB_SERIALIZE(my_key, (region)(id))
      };

      struct [[sysio::table("custom"), sysio::kv_key("my_key")]] my_value {
         std::string payload;
         SYSLIB_SERIALIZE(my_value, (payload))
      };

      [[sysio::action]]
      void test() {};
};
