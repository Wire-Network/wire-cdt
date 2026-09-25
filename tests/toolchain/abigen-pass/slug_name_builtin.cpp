// `sysio::slug_name` must reach the ABI as the BARE builtin spelling -- no struct_def,
// no typedef -- on both paths that can leak it: an action field type, and a kv table's
// key_types.
//
// It is a DERIVED STRUCT (`struct slug_name : basic_name<slug_name_traits>`), the same shape
// as `sysio::name`, precisely so the builtin match applies to the written spelling. As an
// ALIAS it would not: `is_aliasing` returns true and abigen emits
// `types: [slug_name -> basic_name_slug_name_traits_]` plus that struct, and the host resolves
// typedefs BEFORE its builtin lookup, so every slug field would silently serialize as
// `{"value": N}` again -- no error anywhere. That is what this fixture pins: the expected ABI
// below must contain NO slug_name struct and NO typedef.
//
// Expected: action field `code` of type "slug_name"; table `codes` with
// key_types ["slug_name"].
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/slug_name.hpp>

using namespace sysio;

class [[sysio::contract("slug_name_builtin")]] slug_name_builtin : public contract {
public:
   using contract::contract;

   struct code_key {
      sysio::slug_name code;
      SYSLIB_SERIALIZE(code_key, (code))
   };

   struct [[sysio::table("codes")]] code_row {
      uint64_t amount;
      SYSLIB_SERIALIZE(code_row, (amount))
   };

   using codes = kv::table<"codes"_n, code_key, code_row>;

   [[sysio::action]]
   void reg(sysio::slug_name code, uint64_t amount) {
      codes tbl(get_self());
      tbl.emplace(get_self(), {code}, {amount});
   }
};
