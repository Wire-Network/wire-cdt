// A container OF a container is not a row type the ABI can name here.
//
// A single-level container is -- abigen-pass/singleton_container_row -- because the canonical
// type is resugared and translate_type()'s container branch spells it `uint64[]`. Nesting is
// deliberately left canonical instead.
//
// The machinery that names a nested container (`B_vector_uint64_E`, a typedef for `uint64[]`)
// is driven off the same printed form those helpers count '<' in, and it declares the typedef
// the name depends on. A half-resugared type slipped through it: the table published
// `B_vector_uint64_E[]` while `types` stayed empty, so the document named a type it did not
// declare and the chain refuses it at set_abi -- a build that succeeds and a contract that
// cannot be deployed, which is exactly what the describability guard exists to prevent.
//
// Refusing at the declaration is what master did for every container, and is what this keeps
// doing for the nested ones until they are supported deliberately rather than by accident.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
#include <vector>

using namespace sysio;

class [[sysio::contract("nested_container_row")]] nested_container_row : public contract {
public:
   using contract::contract;

   using nested = sysio::singleton<"nestsing"_n, std::vector<std::vector<uint64_t>>>;

   [[sysio::action]]
   void test() {
      nested t(get_self(), get_self().value);
      t.set({{1, 2}}, get_self());
   }
};
