#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>
using namespace sysio;

// "ah" and "aa1" both hash to table_id 58136, triggering a collision error at abigen time.

struct my_key_ah {
   uint64_t id;
   SYSLIB_SERIALIZE(my_key_ah, (id))
};

struct [[sysio::table("ah")]] val_ah {
   uint64_t data;
   SYSLIB_SERIALIZE(val_ah, (data))
};

struct my_key_aa1 {
   uint64_t id;
   SYSLIB_SERIALIZE(my_key_aa1, (id))
};

struct [[sysio::table("aa1")]] val_aa1 {
   uint64_t data;
   SYSLIB_SERIALIZE(val_aa1, (data))
};

CONTRACT table_id_collision : public contract {
   public:
      using contract::contract;

      using ah_table = kv::table<"ah"_n, my_key_ah, val_ah>;
      using aa1_table = kv::table<"aa1"_n, my_key_aa1, val_aa1>;

      ACTION store(uint64_t id) {
         ah_table tbl1;
         tbl1.set(get_self(), {id}, {id});
         aa1_table tbl2;
         tbl2.set(get_self(), {id}, {id});
      }
};
