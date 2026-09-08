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
// only by defined_in_contract(), which matches a TypedefNameDecl in the contract class and
// nothing else -- notably not a FieldDecl, so `member_inst` below rides on its row's annotation
// and not on the member. Both spellings of the alias are covered on both paths.
#include <sysio/sysio.hpp>
#include <sysio/crypto.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_singleton.hpp>
#include <sysio/singleton.hpp>
#include <string>

using namespace sysio;

class [[sysio::contract("singleton_decl_forms")]] singleton_decl_forms : public contract {
public:
   singleton_decl_forms( name receiver, name code, datastream<const char*> ds )
      : contract(receiver, code, ds)
      , member_inst(receiver, receiver.value) {}

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

   typedef sysio::singleton<"tdefsing"_n, tdef_row>          tdef_singleton;
   using   usng_singleton    = sysio::singleton<"usngsing"_n, usng_row>;
   typedef sysio::kv_singleton<"kvtdefsing"_n, kvtdef_row>   kv_tdef_singleton;
   using   kv_usng_singleton = sysio::kv_singleton<"kvusngsing"_n, kvusng_row>;
   using   hashed_singleton  = sysio::singleton<"singleton_hashed_name"_i, hashed_row>;
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

   // No alias at all: the specialization is named only by this member's type. member_row's
   // annotation is what admits it -- defined_in_contract() does not look at fields.
   sysio::singleton<"membersing"_n, member_row> member_inst;

   [[sysio::action]]
   void test() {
      tdef_singleton    a(get_self(), get_self().value);
      usng_singleton    b(get_self(), get_self().value);
      kv_tdef_singleton c(get_self(), get_self().value);
      kv_usng_singleton d(get_self(), get_self().value);
      hashed_singleton  e(get_self(), get_self().value);

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
      member_inst.set({10}, get_self());
   }
};
