#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

// A DIFFERENT type from the one in the main source, despite printing the same qualified name.
namespace {
   struct [[sysio::table("second"), sysio::contract("named_table_internal_linkage")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };
}

[[sysio::action]] void other() {
   sysio::multi_index<"two"_n, row> t(sysio::name{}, 0);
   (void)t;
}
