hash_id_example — Demonstrates the _i literal for long table names.

The _i literal computes a DJB2 hash at compile time, allowing table names
that exceed the 13-character EOSIO name limit:

   kv::table<"user_preferences"_i, K, V>
   kv::global<"feature_flags"_i, T>

When using _i, annotate the value struct with [[sysio::table("name")]]
so CDT generates the correct ABI entry.

Features shown:
- kv::table with _i literal and string keys
- kv::global with _i literal
- set/try_get/get_or_default patterns
- [[sysio::table]] and [[sysio::kv_key]] ABI annotations
