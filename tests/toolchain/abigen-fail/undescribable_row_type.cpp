// A table's row type must be something the ABI can name.
//
// Row types reach translate_type() as CANONICAL types -- a template argument carries no sugar --
// and its container/map/variant branches match only sugared spellings, so a std::vector row
// translates to the C++ record name `vector`, which is neither an ABI builtin nor anything the
// document declares. Publishing it produced an ABI the chain refuses at set_abi with
// invalid_type_inside_abi: a build that succeeds and a contract that cannot be deployed.
//
// The diagnostic is reported against the contract class, not the specialization -- an implicit
// specialization's location is the template's definition inside sysiolib, which says nothing
// about the author's source.
#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>
#include <vector>

using namespace sysio;

class [[sysio::contract("undescribable_row_type")]] undescribable_row_type : public contract {
public:
   using contract::contract;

   using blob = sysio::singleton<"blobsing"_n, std::vector<uint64_t>>;

   [[sysio::action]]
   void test() {
      blob b(get_self(), get_self().value);
      b.set({1, 2, 3}, get_self());
   }
};
