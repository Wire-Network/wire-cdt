// A kv::table keyed DIRECTLY on a builtin must publish that builtin as its one key leaf.
//
// `slug_name_builtin` covers the WRAPPED form -- a `code_key` struct holding one slug -- which is
// the shape wire-sysio's registries use. That fixture never exercised the direct form, and the
// direct form was broken: key_names/key_types are derived by walking the key type's FIELDS, and a
// builtin key is a single packed scalar with none, so the ABI carried `key_names: []` and
// `key_types: []`. A host cannot decode a next_key or encode a bound from an empty shape, and
// nothing downstream can recover what the key was -- wire-sysio #619's slug key codec included.
//
// Distinct from `abigen-fail/kv_table_scalar_key`: that case keys on `uint64_t`, a NON-CLASS type
// with no CXXRecordDecl at all, and is still refused. `sysio::slug_name` is a class (a derived
// struct, the same shape as `sysio::name`), so it reaches the field walk legitimately and is
// described as the single leaf it is -- matching the one-leaf shape `kv::global` already emits.
//
// Expected: table `codes` with key_names ["slug_name"] and key_types ["slug_name"], and NO
// slug_name struct_def or typedef anywhere in the ABI.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/slug_name.hpp>

using namespace sysio;

class [[sysio::contract("slug_name_direct_kv_key")]] slug_name_direct_kv_key : public contract {
public:
   using contract::contract;

   struct [[sysio::table("codes")]] code_row {
      uint64_t amount;
      SYSLIB_SERIALIZE(code_row, (amount))
   };

   /// Keyed on the builtin itself, with no wrapper struct.
   using codes = kv::table<"codes"_n, sysio::slug_name, code_row>;

   [[sysio::action]]
   void reg(sysio::slug_name code, uint64_t amount) {
      codes tbl(get_self());
      tbl.emplace(get_self(), code, {amount});
   }
};
