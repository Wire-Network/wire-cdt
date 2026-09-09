// A table's row type must be something the ABI can name.
//
// The guard exists because the published `type` is the ABI type name, not the C++ record name:
// std::string's record is `basic_string` and sysio::checksum256's is `fixed_bytes`, and neither
// is a type any ABI declares. Publishing one produced a document the chain refuses at set_abi
// with invalid_type_inside_abi -- a build that succeeds and a contract that cannot be deployed.
// Catching it at the declaration is the whole point: the alternative is a failure hours later,
// in a different tool, naming neither the table nor the row.
//
// sysio::fixed_bytes<7> is the honest remainder. The ABI names three widths of it -- 20, 32 and
// 64 bytes, as checksum160/256/512 -- and has no spelling for a seventh, so there is nothing to
// publish and no suffix to strip. A CONTAINER row is a different case and is not an error:
// `uint64[]` resolves, and abigen-pass/singleton_container_row pins that it is emitted rather
// than rejected. This fixture used a std::vector row until that was fixed, which meant it had
// been pinning a limitation of the translation rather than a limitation of the ABI.
//
// The diagnostic is reported against the contract class, not the specialization -- an implicit
// specialization's location is the template's definition inside sysiolib, which says nothing
// about the author's source.
#include <sysio/sysio.hpp>
#include <sysio/fixed_bytes.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

class [[sysio::contract("undescribable_row_type")]] undescribable_row_type : public contract {
public:
   using contract::contract;

   using odd = sysio::singleton<"oddwidth"_n, sysio::fixed_bytes<7>>;

   [[sysio::action]]
   void test() {
      odd b(get_self(), get_self().value);
      b.set(fixed_bytes<7>(), get_self());
   }
};
