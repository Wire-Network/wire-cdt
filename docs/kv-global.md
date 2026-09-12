# sysio::kv::global

## Include

```cpp
#include <sysio/kv_global.hpp>
```

## Overview

`kv::global<Name, T>` stores a single value per contract, keyed by name alone. No scope — the entry is global to the contract account.

Each `global` instance receives a unique `table_id` (uint16, DJB2 hash), providing per-table namespace isolation.

- **Zero-copy** for `trivially_copyable` types (compile-time `sizeof(T)` buffer)
- Falls back to stack-first serialization for complex types (strings, vectors)
- Multiple globals with different names are independent

## Template Parameter

Both `_n` and `_i` literals work. Reach for `_n` unless the name does not fit it:

```cpp
kv::global<"appconfig"_n, config_type>          cfg(get_self());  // fits a name: use _n
kv::global<"app_configuration"_i, config_type>  cfg(get_self());  // longer, or has _ / A-Z: _i
```

`_n` covers a name of up to 13 characters drawn from `.12345a-z` — with the **13th position
restricted to `.12345a-j`**, because it is encoded in 4 bits rather than 5. Anything else — longer,
an underscore, an uppercase letter, a digit outside `1-5` — needs `_i`, which hashes the string
instead. `"app_config"_n` is a *compile* error for that reason, not a style preference.

**ABI requirement.** Annotate the value struct with `[[sysio::table("...")]]` so the ABI carries a
readable name — with `_i` that is the only place the readable name exists, since the literal is a
hash. The annotation alone is not enough: the struct must also be **reachable from the contract**,
either declared inside the contract class (as below) or carrying `sysio::contract("...")` beside
the table attribute. A namespace-scope struct with only `[[sysio::table]]` compiles and runs, but
emits **no** ABI table entry at all, so `get_table_rows` has nothing to describe it.

## API

| Method | Description |
|--------|-------------|
| `exists()` | Returns true if a value has been stored |
| `get(msg)` | Returns stored value, asserts if missing |
| `get_or_default(def)` | Returns stored value or `def` |
| `get_or_create(payer, def)` | Returns stored value, or stores and returns `def` |
| `set(value, payer)` | Stores or overwrites the value |
| `remove()` | Deletes the stored value (safe to call if not set) |

## Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

class [[sysio::contract("myapp")]] myapp : public contract {
public:
   using contract::contract;

   // Inside the contract class, so abigen emits the table entry -- see the ABI note above.
   struct [[sysio::table("appconfig")]] app_config {
      uint64_t max_transfer;
      uint32_t fee_bps;
      SYSLIB_SERIALIZE(app_config, (max_transfer)(fee_bps))
   };

   kv::global<"appconfig"_n, app_config> cfg{get_self()};

   [[sysio::action]]
   void setconfig(uint64_t max_transfer, uint32_t fee_bps) {
      require_auth(get_self());
      cfg.set({max_transfer, fee_bps}, get_self());
   }

   [[sysio::action]]
   void transfer(name from, name to, uint64_t amount) {
      require_auth(from);
      auto c = cfg.get("config not set");
      check(amount <= c.max_transfer, "exceeds max transfer");
   }
};
```

## Comparison with singleton

| | `kv::global` | `singleton` |
|---|---|---|
| Scope | None | Required |
| Key size | 8 bytes | 16 bytes |
| Namespace | Unique `table_id` | Unique `table_id` |
| Use case | Contract-wide config | Per-account config |
