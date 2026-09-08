// A kv::table whose value type is a builtin, not a struct.
//
// The ABI type name is not the C++ record name: std::string's record is `basic_string`, and
// sysio::checksum256's is `fixed_bytes`. Publishing the record name named a type the document
// never declares, and the chain refuses such a document at set_abi with invalid_type_inside_abi
// -- a build that succeeds and a contract that cannot be deployed. The same defect sat in the
// multi_index/singleton path, where singleton_decl_forms pins it; this is the kv::table half.
//
// Nothing is declared for either row: `string` and `checksum256` are ABI builtins. The key
// struct still is, because a kv::table's key_names/key_types come from its fields.
//
// Expected: ckrow (key_names ["id"]) and strrow (key_names ["id"]).
#include <sysio/sysio.hpp>
#include <sysio/crypto.hpp>
#include <sysio/kv_table.hpp>
#include <string>

using namespace sysio;

class [[sysio::contract("kv_table_builtin_row")]] kv_table_builtin_row : public contract {
public:
   using contract::contract;

   struct row_key {
      uint64_t id;
      SYSLIB_SERIALIZE(row_key, (id))
   };

   using strings   = kv::table<"strrow"_n, row_key, std::string>;
   using checksums = kv::table<"ckrow"_n,  row_key, sysio::checksum256>;

   [[sysio::action]]
   void test() {
      strings   a(get_self());
      checksums b(get_self());
      a.emplace(get_self(), {1}, std::string("x"));
      b.emplace(get_self(), {2}, checksum256());
   }
};
