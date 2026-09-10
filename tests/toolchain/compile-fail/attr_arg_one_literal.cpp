// A sysio attribute argument must be one string literal.
//
// Clang does not parse arguments for a C++11-spelled plugin attribute, so abigen reads this from
// source text -- and the compiler joins adjacent string literals while a source read does not.
// Wrapping a long name across two lines is ordinary C++, and a name long enough to need `_i` is
// exactly a name long enough to wrap:
//
//   [[sysio::table("user_preferences_" "history")]]
//   multi_index<"user_preferences_history"_i, row>
//
// published `user_preferences_` at table_id 32944 -- the id of the FULL name, since `_i` hashed
// what the compiler saw. The published name addressed nothing, and the id it sat at could not be
// reached by name. Across two translation units the descriptors used to disagree and the link
// failed; once annotations are resolved link-wide they agree, and the mislabelled table ships.
//
// Refused rather than joined: one literal is what the reader and the compiler are guaranteed to
// agree on, and a name worth having is one that fits on a line.
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/multi_index.hpp>

using namespace sysio;

class [[sysio::contract("attr_arg_one_literal")]] attr_arg_one_literal : public contract {
public:
   using contract::contract;

   struct [[sysio::table("user_preferences_"
                         "history")]] pref_row {
      uint64_t id;
      uint64_t primary_key() const { return id; }
      SYSLIB_SERIALIZE(pref_row, (id))
   };

   [[sysio::action]]
   void test() {
      multi_index<"user_preferences_history"_i, pref_row> t(get_self(), get_self().value);
      t.emplace(get_self(), [&](auto& r) { r.id = 1; });
   }
};
