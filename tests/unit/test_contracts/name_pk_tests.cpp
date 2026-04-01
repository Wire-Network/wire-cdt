// Verifies that a table with name-typed primary key works

#include <sysio/kv_multi_index.hpp>
#include <sysio/contract.hpp>

struct [[sysio::table]] name_table {
    sysio::name pk;
    int num;

    auto primary_key() const { return pk; }
};
using name_table_idx = sysio::kv_multi_index<"name.pk"_n, name_table>;

class [[sysio::contract]] name_pk_tests : public sysio::contract {
 public:
   using sysio::contract::contract;

   [[sysio::action]] void write() {
       name_table_idx table(get_self(), 0);
       table.emplace(get_self(), [](auto& row) {
           row.pk = "alice"_n;
           row.num = 2;
       });
       table.emplace(get_self(), [](auto& row) {
           row.pk = "bob"_n;
           row.num = 1;
       });
   }

   [[sysio::action]] void read() {
       name_table_idx table(get_self(), 0);
       sysio::check(table.get("alice"_n).num == 2, "num mismatch");
       sysio::check(table.get("bob"_n).num == 1, "num mismatch");
   }
};
