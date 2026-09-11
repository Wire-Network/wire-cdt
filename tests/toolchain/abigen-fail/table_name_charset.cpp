// A table's annotated name is taken from the source LITERALLY, so it has to be plain.
//
// The argument of a C++11-spelled attribute is not parsed into an Expr, so it is read as source
// text and then encoded into an AnnotateAttr as `sysio_table(name)`, which is split back out on
// [\s,]+. Two things follow, and both used to pass silently while naming a table something the
// author did not write:
//
//   [[sysio::table("config\x31")]]     published `config\x31` at cooked `config1`'s table_id
//   [[sysio::table("con" "cat")]]      published `con`        at the concatenation's table_id
//   [[sysio::table("has space")]]      published `has`, truncated by the encoding
//
// The first two are the compiler cooking a literal the reader does not; the third is the
// encoding. All three are refused rather than repaired: these names leave the toolchain in the
// ABI and are read by wire-sysio, SHiP and Hyperion, and a name worth having is one that can be
// written plainly.
//
// Letters, digits and underscore. `_i` still lifts the 13-character limit -- the name is hashed,
// not encoded -- so a long readable name is exactly what this is for.
#include <sysio/sysio.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("table_name_charset")]] table_name_charset : public contract {
public:
   using contract::contract;

   struct [[sysio::table("has-hyphen")]] row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(row, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"tbl"_n, row> t(get_self(), get_self().value);
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
