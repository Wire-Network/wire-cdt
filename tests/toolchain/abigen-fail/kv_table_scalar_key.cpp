// A kv::table key type must be a struct.
//
// key_names/key_types come from the key type's FIELDS, so a scalar key describes nothing. Asking
// a non-class type for its CXXRecordDecl gives null, and abigen dereferenced it: `clang frontend
// command failed with exit code 139`, no diagnostic, no ABI. A contract author's source -- legal
// C++ that the library itself accepts -- must produce a diagnostic, never a crash.
//
// The message is reported against the contract class: an implicit specialization's location is
// the template's definition inside sysiolib, which says nothing about the author's source.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

class [[sysio::contract("kv_table_scalar_key")]] kv_table_scalar_key : public contract {
public:
   using contract::contract;

   struct [[sysio::table("vals")]] val {
      uint64_t v;
      SYSLIB_SERIALIZE(val, (v))
   };

   using scalar_keyed = kv::table<"vals"_n, uint64_t, val>;

   [[sysio::action]]
   void test() {
      scalar_keyed t(get_self());
      t.emplace(get_self(), 1, {2});
   }
};
