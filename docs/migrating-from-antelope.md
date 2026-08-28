# Migrating a Contract from an Antelope Chain to Wire

For developers arriving with a working contract from EOS, Telos, WAX, Jungle, or any other
Antelope-family chain. It covers what to install, what to change in the source, and — the part that
surprises people — why the contract, not the user, has to be provisioned before anyone can call it.

Wire is Antelope-derived. The account model, permissions, actions, inline actions, notifications,
ABIs, `check()`, `name`, `asset`, `symbol`, `time_point`, the cryptographic intrinsics and the
serialization format are all the ones you already know. Three things are genuinely different:

| What | Size of the job |
|---|---|
| **Every `eosio` identifier is spelled `sysio`** | Mechanical. One `sed` pass over the source. |
| **The legacy `db_*_i64` table store no longer exists** | Usually zero source changes — `sysio::multi_index` is a drop-in shim over the new KV store — but RAM sizing and client-side `get_table_rows` calls both change. |
| **The contract is billed for CPU, NET, and RAM — by default, not the signer** | One decision, and often one line: where your contract bills RAM. Everything else follows from it. A signer *can* still volunteer to pay, by opting in with the reserved `sysio.payer` permission — but that is the exception, not how ordinary traffic works. |

Plus a short list of Antelope features Wire does not carry: deferred transactions and two
permission intrinsics. See [Features with no Wire equivalent](#features-with-no-wire-equivalent).

---

## Contents

- [Getting started on Wire](#getting-started-on-wire)
- [Step 1 — rename `eosio` to `sysio`](#step-1--rename-eosio-to-sysio)
- [Step 2 — storage](#step-2--storage)
- [Step 3 — resources: the contract pays](#step-3--resources-the-contract-pays)
- [Step 4 — host functions added and removed](#step-4--host-functions-added-and-removed)
- [Features with no Wire equivalent](#features-with-no-wire-equivalent)
- [Worth adopting once the port works](#worth-adopting-once-the-port-works)
- [Porting checklist](#porting-checklist)
- [Further reading](#further-reading)

---

## Getting started on Wire

### Install the toolchain

Wire CDT replaces `eosio.cdt` / `cdt`. It is a self-contained toolchain — its own LLVM 18, its own
libc and libc++, the WASM contract library, and the CMake package. Only the `cdt-*` and `sysio-*`
entry points land on `PATH`; the bundled `clang`, `lld` and `llvm-*` binaries stay private under the
toolchain's own home, so nothing shadows your distro's compiler.

> **If AntelopeIO CDT 3.0+ is already installed**, both packages publish a `cdt-cpp` (and `cdt-cc`,
> `cdt-ld`, `cdt-init`) on `PATH`. Install Wire CDT from the portable tarball to `/opt/wire-cdt` and
> put `/opt/wire-cdt/bin` ahead on `PATH` for Wire work, or check `which cdt-cpp` before every build.

```bash
sudo apt install ./wire-cdt_<version>_amd64.deb    # deb / rpm → /usr/lib/cdt
```

Or extract the portable tarball to `/opt` (`/opt/wire-cdt`), which coexists with a deb install.
Building from source is documented in [BUILD.md](../BUILD.md).

Verify:

```bash
cdt-cpp --version
```

Tool-name mapping, if you have scripts to update:

| Antelope | Wire |
|---|---|
| `eosio-cpp` / `cdt-cpp` | `cdt-cpp` |
| `eosio-cc` / `cdt-cc` | `cdt-cc` |
| `eosio-ld` / `cdt-ld` | `cdt-ld` |
| `eosio-abidiff` | `cdt-abidiff` |
| `eosio-init` | `cdt-init` |
| `eosio-wast2wasm` / `-wasm2wast` | `cdt-wast2wasm` / `cdt-wasm2wast` |
| `eosio-pp` | `sysio-pp` (also aliased `cdt-pp`) |
| `find_package(cdt)` / `${CDT_ROOT}` | unchanged — the CMake package is still named `cdt` |

`cleos` becomes `clio`, `nodeos` becomes `nodeop`, and `keosd` becomes `kiod`.

### Create a project

```bash
cdt-init -project=mycontract          # add -path=<dir> to place it elsewhere
cd mycontract/build && cmake .. && make
```

`cdt-init` scaffolds `src/`, `include/`, `ricardian/`, `build/` and a CMake project that already
calls `find_package(cdt)` and `add_contract`. `-bare` emits just the `.hpp`/`.cpp` skeleton.

The generated skeleton is the familiar shape, with `sysio` in place of `eosio`:

```cpp
#include <sysio/sysio.hpp>
using namespace sysio;

CONTRACT mycontract : public contract {
   public:
      using contract::contract;

      ACTION hi( name nm );

      using hi_action = action_wrapper<"hi"_n, &mycontract::hi>;
};
```

### Build an existing contract

Through CMake — the macro signature is unchanged from `eosio.cdt`:

```cmake
find_package(cdt)

add_contract( mycontract mycontract mycontract.cpp )
target_include_directories( mycontract PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( mycontract ${CMAKE_SOURCE_DIR}/../ricardian )
```

Or directly:

```bash
cdt-cpp -abigen ../src/mycontract.cpp -o mycontract.wasm -I ../include/
```

Both produce `mycontract.wasm` and `mycontract.abi`. The generated `.actions.cpp`, `.dispatch.cpp`
and `.desc` sidecars are build products — do not commit them.

### Test without a chain

Wire CDT compiles contracts to native host code with mockable intrinsics, so you can unit-test and
run them under `gdb`/`lldb` before you have an account anywhere. See
[Native Tester and Compilation](native-tester-compilation.md).

### Get an account, and provision the contract

On a shared Wire network, accounts are not created by ordinary accounts — `sysio.roa::newuser`
is the supported flow and is restricted to registered tier-1 node owners. On a local dev chain you
control the privileged `sysio` account, so the familiar form works:

```bash
clio create account sysio mycontract <OwnerKey> <ActiveKey>
```

**A new account holds zero CPU and zero NET.** For a user account that does not matter — see
[Step 3](#step-3--resources-the-contract-pays). For the account that will *hold your contract* it is
fatal, because on Wire the contract is the payer. Before anyone can call it, a node owner must
issue it a policy:

```bash
clio push action sysio.roa addpolicy \
  '{"owner":"mycontract","issuer":"<nodeowner>","netWeight":"0.1000 SYS", \
    "cpuWeight":"0.1000 SYS","ramWeight":"1.0000 SYS","timeBlock":0,"networkGen":0}' \
  -p <nodeowner>@active
```

Without it, every call fails with `account mycontract net usage is too high: 132 > 0` — which looks
like a broken contract and is not one.

### Deploy

```bash
clio set contract mycontract ./build/mycontract -p mycontract@active
```

`setcode` bills code RAM at **ten times** the WASM size (`setcode_ram_bytes_multiplier`), so size
the RAM side of the policy from the ×10 figure. `setabi` is billed 1×.

---

## Step 1 — rename `eosio` to `sysio`

There are **no `eosio::` compatibility aliases**. Every identifier changes, and the compiler will
find each one for you.

| Antelope | Wire |
|---|---|
| `#include <eosio/eosio.hpp>` | `#include <sysio/sysio.hpp>` |
| `namespace eosio` | `namespace sysio` |
| `eosio::name`, `eosio::asset`, `eosio::check`, … | `sysio::name`, `sysio::asset`, `sysio::check`, … |
| `[[eosio::contract]]` / `action` / `table` / `on_notify` / `ignore` / `ricardian` / `read_only` | `[[sysio::…]]`, same spellings after the namespace |
| `[[eosio::wasm_entry]]` / `wasm_import` / `wasm_action` / `wasm_notify` / `wasm_abi` | `[[sysio::…]]` |
| `EOSIO_DISPATCH` | `SYSIO_DISPATCH` |
| `EOSLIB_SERIALIZE` | `SYSLIB_SERIALIZE` |
| `EOSLIB_SERIALIZE_DERIVED` | `SYSLIB_SERIALIZE_DERIVED` |
| `eosio_assert` (C API) | `sysio_assert` |
| `eosio_exit` | `sysio_exit` |
| `CONTRACT` / `ACTION` / `TABLE` | unchanged (they expand to the `sysio` attributes) |

A first pass that gets nearly all of it:

```bash
grep -rl 'eosio\|EOSIO\|EOSLIB' src include \
  | xargs sed -i -e 's/\beosio\b/sysio/g' -e 's/\bEOSIO_/SYSIO_/g' -e 's/\bEOSLIB_/SYSLIB_/g'
```

Review the diff before trusting it: a `sed` this broad will also rewrite `eosio.token` to
`sysio.token` inside string literals and `"eosio"_n` to `"sysio"_n`. On Wire those renames are
usually correct — the system account is `sysio` and the token contract is `sysio.token` — but the
account names in *your* contract's own tables and constants are yours to decide.

### The ABI

The generated ABI's version string changes from `eosio::abi/1.x` to **`sysio::abi/1.2`**. Table
entries carry two Wire additions — `table_id`, and `key_names`/`key_types` describing the key
layout:

```json
{
   "name": "accounts",
   "type": "account",
   "index_type": "i64",
   "key_names": ["scope", "primary_key"],
   "key_types": ["name", "uint64"],
   "table_id": 25660
}
```

`cdt-abidiff <old.abi> <new.abi>` will show you exactly what moved.

---

## Step 2 — storage

### The legacy database is gone

The `db_store_i64` / `db_find_i64` / `db_idx64_*` / `db_idx128_*` / `db_idx256_*` /
`db_idx_double_*` / `db_idx_long_double_*` intrinsics do not exist on Wire — not in CDT, and not in
the chain. `<sysio/db.h>` is a stub that forwards to `<sysio/kv.h>`. Contract state lives in a
key-value store addressed by a compile-time `table_id`.

Code that called those intrinsics directly must be rewritten. Code that used `multi_index` — which
is nearly all of it — usually does not change at all.

### `multi_index` still works

`sysio::multi_index` is a drop-in replacement implemented over the KV intrinsics. It keeps
`emplace` / `modify` / `erase` / `find` / `require_find` / `get` / `lower_bound` / `upper_bound`,
`available_primary_key()`, `begin`/`end`/`cbegin`/`cend`/`rbegin`/`rend`, `indexed_by` +
`const_mem_fun` with up to 16 secondary indices, and object caching. `sysio::singleton` is likewise
preserved.

Secondary key types carried over: `uint64_t`, `uint128_t`, `double`, `long double`, and
`checksum256` (and anything else with a CDT serializer, via the generic encoder). Iteration order
is `memcmp` order over a big-endian encoding, so the fixed-width numeric types sort exactly as they
did.

What changed underneath, and where it shows:

| | Antelope | Wire |
|---|---|---|
| Primary row key | `(code, scope, table, primary_key)` | `table_id` + 16 bytes: `[scope:8B BE][pk:8B BE]` |
| Table identity | `name` embedded in the key | `table_id` (uint16, DJB2 hash of the table name) |
| Bytes per row | 24-byte key | 16-byte key — 8 bytes cheaper |
| RAM billing | per-row + per-index overhead | different constants — see [kv-ram-billing.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/kv-ram-billing.md) |
| `get_table_rows` response | bare value objects | `{key, value}` objects |

The response-shape change is the one that reaches your front end. `index_position` becomes
`index_name` (which accepts names *or* the old numbers), and `key_type`, `encode_type` and
`table_key` are no longer needed — old parameters are ignored rather than rejected, so simple
existing queries keep working. See
[get-table-rows-api.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/get-table-rows-api.md).

### Storage APIs available to a port

You do not have to move off `multi_index` to ship. When you are ready, Wire adds three
purpose-built types:

| Type | Use | Header |
|---|---|---|
| [`kv::table`](kv-table.md) | Multiple rows, user-defined key struct, optional secondary indices | `<sysio/kv_table.hpp>` |
| [`kv::scoped_table`](kv-scoped-table.md) | Same scope semantics as `multi_index`, byte-identical primary keys, no object-cache overhead | `<sysio/kv_scoped_table.hpp>` |
| [`kv::global`](kv-global.md) | One value per contract — config, counters | `<sysio/kv_global.hpp>` |

`kv::scoped_table` is the natural landing place for a ported `multi_index` table: identical scoping,
identical key bytes, less overhead. The full comparison and a step-by-step conversion are in the
[KV Storage Guide](kv-storage-guide.md).

Table names are also no longer confined to what `sysio::name` can hold — 13 characters of
`a-z1-5.`. The `_i` literal hashes an identifier of up to 128 characters from `a-zA-Z0-9_`:

```cpp
#include <sysio/hash_id.hpp>

kv::table<"user_balance_history"_i, my_key, my_val> users(get_self());
```

Annotate the value struct with `[[sysio::table("user_balance_history")]]` so the ABI carries the
readable name.

---

## Step 3 — resources: the contract pays

This is the part with no Antelope analogue, and the part that decides whether your ported contract
works at all.

**On Wire, the account billed for an action's CPU and NET is the contract that action invokes, not
the account that signed it.** An ordinary transaction names no payer, so the signer is neither
charged nor limit-checked. A user account with zero CPU, zero NET and no tokens can call every
provisioned contract on the network.

Capacity reaches a contract as a **policy** — a grant of CPU, NET and RAM weight issued by a
registered node owner through the `sysio.roa` contract. A contract with no policy has nothing to pay
with, so ordinary calls into it fail.

The full model — policies, node-owner tiers, how weight becomes throughput, subjective billing, and
how it compares to staking, REX and PowerUp — is documented in wire-sysio:
**[docs/roa-overview.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/roa-overview.md)**
(landing via [wire-sysio#583](https://github.com/Wire-Network/wire-sysio/pull/583)). What follows is
only what changes in *contract code*.

### The one mandatory source change: where you bill RAM

The Antelope idiom of naming the calling user as the RAM payer does not work on Wire:

```cpp
// Antelope — the user authorized the action, so the user pays for the row
_table.emplace( user, [&]( auto& row ) { ... } );
```

Wire rejects that transaction whatever policy the contract holds:

```
Requested payer alice did not authorize payment. Missing sysio.payer.
```

`apply_context::validate_account_ram_deltas` requires that a positive RAM delta billed to an account
*other than the executing contract* be backed by that account appearing in the action's
authorizations under the reserved **`sysio.payer`** permission. An ordinary `{alice, active}`
authorization does not satisfy it. (The bypass for `sysio.`-prefixed privileged contracts is
governance-granted and not a route an application can take.)

Two ways through, and it is a product decision rather than a technical one:

```cpp
// (a) The gasless path — the contract's policy covers the storage.
//     The caller's transaction is unchanged, and users stay free.
_table.emplace( get_self(), [&]( auto& row ) { ... } );

// (b) Keep billing the user. Requires the *client* to put {user, "sysio.payer"_n}
//     at index 0 of the action's authorizations, alongside a real permission it signs for.
_table.emplace( user, [&]( auto& row ) { ... } );
```

Option (b) has a consequence worth stating plainly: because `sysio.payer` must sit at **index 0**,
adding it also makes that user the CPU and NET payer for the action — so the user then needs their
own allocation. There is no way to charge a user for storage while the contract absorbs their
bandwidth. Most ports want (a).

CDT has no helper for building the `sysio.payer` authorization; the client constructs it, as
`permission_level{ user, "sysio.payer"_n }` at position 0 with a real, signed permission of the same
actor also present on the action.

`same_payer` behaves as it always did: on `modify` it keeps the row's existing payer; on `emplace`
there is no existing payer to keep, so it is rejected — same as Antelope.

### Notification handlers

Inside an `[[sysio::on_notify]]` handler, `get_self()` is the *notified* contract, and that is the
account whose policy pays for any rows the handler writes. Billing RAM to any other account from a
notify context is refused outright (`unprivileged contract cannot increase RAM usage of another
account within a notify context`) — the same rule Antelope has. Bill to `get_self()`.

### Who pays for inline actions and notifications

CPU is metered per **top-level** action — timed across that action's entire execution, including
every inline action and notification handler it triggers — and billed to that top-level action's
payer. NET is likewise charged per top-level action, on the transaction's serialized bytes; inline
actions add no NET, because they are not on the wire.

So the payer of the top-level action absorbs the CPU of the whole call tree beneath it:

- A user calls **your** contract, which inlines `sysio.token::transfer`. Your contract is the
  top-level payer, so **your** policy pays for the transfer's execution too.
- Another contract's action inlines into yours. **Their** contract is the top-level payer, so their
  policy pays for your code running.
- A contract notifies your `on_notify` handler. The top-level payer pays your handler's CPU; your
  own policy covers only the RAM your handler writes.

The CPU and NET a contract must be provisioned for is therefore the cost of everything its actions
*cause*, not just its own action bodies.

### Sizing

- **CPU and NET are replenishing rate limits, not balances.** A million calls and ten calls draw on
  the same weight. Exceeding it fails transactions until the averaging window rolls forward
  (`tx_cpu_usage_exceeded` / `tx_net_usage_exceeded`, naming the contract account); it does not run
  a balance down. Measure `cpu_usage_us` from a real trace on a test network — a figure from another
  chain or a debug build is not guidance.
- **RAM is occupancy, and it accumulates.** Under option (a) every row your contract creates is
  permanent state on *your* account. A contract expecting a million rows needs a policy sized for a
  million rows. This is where a port's costs actually live.
- **Failed transactions cost the payer nothing objectively** — `add_transaction_usage` is reached
  only on the success path. Budget headroom for peaks, not for failures.

### What you do not need to change

Nothing about `require_auth`, `has_auth`, `require_recipient`, permission levels, or
`action_wrapper`. Authorization and payment are separate concerns on Wire, exactly as they were on
Antelope; what changed is only *who* the payment lands on by default.

---

## Step 4 — host functions added and removed

### Added on Wire

| Function(s) | Purpose |
|---|---|
| 22 × `kv_*` — `kv_set`, `kv_get`, `kv_erase`, `kv_contains`, `kv_it_*` (8), `kv_idx_*` (10) | The KV database that replaces `db_*_i64`. Full signatures in [kv-intrinsics-reference.md](kv-intrinsics-reference.md). |
| `get_ram_usage(account) → int64_t` | Bytes currently used by an account. `<sysio/system.h>`. |
| `get_permission_lower_bound(account, permission, buffer, size)` | Iterate an account's permissions from a starting point. `<sysio/permission.h>`. |
| `blake2b_256(data, data_len, hash, hash_len)` | BLAKE2b-256 hash. `<sysio/crypto_ext.h>`. (`blake2_f`, the compression-function primitive, is in both.) |

### Removed on Wire

| Function(s) | What to do instead |
|---|---|
| `db_store_i64`, `db_update_i64`, `db_remove_i64`, `db_get_i64`, `db_next_i64`, `db_previous_i64`, `db_find_i64`, `db_lowerbound_i64`, `db_upperbound_i64`, `db_end_i64` | Use `multi_index` (unchanged source) or the `kv_*` intrinsics. |
| `db_idx64_*`, `db_idx128_*`, `db_idx256_*`, `db_idx_double_*`, `db_idx_long_double_*` (50 in total) | `indexed_by` on `multi_index`, or `kv_idx_*`. |
| `send_deferred`, `cancel_deferred` | See [Features with no Wire equivalent](#features-with-no-wire-equivalent). |
| `get_permission_last_used`, `get_account_creation_time` | No equivalent intrinsic. Track it in contract state, or read it off-chain. |

### Unchanged

Everything else you already use is present with the same signature: `require_auth`, `require_auth2`,
`has_auth`, `require_recipient`, `is_account`, `send_inline`, `send_context_free_inline`,
`read_action_data`, `action_data_size`, `current_receiver`, `publication_time`, `get_sender`,
`get_code_hash`, `set_action_return_value`, `current_time`, `get_block_num`, `is_feature_activated`,
the whole `print*` family, the transaction and TAPOS accessors, `check_transaction_authorization` /
`check_permission_authorization`, every `privileged.h` setter, `set_finalizers`, and the full
cryptographic surface — `sha1`/`sha256`/`sha512`/`sha3`/`ripemd160` and their `assert_` forms,
`recover_key`, `assert_recover_key`, `k1_recover`, `blake2_f`, `alt_bn128_add`/`_mul`/`_pair`,
`mod_exp`, and the ten `bls_*` functions.

---

## Features with no Wire equivalent

**Deferred transactions.** `send_deferred` and `cancel_deferred` do not exist — neither in CDT nor
in the chain. Antelope deprecated them; Wire does not implement them. Replacements, in order of
preference:

- **Inline actions** for work that must happen as part of the same transaction.
- **An off-chain scheduler** — a service that watches state and pushes the follow-up transaction.
  With contract-pays billing this is cheaper to operate than it was on Antelope: the relayer needs no
  per-user staking, only an account whose signatures the contract will accept.
- **A queue table plus a `crank`-style action** that any caller can invoke to advance pending work.

**`get_permission_last_used` / `get_account_creation_time`.** Not available as intrinsics. Record
what you need in your own tables, or query the history API off-chain.

---

## Worth adopting once the port works

None of this is required to ship. Do it after the contract builds, deploys and passes its tests.

- **[`kv::scoped_table`](kv-scoped-table.md)** — the lowest-friction upgrade from `multi_index`:
  identical scope semantics and byte-identical primary keys, without the object-cache overhead.
- **[`kv::table`](kv-table.md) and [`kv::global`](kv-global.md)** — user-defined key structs and
  single-value config, with zero-copy serialization for trivially-copyable POD values.
- **Long table names** via the `_i` literal (up to 128 chars of `a-zA-Z0-9_`).
- **[Protocol Buffers](protocol-buffers.md)** — protobuf-encoded action data via `sysio::pb<T>`, with
  the `FileDescriptorSet` embedded in the ABI so clients can decode it. Useful when you need schema
  evolution or a language-neutral wire format.
- **[Native testing](native-tester-compilation.md)** — compile the contract to a host executable
  with mockable intrinsics, and debug it under `gdb`/`lldb`.

---

## Porting checklist

1. Install Wire CDT; confirm `cdt-cpp --version`.
2. Rename `eosio` → `sysio` across sources, headers and CMake. Review the diff for string literals.
3. Build. Every remaining `eosio` reference is now a compiler error with a file and line.
4. Search for direct `db_*_i64` / `db_idx*` calls. `multi_index` users have nothing to do here.
5. **Find every `emplace` / `modify` that names a user as payer.** Decide, per table, whether the
   contract absorbs the RAM (`get_self()`) or the client will supply `sysio.payer`.
6. Replace any `send_deferred` with an inline action, an off-chain relayer, or a crank action.
7. Regenerate the ABI and diff it: `cdt-abidiff old.abi new.abi`.
8. Test natively — see [native-tester-compilation.md](native-tester-compilation.md).
9. Deploy to a test network. Get a policy on the **contract account** before the first call, sized
   from the ×10 `setcode` charge plus the rows the contract will hold.
10. Update front-end `get_table_rows` calls for the `{key, value}` response shape.
11. Measure `cpu_usage_us` and the action's billable NET size from a real trace, and size the
    production policy from those numbers.

---

## Further reading

**In this repository**

| Doc | Topic |
|---|---|
| [KV Storage Guide](kv-storage-guide.md) | The storage layer, the decision matrix, and `multi_index` → `kv::table` migration |
| [kv-intrinsics-reference.md](kv-intrinsics-reference.md) | All 22 KV host functions |
| [kv-abi-key-metadata.md](kv-abi-key-metadata.md) | How key metadata lands in the ABI |
| [native-tester-compilation.md](native-tester-compilation.md) | Host-side contract testing |
| [protocol-buffers.md](protocol-buffers.md) | Protobuf action data |
| [BUILD.md](../BUILD.md) | Building, installing and packaging the toolchain |

**In [wire-sysio](https://github.com/Wire-Network/wire-sysio)**

| Doc | Topic |
|---|---|
| [docs/roa-overview.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/roa-overview.md) | The resource model in full — policies, tiers, throughput, spam control, scenarios ([#583](https://github.com/Wire-Network/wire-sysio/pull/583)) |
| [docs/kv-ram-billing.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/kv-ram-billing.md) | KV vs legacy RAM billing, with the per-row constants |
| [docs/get-table-rows-api.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/get-table-rows-api.md) | Querying tables, and what changed from the old endpoint |
| [docs/key-formats.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/key-formats.md) | Supported public-key formats |
| [docs/contract-upgrade-order.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/contract-upgrade-order.md) | Deploying a coupled set of contracts safely |
