// A map whose value is a container is refused, and it is the shape that proves the rule.
//
// nested_container_row's vector-of-vectors is refused whether or not resugaring stops at one
// level: arguments are not resugared, so the inner stays canonical and the outer never
// resolves. A map is different. Its ABI spelling is built from two arguments, and with the
// nesting check removed it resugars and publishes
//
//   type: pair_uint64_vector_unsigned_long_long_[]      types: []
//
// a document naming a type it does not declare, which the chain refuses at set_abi -- a build
// that succeeds and a contract that cannot be deployed. This is the one row that stops failing
// when the check goes, so it gets a fixture to itself: an abigen-fail test is satisfied by any
// non-zero exit carrying the expected text, and a row that fails regardless would mask it.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
#include <map>
#include <vector>

using namespace sysio;

class [[sysio::contract("nested_map_row")]] nested_map_row : public contract {
public:
   using contract::contract;

   using nestmap = sysio::singleton<"nestmap"_n, std::map<uint64_t, std::vector<uint64_t>>>;

   [[sysio::action]]
   void test() {
      nestmap m(get_self(), get_self().value);
      m.set({{1, {2}}}, get_self());
   }
};
