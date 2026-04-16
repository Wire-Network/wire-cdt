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

Both `_n` and `_i` literals work:

```cpp
kv::global<"config"_n, config_type>             cfg(get_self());  // short name
kv::global<"app_configuration"_i, config_type>  cfg(get_self());  // long name
```

**ABI requirement:** When using `_i`, annotate the value struct with `[[sysio::table("app_configuration")]]` so CDT generates the ABI entry.

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
#include <sysio/hash_id.hpp>

using namespace sysio;

struct [[sysio::table("app_config")]] app_config {
   uint64_t max_transfer;
   uint32_t fee_bps;
   SYSLIB_SERIALIZE(app_config, (max_transfer)(fee_bps))
};

class [[sysio::contract]] myapp : public contract {
public:
   using contract::contract;

   kv::global<"app_config"_i, app_config> cfg{get_self()};

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
