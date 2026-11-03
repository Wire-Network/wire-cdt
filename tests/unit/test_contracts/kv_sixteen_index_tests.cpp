// The most indices kv::table allows, moved in one write. Each index needs two 260-byte key streams, so with all of them
// alive at once a table this size would outgrow the 8 KiB default stack and trap.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_sixteen_index_tests")]] kv_sixteen_index_tests : public contract {
public:
   using contract::contract;

   struct row_key {
      uint64_t id;
      SYSLIB_SERIALIZE(row_key, (id))
   };

   struct [[sysio::table("sixteen")]] row_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(row_val, (balance))
   };

   /// A different key per index, so that every index has an entry to move.
   template<uint64_t Number>
   struct balance_of {
      using result_type = uint64_t;
      uint64_t operator()(const row_val& row) const { return row.balance + Number; }
   };

   using sixteen_table = kv::table<"sixteen"_n, row_key, row_val,
      kv::index<"ia"_n, balance_of<0>>,  kv::index<"ib"_n, balance_of<1>>,   kv::index<"ic"_n, balance_of<2>>,
      kv::index<"id"_n, balance_of<3>>,  kv::index<"ie"_n, balance_of<4>>,   kv::index<"if"_n, balance_of<5>>,
      kv::index<"ig"_n, balance_of<6>>,  kv::index<"ih"_n, balance_of<7>>,   kv::index<"ii"_n, balance_of<8>>,
      kv::index<"ij"_n, balance_of<9>>,  kv::index<"ik"_n, balance_of<10>>,  kv::index<"il"_n, balance_of<11>>,
      kv::index<"im"_n, balance_of<12>>, kv::index<"in"_n, balance_of<13>>,  kv::index<"io"_n, balance_of<14>>,
      kv::index<"ip"_n, balance_of<15>>>;

   [[sysio::action]]
   void moveall() {
      sixteen_table rows(get_self());
      rows.emplace(get_self(), {1}, {10});
      rows.modify(get_self(), {1}, [](row_val& row) { row.balance = 20; });
      auto last_index = rows.get_index<"ip"_n>();
      check(last_index.find(uint64_t{35}) != last_index.end(), "the last index moved");
      rows.erase({1});
      check(last_index.find(uint64_t{35}) == last_index.end(), "the last index's entry went with the row");
   }
};
