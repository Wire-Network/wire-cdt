// Every way a singleton can be declared has to reach the ABI.
//
// sysio::singleton is an ALIAS TEMPLATE over kv_singleton (singleton.hpp), and an alias template
// has no specialization of its own -- the AST holds only the kv_singleton one. abigen's visitor
// tested for the name "singleton" and never for "kv_singleton", so no singleton has ever
// produced an ABI table entry, whichever spelling or declaration form the contract used. It went
// unnoticed because sysio::multi_index is the same alias shape over kv_multi_index, which WAS
// tested for: a contract mixing the two saw its multi_index tables described and its singletons
// silently omitted.
//
// Covered here, each of which must produce a table:
//   typedef sysio::singleton<...>        using X = sysio::singleton<...>
//   typedef sysio::kv_singleton<...>     using X = sysio::kv_singleton<...>
//   a singleton held as a data member, named by no alias at all
//   a `_i`-named singleton, published under the name its row annotation gives it
//   a `_i`-named singleton over a SCALAR row, which has nowhere to put that annotation
//   the same, held by a member written through an alias declared outside the contract class
//   an UNANNOTATED row struct, reached through defined_in_contract(), by typedef and by using
//   a row type that is not a class: a builtin (`name`), a scalar, a string, a checksum
//
// Each form gets its own row struct, so every entry stands for exactly one of them and a
// regression names the form it broke. The key layout is the kv_multi_index one -- kv_singleton
// holds a kv_multi_index privately and pins its primary key -- so every entry must carry
// key_names ["scope","primary_key"].
//
// The two admission paths are deliberately kept apart. A row carrying [[sysio::table]] is
// admitted by the annotation, whatever names the specialization; an unannotated row is admitted
// only by defined_in_contract(). That used to match a TypedefNameDecl and nothing else, so a
// DATA MEMBER admitted nothing on its own: `member_inst` rode on its row's annotation, and a
// member over a scalar or over an unannotated row -- `scalar_inst` and `plain_inst` below --
// produced no table at all while the contract built clean. Both spellings of the alias are
// covered on both paths, and the member path now carries all three row kinds.
#include <sysio/sysio.hpp>
#include <sysio/crypto.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_singleton.hpp>
#include <sysio/singleton.hpp>
#include <string>

using namespace sysio;

// Declared OUTSIDE the contract class, so a member of this type writes `outer_alias` and not
// the arguments: its TypeSourceInfo is a TypedefTypeLoc with none to read. The in-class alias
// beside it does write them, which is why the `_i` recovery tries every member naming the
// specialization rather than stopping at the first.
using outer_alias = sysio::singleton<"singleton_outer_alias_name"_i, uint64_t>;

class [[sysio::contract("singleton_decl_forms")]] singleton_decl_forms : public contract {
public:
   singleton_decl_forms( name receiver, name code, datastream<const char*> ds )
      : contract(receiver, code, ds)
      , member_inst(receiver, receiver.value)
      , scalar_inst(receiver, receiver.value)
      , plain_inst(receiver, receiver.value)
      , outer_inst(receiver, receiver.value) {}

   struct [[sysio::table]] tdef_row {
      uint64_t v;
      SYSLIB_SERIALIZE(tdef_row, (v))
   };

   struct [[sysio::table]] usng_row {
      uint64_t v;
      SYSLIB_SERIALIZE(usng_row, (v))
   };

   struct [[sysio::table]] kvtdef_row {
      uint64_t v;
      SYSLIB_SERIALIZE(kvtdef_row, (v))
   };

   struct [[sysio::table]] kvusng_row {
      uint64_t v;
      SYSLIB_SERIALIZE(kvusng_row, (v))
   };

   struct [[sysio::table]] member_row {
      uint64_t v;
      SYSLIB_SERIALIZE(member_row, (v))
   };

   // A `_i` raw is a DJB2 hash rather than a name encoding, so the readable name exists only in
   // the annotation -- name_to_string() of that raw is garbage.
   struct [[sysio::table("singleton_hashed_name")]] hashed_row {
      uint64_t v;
      SYSLIB_SERIALIZE(hashed_row, (v))
   };

   // No [[sysio::table]] at all -- the ordinary way a singleton is written, and how
   // tests/unit/test_contracts/kv_singleton_tests.cpp writes its own. These reach the ABI only
   // through defined_in_contract(), which reads the aliases below and not the row struct, so the
   // ABI must still DEFINE them: a table naming a type the document does not declare is refused
   // by the chain with invalid_type_inside_abi, and set_abi fails.
   struct plain_tdef_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_tdef_row, (v))
   };

   struct plain_usng_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_usng_row, (v))
   };

   // Held by a data member below, and carrying no annotation: the combination that produced
   // nothing at all.
   struct plain_memb_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_memb_row, (v))
   };

   typedef sysio::singleton<"tdefsing"_n, tdef_row>          tdef_singleton;
   using   usng_singleton    = sysio::singleton<"usngsing"_n, usng_row>;
   typedef sysio::kv_singleton<"kvtdefsing"_n, kvtdef_row>   kv_tdef_singleton;
   using   kv_usng_singleton = sysio::kv_singleton<"kvusngsing"_n, kvusng_row>;
   using   hashed_singleton  = sysio::singleton<"singleton_hashed_name"_i, hashed_row>;
   // `_i` over a row that is NOT a class. The raw is a DJB2 hash and a scalar cannot carry
   // [[sysio::table("...")]], so there is no annotation to recover the name from: abigen reads
   // the spelling off the alias's own TypeSourceInfo instead. Published as
   // `singleton_builtin_long_name` at table_id 296; decoding the raw gave `z4pypstb1k13h`, a
   // live table no client could address by name. Deliberately longer than 13 characters, which
   // is the case an `_n` literal cannot express at all.
   using   long_i_singleton  = sysio::singleton<"singleton_builtin_long_name"_i, uint64_t>;
   typedef sysio::singleton<"plaintdef"_n, plain_tdef_row>   plain_tdef_singleton;
   using   plain_usng_singleton = sysio::singleton<"plainusng"_n, plain_usng_row>;

   // Row types that are not classes. Asking one for its CXXRecordDecl gives null, which used to
   // crash the compiler outright; and the ABI type name is not the C++ record name -- a
   // std::string row whose type reads `basic_string`, or a checksum256 row reading
   // `fixed_bytes`, names something the document never declares and set_abi refuses. None of
   // these declares a struct: each is already an ABI builtin.
   using   name_singleton   = sysio::singleton<"namesing"_n, sysio::name>;
   using   uint_singleton   = sysio::singleton<"uintsing"_n, uint64_t>;
   using   str_singleton    = sysio::singleton<"strsing"_n, std::string>;
   using   ck_singleton     = sysio::singleton<"cksing"_n, sysio::checksum256>;

   // No alias at all: the specialization is named only by these members' types.
   // member_inst is admitted twice over -- member_row's annotation would do it alone -- so it
   // cannot tell whether the member itself is recognised. The two below can: neither row
   // carries an annotation, and neither is named by an alias, so each reaches the ABI only if a
   // data member is accepted as owning its specialization.
   sysio::singleton<"membersing"_n, member_row>     member_inst;
   sysio::singleton<"scalarmemb"_n, uint64_t>       scalar_inst;
   sysio::singleton<"plainmemb"_n,  plain_memb_row> plain_inst;

   // Deliberately ahead of the alias that spells the arguments out: taking the first member
   // that names the specialization finds this one and publishes the decoded hash
   // `3rwajluzfjyc1` instead.
   outer_alias outer_inst;
   using outer_alias_in_class = sysio::singleton<"singleton_outer_alias_name"_i, uint64_t>;

   [[sysio::action]]
   void test() {
      tdef_singleton    a(get_self(), get_self().value);
      usng_singleton    b(get_self(), get_self().value);
      kv_tdef_singleton c(get_self(), get_self().value);
      kv_usng_singleton d(get_self(), get_self().value);
      hashed_singleton  e(get_self(), get_self().value);
      long_i_singleton  l(get_self(), get_self().value);

      plain_tdef_singleton f(get_self(), get_self().value);
      plain_usng_singleton g(get_self(), get_self().value);

      name_singleton    h(get_self(), get_self().value);
      uint_singleton    i(get_self(), get_self().value);
      str_singleton     j(get_self(), get_self().value);
      ck_singleton      k(get_self(), get_self().value);

      a.set({1}, get_self());
      b.set({2}, get_self());
      c.set({3}, get_self());
      d.set({4}, get_self());
      e.set({5}, get_self());
      f.set({6}, get_self());
      g.set({7}, get_self());
      h.set("alice"_n, get_self());
      i.set(8, get_self());
      j.set("nine", get_self());
      k.set(checksum256(), get_self());
      l.set(11, get_self());
      member_inst.set({10}, get_self());
      scalar_inst.set(12, get_self());
      plain_inst.set({13}, get_self());
      outer_inst.set(14, get_self());
   }
};
