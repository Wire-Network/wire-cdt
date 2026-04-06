# sysio::kv::global

## Include

```cpp
#include <sysio/kv_global.hpp>
```

## Overview

`sysio::kv::global<Name, T>` stores a single value per contract, keyed by name alone. No scope parameter -- the entry is global to the contract account. Built on format=0 raw KV storage.

Key properties:

- **No scope** -- one value per `Name` per contract, globally accessible
- **Zero-copy** for `trivially_copyable` types: compile-time `sizeof(T)` buffer, no dynamic allocation
- Falls back to stack-first serialization for complex types (strings, vectors)
- Simple API: `set`, `get`, `exists`, `remove`, `get_or_default`, `get_or_create`
- Multiple globals with different names are independent (`global<"cfg1"_n, T>` vs `global<"cfg2"_n, T>`)

## When to use kv::global

Use `kv::global` when you need:
- A single configuration or state value per contract (rate limits, feature flags, counters)
- No scope partitioning -- the value is the same regardless of who calls the contract
- The simplest possible storage API

Use `singleton` instead if you need scope-based partitioning (different config per scope).
Use `kv::table` if you need multiple rows or iteration.

## Constructor

```cpp
kv::global<"config"_n, config_type> cfg(get_self());
```

- `code` -- the contract account that owns the data (default: current receiver)

## API reference

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

struct app_config {
   uint64_t max_transfer;
   uint32_t fee_bps;
   SYSLIB_SERIALIZE(app_config, (max_transfer)(fee_bps))
};

class [[sysio::contract]] myapp : public contract {
public:
   using contract::contract;

   kv::global<"config"_n, app_config> cfg{get_self()};

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
      // ... apply fee_bps, do transfer ...
   }
};
```

## Comparison with singleton

| | `kv::global` | `singleton` |
|---|---|---|
| Scope | None (global) | Required |
| Backing | format=0 raw KV | format=1 `kv::table` |
| Key size | 8 bytes | 24 bytes |
| Multiple values per contract | One per Name | One per Name per scope |
| Use case | Contract-wide config | Per-account or per-scope config |
