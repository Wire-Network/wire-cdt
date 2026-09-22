// A REFERENCED declaration whose ABI spelling collides with a builtin must be a diagnostic.
//
// Sibling of `builtin_type_name_collision`, which covers an action METHOD named after a builtin.
// This is the other half, and it takes a different path: `add_type()` short-circuits on any type
// whose translated, namespace-stripped spelling is builtin, so a user declaration named
// `slug_name` is never described and never reaches `add_struct`'s guard at all. The contract
// compiled with exit 0 and emitted `payload` as type `slug_name` with no struct for it -- so with
// wire-sysio#619 installed the host resolves the BUILTIN (8 bytes) while the generated dispatcher
// still deserializes uint64 + uint32 (12 bytes). Silent, and unusable.
//
// The check therefore runs BEFORE that short-circuit, and is declaration-aware: `sysio::` is
// where CDT declares the builtins that are real types and `std::` is where `string` comes from,
// so a record or enum outside both whose spelling collides is the author's own. An `enum class
// slug_name` fails identically -- same path, same diagnostic.
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract("builtin_type_name_collision_decl")]] builtin_type_name_collision_decl
   : public contract {
public:
   using contract::contract;

   struct slug_name {
      uint64_t id;
      uint32_t flags;
      SYSLIB_SERIALIZE(slug_name, (id)(flags))
   };

   [[sysio::action]]
   void reg(slug_name payload) {
      print(payload.id);
   }
};
