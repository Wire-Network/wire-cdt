#include <sysio/sysio.hpp>
using namespace sysio;

enum test_chain_kind : uint8_t {
   CHAIN_KIND_UNKNOWN  = 0,
   CHAIN_KIND_WIRE     = 1,
   CHAIN_KIND_ETHEREUM = 2,
   CHAIN_KIND_SOLANA   = 3
};

enum class test_status : uint8_t {
   WARMUP     = 1,
   ACTIVE     = 2,
   COOLDOWN   = 3,
   TERMINATED = 240
};

CONTRACT abigen_test_enum_table : public contract {
   public:
      using contract::contract;
      abigen_test_enum_table( name receiver, name code, datastream<const char*> ds )
         : contract(receiver, code, ds), outposts(receiver, receiver.value),
           operators(receiver, receiver.value) {}

      ACTION regoutpost( test_chain_kind chain_kind, uint32_t chain_id );
      ACTION regoperator( name account, test_status status );

      // Simple enum field in table struct
      TABLE outpost_info {
         uint64_t         id;
         test_chain_kind  chain_kind;
         uint32_t         chain_id;
         uint64_t primary_key() const { return id; }
      };

      // Enum inside nested container (std::vector<std::pair<enum, T>>)
      TABLE operator_info {
         name         account;
         test_status  status;
         std::vector<std::pair<test_chain_kind, checksum256>> chain_addresses;
         uint64_t primary_key() const { return account.value; }
      };

      using outposts_t  = sysio::multi_index<"outposts"_n, outpost_info>;
      using operators_t = sysio::multi_index<"operators"_n, operator_info>;

      using regoutpost_action  = action_wrapper<"regoutpost"_n, &abigen_test_enum_table::regoutpost>;
      using regoperator_action = action_wrapper<"regoperator"_n, &abigen_test_enum_table::regoperator>;

      outposts_t  outposts;
      operators_t operators;
};

ACTION abigen_test_enum_table::regoutpost( test_chain_kind chain_kind, uint32_t chain_id ) {
   require_auth(get_self());
   outposts.emplace(get_self(), [&](auto& o) {
      o.id = outposts.available_primary_key();
      o.chain_kind = chain_kind;
      o.chain_id = chain_id;
   });
}

ACTION abigen_test_enum_table::regoperator( name account, test_status status ) {
   require_auth(get_self());
   operators.emplace(get_self(), [&](auto& o) {
      o.account = account;
      o.status  = status;
   });
}
