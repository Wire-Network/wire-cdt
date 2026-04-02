# sysio::multi\_index

## Include

```cpp
#include <sysio/multi_index.hpp>   // explicit
// or automatically via:
#include <sysio/sysio.hpp>
```

## Overview

The API surface is identical to the EOSIO `multi_index`. Under the hood, primary rows are stored as 24-byte KV keys and secondary indices use the `kv_idx_*` intrinsics, but from the contract author's perspective the interface is the same.

Key properties:

- Up to **16 secondary indices** via `indexed_by` / `const_mem_fun`
- Full bidirectional iterator support: `begin`/`end`, `rbegin`/`rend`, `cbegin`/`cend`
- `find`, `require_find`, `get`, `lower_bound`, `upper_bound`, `iterator_to`
- `emplace`, `modify`, `erase` (erase returns next iterator)
- `available_primary_key()` for auto-increment
- Object caching: repeated access to the same primary key returns the cached pointer
- `payer` parameter is honored -- RAM is charged to the specified payer (cross-account billing requires `sysio.payer` permission)
- Row types must use `SYSLIB_SERIALIZE`
- Post-increment (`it++`) and post-decrement (`it--`) are deleted -- use `++it` / `--it`

## Singleton support

```cpp
#include <sysio/singleton.hpp>
```

`sysio::singleton<Name, T>` is now an alias for `sysio::kv_singleton`, backed by the KV database. The API is unchanged:

| Method | Description |
|--------|-------------|
| `exists()` | Returns true if a value has been stored |
| `get()` | Returns stored value, asserts if missing |
| `get_or_default(def)` | Returns stored value or `def` |
| `get_or_create(payer, def)` | Returns stored value, or stores and returns `def` |
| `set(value, payer)` | Stores or updates the value |
| `remove()` | Deletes the stored value |

## Secondary index views

Access a secondary index with `get_index<"indexname"_n>()`. The returned view supports:

| Method | Description |
|--------|-------------|
| `find(sec_key)` | Exact match on secondary key |
| `lower_bound(sec_key)` | First entry >= sec\_key |
| `require_find(sec_key, msg)` | find() + assert |
| `begin()` / `end()` | Full range in secondary key order |
| `rbegin()` / `rend()` | Reverse iteration |
| `modify(itr, payer, updater)` | Modify the primary row via secondary iterator |
| `erase(itr)` | Erase the primary row, returns next secondary iterator |

Supported secondary key types: `uint64_t`, `uint128_t`, `double`, `long double`, and any serializable type.

## Example: token-like contract with secondary index

```cpp
#include <sysio/sysio.hpp>

using namespace sysio;

class [[sysio::contract]] mytoken : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] account {
      name     owner;
      uint64_t balance;

      uint64_t primary_key() const { return owner.value; }
      uint64_t by_balance()  const { return balance; }

      SYSLIB_SERIALIZE(account, (owner)(balance))
   };

   using accounts_table = multi_index<"accounts"_n, account,
      indexed_by<"bybalance"_n, const_mem_fun<account, uint64_t, &account::by_balance>>
   >;

   [[sysio::action]]
   void transfer(name from, name to, uint64_t amount) {
      require_auth(from);

      accounts_table accts(get_self(), get_self().value);

      // Debit sender
      auto from_itr = accts.require_find(from.value, "sender not found");
      check(from_itr->balance >= amount, "insufficient balance");
      accts.modify(from_itr, same_payer, [&](auto& a) {
         a.balance -= amount;
      });

      // Credit receiver
      auto to_itr = accts.find(to.value);
      if (to_itr == accts.end()) {
         accts.emplace(get_self(), [&](auto& a) {
            a.owner = to;
            a.balance = amount;
         });
      } else {
         accts.modify(to_itr, same_payer, [&](auto& a) {
            a.balance += amount;
         });
      }
   }

   [[sysio::action]]
   void topbalances(uint32_t limit) {
      accounts_table accts(get_self(), get_self().value);
      auto idx = accts.get_index<"bybalance"_n>();

      // Iterate in reverse (highest balance first)
      uint32_t count = 0;
      for (auto it = idx.rbegin(); it != idx.rend() && count < limit; ++it, ++count) {
         print(it->owner, ": ", it->balance, "\n");
      }
   }
};
```
