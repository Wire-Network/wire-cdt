#pragma once
#include <sysio/sysio.hpp>
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

// --- Custom key with string field ---
struct preference_key {
   name        user;
   std::string category;
   SYSLIB_SERIALIZE(preference_key, (user)(category))
};

// Value struct — long table name requires [[sysio::table]] for ABI.
// [[sysio::contract]] as well: the annotation is only recorded for a struct abigen can tie to
// this contract, and a struct at namespace scope has nothing else tying it. Without it the
// annotation never reaches the descriptor, so nothing renames the table and the ABI publishes
// name_to_string() of the _i hash -- `31v1bqlusjbf5`, which no client can address.
struct [[sysio::table("user_preferences"), sysio::contract("hash_id_example"),
         sysio::kv_key("preference_key")]] preference {
   std::string value;
   uint64_t    updated_at;
   SYSLIB_SERIALIZE(preference, (value)(updated_at))
};

// Table name exceeds 13-char EOSIO name limit — use _i literal
using prefs_table = kv::table<"user_preferences"_i, preference_key, preference>;

// Global with long name
struct [[sysio::table("feature_flags"), sysio::contract("hash_id_example")]] feature_flags {
   bool enable_notifications;
   bool enable_dark_mode;
   SYSLIB_SERIALIZE(feature_flags, (enable_notifications)(enable_dark_mode))
};

using flags_global = kv::global<"feature_flags"_i, feature_flags>;

class [[sysio::contract("hash_id_example")]] hash_id_example : public contract {
public:
   using contract::contract;

   prefs_table  prefs{get_self()};
   flags_global flags{get_self()};

   [[sysio::action]] void setpref(name user, std::string category, std::string value);
   [[sysio::action]] void getpref(name user, std::string category);
   [[sysio::action]] void setflags(bool notif, bool dark);
   [[sysio::action]] void getflags();
};
