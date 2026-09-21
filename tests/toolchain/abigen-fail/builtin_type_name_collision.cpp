// An action method named after a builtin ABI type must be a DIAGNOSTIC, not a silent ABI.
//
// The wrapper struct abigen synthesizes for an action is named after the METHOD. Once
// `slug_name` joined the builtin set (wire-cdt #119), a method named `slug_name` produced a
// wrapper whose name is a builtin -- and `validate_struct` drops any such struct. The ABI then
// carried the action with `type: "slug_name"` and `structs: []`, so the host resolved the
// BUILTIN's shape (one 8-byte slug) while the generated dispatcher still deserialized the real
// parameter list (uint64 + uint32, 12 bytes). Nothing failed at build time and nothing failed at
// deploy time; the action was simply unusable, and no error pointed at why.
//
// Emitting the wrapper instead is not an option either -- the host rejects an ABI that defines a
// type it already knows intrinsically (`duplicate_abi_type_def_exception`). Since neither
// outcome is recoverable at run time, abigen refuses at compile time, where renaming the method
// costs nothing. The [[sysio::action("...")]] NAME is unaffected; only the C++ method spelling
// has to move.
#include <sysio/sysio.hpp>
#include <sysio/slug_name.hpp>

using namespace sysio;

class [[sysio::contract("builtin_type_name_collision")]] builtin_type_name_collision
   : public contract {
public:
   using contract::contract;

   [[sysio::action("regslug")]]
   void slug_name(uint64_t id, uint32_t flags) {
      check(flags != 0, "flags must be set");
      print(id);
   }
};
