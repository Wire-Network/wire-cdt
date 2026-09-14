# sysio::kv::scoped_table

> Scoped KV table — same API as `kv::table` but with mandatory scope. Produces byte-identical primary keys to `multi_index`: `[scope:8B BE][K encoded]`.

## Include

```cpp
#include <sysio/kv_scoped_table.hpp>
```

## When to use

Use `scoped_table` when your data is naturally partitioned by an account or category (e.g., token balances per account, proposals per proposer). Use `kv::table` when scope adds no value (singleton-like tables, global registries).

`scoped_table` is a **migration target** for `multi_index`, not a drop-in: it keeps the same scope semantics and drops the object-cache overhead, but the port is a rewrite. The template goes from `<Name, T, ...>` to `<Name, K, V, ...>`, the row splits into a separate key and value type, primary-key access changes with it, and the payer moves to the first parameter of every mutator (see the worked conversion below). What it buys:
- No `std::map` cache, no `shared_ptr`, no double deserialization
- Zero-copy fast path for trivially copyable values
- `if constexpr` scope plumbing — zero overhead vs `kv::table` for the scope branches

## Template Parameters

```cpp
template<name::raw TableName, typename K, typename V, typename... Indices>
```

Same as `kv::table`. See [kv-table.md](kv-table.md) for details.

## Constructor

```cpp
// scope is required
kv::scoped_table<"accounts"_n, K, V> tbl(get_self(), owner.value);

// cross-contract read
kv::scoped_table<"accounts"_n, K, V> tbl("sysio.token"_n, owner.value);
```

## Key Layout

Primary keys are `[scope:8B BE][K encoded]` — byte-identical to `multi_index`'s `[scope:8B BE][pk:8B BE]` when `K` is a single `uint64_t`.

Secondary keys are `[scope:8B BE][secondary_value encoded]`. Secondary index pri_key stores only `[K encoded]` (no scope) — saves 8 bytes per secondary row.

## API

All methods from `kv::table` are available. Additionally:

| Method | Returns | Description |
|--------|---------|-------------|
| `get_scope()` | `uint64_t` | Returns the scope value |
| `scope_lower_bound(code, scope)` | `scope_iterator` | Static. First scope >= value |
| `scope_end()` | `scope_iterator` | Static. End sentinel |

## Scope Iteration

Enumerate all scopes that have data:

```cpp
using accounts = kv::scoped_table<"accounts"_n, pk_key, account>;

for (auto it = accounts::scope_lower_bound(get_self(), 0);
     it != accounts::scope_end(); ++it) {
   uint64_t scope = *it;
   // process scope...
}
```

`scope_iterator` is input-only (forward, no `--`). Uses no new intrinsics — walks primary keys and skips to the next scope prefix on each `++`.

## Migrating from multi_index

```cpp
// Before (multi_index):
multi_index<"accounts"_n, account> accts(get_self(), owner.value);
accts.emplace(get_self(), [&](auto& r) { r.id = 1; r.balance = 100; });
auto it = accts.find(1);

// After (scoped_table):
struct pk_key { uint64_t id; SYSLIB_SERIALIZE(pk_key, (id)) };
struct acct_val { uint64_t balance; SYSLIB_SERIALIZE(acct_val, (balance)) };
kv::scoped_table<"accounts"_n, pk_key, acct_val> accts(get_self(), owner.value);
accts.emplace(get_self(), pk_key{1}, acct_val{100});
auto it = accts.find(pk_key{1});
```

Key compatibility: if `K` is `{uint64_t id}`, the on-chain key bytes are identical. Existing `get_table_rows` queries with scope continue to work.

### API differences from multi_index

**Payer is always the first parameter** across all mutations (`emplace`, `upsert`, `modify`, `erase`). multi_index put it second in `modify(it, payer, lambda)`.

**`modify` has two forms** with different trade-offs:

```cpp
// multi_index (lambda mutates cached object in-place):
tbl.modify(it, payer, [&](auto& r) { r.balance += 100; });

// scoped_table option 1 — iterator + new value (no lambda, no cache):
tbl.modify(payer, it, account{new_balance, owner});

// scoped_table option 2 — key + lambda (reads old value, applies lambda, writes back):
tbl.modify(payer, pk_key{id}, [&](account& a) { a.balance += 100; });
```

Why: kv::table has no object cache. multi_index's lambda mutated a cached object in-place, then the cache serialized it back. Without a cache, the iterator form just takes a new value directly. The lambda form uses a key instead of an iterator — it reads the current value from the KV store, applies the lambda, and writes back.

## Complete Example

```cpp
#include <sysio/sysio.hpp>
#include <sysio/kv_scoped_table.hpp>

using namespace sysio;

struct sym_key {
   uint64_t sym_code;
   SYSLIB_SERIALIZE(sym_key, (sym_code))
};

struct [[sysio::table("accounts")]] account {
   uint64_t balance;
   SYSLIB_SERIALIZE(account, (balance))
};

using accounts = kv::scoped_table<"accounts"_n, sym_key, account>;

class [[sysio::contract]] token : public contract {
public:
   using contract::contract;

   [[sysio::action]]
   void transfer(name from, name to, uint64_t sym_code, uint64_t amount) {
      accounts from_accts(get_self(), from.value);
      accounts to_accts(get_self(), to.value);

      from_accts.modify(get_self(), sym_key{sym_code}, [&](account& a) {
         check(a.balance >= amount, "overdrawn");
         a.balance -= amount;
      });

      auto to_bal = to_accts.try_get(sym_key{sym_code});
      if (to_bal) {
         to_accts.set(get_self(), sym_key{sym_code}, account{to_bal->balance + amount});
      } else {
         to_accts.emplace(get_self(), sym_key{sym_code}, account{amount});
      }
   }

   [[sysio::action]]
   void holders(uint64_t sym_code) {
      // Enumerate all accounts holding this token
      for (auto sit = accounts::scope_lower_bound(get_self(), 0);
           sit != accounts::scope_end(); ++sit) {
         accounts accts(get_self(), *sit);
         auto bal = accts.try_get(sym_key{sym_code});
         if (bal && bal->balance > 0)
            print(name(*sit), ": ", bal->balance, "\n");
      }
   }
};
```
