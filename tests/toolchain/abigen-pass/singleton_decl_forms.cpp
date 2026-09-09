// Every way a singleton can be declared has to reach the ABI.
//
// sysio::singleton is an ALIAS TEMPLATE over kv_singleton (singleton.hpp), and an alias template
// has no specialization of its own -- the AST holds only the kv_singleton one. abigen's visitor
// tested for the name "singleton" and never for "kv_singleton", so no singleton has ever
// produced an ABI table entry, whichever spelling or declaration form the contract used.
//
// That is a Wire regression rather than a missing feature: upstream `eosio::singleton` is a real
// class template, which is why it has always worked there. It went unnoticed here because
// sysio::multi_index is the same alias shape over kv_multi_index, which WAS tested for -- a
// contract mixing the two saw its multi_index tables described and its singletons omitted.
//
// Covered here, each of which must produce a table:
//   typedef sysio::singleton<...>        using X = sysio::singleton<...>
//   typedef sysio::kv_singleton<...>     using X = sysio::kv_singleton<...>
//   a singleton held as a data member, named by no alias at all
//   an UNANNOTATED row struct, reached through defined_in_contract(), by alias and by member
//   a `_i`-named singleton, published under the name its row annotation gives it
//
// Every row is a struct, which is the rule -- abigen-fail/row_must_be_a_struct is the other
// side of it. The key layout is the kv_multi_index one, since kv_singleton holds one privately
// and pins its primary key, so every entry carries key_names ["scope","primary_key"].
//
// The two admission paths are kept apart. A row carrying [[sysio::table]] is admitted by the
// annotation, whatever names the specialization; an unannotated row is admitted only by
// defined_in_contract(), which used to match a TypedefNameDecl and nothing else -- so a DATA
// MEMBER admitted nothing on its own and `plain_inst` produced no table while the contract
// built clean. `member_inst` cannot show that, because its row's annotation admits it either
// way.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_singleton.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

class [[sysio::contract("singleton_decl_forms")]] singleton_decl_forms : public contract {
public:
   singleton_decl_forms( name receiver, name code, datastream<const char*> ds )
      : contract(receiver, code, ds)
      , member_inst(receiver, receiver.value)
      , plain_inst(receiver, receiver.value) {}

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
   // the annotation -- name_to_string() of that raw is garbage, and nothing recovers a string
   // from a hash. The annotation is the channel, which is why a `_i` table needs an annotated
   // row struct; the row rule guarantees there is always one to annotate.
   struct [[sysio::table("singleton_hashed_name")]] hashed_row {
      uint64_t v;
      SYSLIB_SERIALIZE(hashed_row, (v))
   };

   // No [[sysio::table]] at all -- the ordinary way a singleton is written, and how
   // tests/unit/test_contracts/kv_singleton_tests.cpp writes its own. These reach the ABI only
   // through defined_in_contract(), which reads the declarations below and not the row struct,
   // so the ABI must still DEFINE them: a table naming a type the document does not declare is
   // refused by the chain with invalid_type_inside_abi, and set_abi fails.
   struct plain_tdef_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_tdef_row, (v))
   };

   struct plain_usng_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_usng_row, (v))
   };

   struct plain_memb_row {
      uint64_t v;
      SYSLIB_SERIALIZE(plain_memb_row, (v))
   };

   typedef sysio::singleton<"tdefsing"_n, tdef_row>          tdef_singleton;
   using   usng_singleton    = sysio::singleton<"usngsing"_n, usng_row>;
   typedef sysio::kv_singleton<"kvtdefsing"_n, kvtdef_row>   kv_tdef_singleton;
   using   kv_usng_singleton = sysio::kv_singleton<"kvusngsing"_n, kvusng_row>;
   using   hashed_singleton  = sysio::singleton<"singleton_hashed_name"_i, hashed_row>;
   typedef sysio::singleton<"plaintdef"_n, plain_tdef_row>   plain_tdef_singleton;
   using   plain_usng_singleton = sysio::singleton<"plainusng"_n, plain_usng_row>;

   // No alias at all: the specialization is named only by these members' types. plain_inst is
   // the one that discriminates -- its row carries no annotation, so it reaches the ABI only if
   // a data member is accepted as owning its specialization.
   sysio::singleton<"membersing"_n, member_row>     member_inst;
   sysio::singleton<"plainmemb"_n,  plain_memb_row> plain_inst;

   [[sysio::action]]
   void test() {
      tdef_singleton    a(get_self(), get_self().value);
      usng_singleton    b(get_self(), get_self().value);
      kv_tdef_singleton c(get_self(), get_self().value);
      kv_usng_singleton d(get_self(), get_self().value);
      hashed_singleton  e(get_self(), get_self().value);

      plain_tdef_singleton f(get_self(), get_self().value);
      plain_usng_singleton g(get_self(), get_self().value);

      a.set({1}, get_self());
      b.set({2}, get_self());
      c.set({3}, get_self());
      d.set({4}, get_self());
      e.set({5}, get_self());
      f.set({6}, get_self());
      g.set({7}, get_self());
      member_inst.set({10}, get_self());
      plain_inst.set({13}, get_self());
   }
};
