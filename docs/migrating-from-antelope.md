# Migrating a Contract from an Antelope Chain to Wire

For developers arriving with a working contract from EOS, Telos, WAX, Jungle, or any other
Antelope-family chain. It covers what to install, what to change in the source, and — the part that
surprises people — why the contract, not the user, is the one that has to be provisioned before
ordinary calls will go through.

Wire is Antelope-derived. The account model, permissions, actions, inline actions, notifications,
ABIs, `check()`, `name`, `asset`, `symbol`, `time_point`, the cryptographic intrinsics and the
serialization format are all the ones you already know. Three things are genuinely different:

| What | Size of the job |
|---|---|
| **Every `eosio` identifier is spelled `sysio`** | Mechanical. One `sed` pass over the source. |
| **The legacy `db_*_i64` table store no longer exists** | Small: `sysio::multi_index` is a compatibility shim over the new KV store, so your table declarations carry over. Expect a mechanical `it++` → `++it` sweep, and note that RAM sizing and client-side `get_table_rows` calls both change. |
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
libc and libc++, the WASM contract library, and the CMake package.

```bash
sudo apt install ./wire-cdt_<version>_amd64.deb ./wire-cdt-dev_<version>_amd64.deb
```

Install **both** packages. The base package carries the compiler drivers and the WASM libraries;
`wire-cdt-dev` carries `libnative*`, `scripts/gen_native_dispatch.py` and
`share/cdt/native-contract-src`, which is what the [native-testing path](#test-without-a-chain)
below needs. Neither package pulls in CMake or a build tool, so on a clean machine also:

```bash
sudo apt install cmake build-essential jq
```

`build-essential` for the `make` the generated project uses, and `jq` for the ABI diff in
[Step 1](#step-1--rename-eosio-to-sysio).

Under the deb/rpm layout the toolchain lives in `/usr/lib/cdt` and only the public entry points —
the `cdt-*` and `sysio-*` names — are symlinked into `/usr/bin`. The bundled `clang`, `lld`,
`wasm-ld`, `opt`, `llc` and `llvm-*` binaries stay in `/usr/lib/cdt/bin`, off `PATH`, so nothing
shadows your distro's compiler.

Or extract the portable tarball to `/opt` (`/opt/wire-cdt`), which coexists with a deb install.
Building from source is documented in [BUILD.md](../BUILD.md).

> **The tarball has no such separation.** Its `bin/` holds every binary, the unprefixed `clang`,
> `clang++`, `lld`, `ld.lld`, `wasm-ld`, `opt`, `llc` and `llvm-*` included, so putting
> `/opt/wire-cdt/bin` on `PATH` *does* shadow the distro toolchain. Invoke it by absolute path
> (`/opt/wire-cdt/bin/cdt-cpp`), alias the `cdt-*` names, or symlink just the public entry points
> into a directory of your own that is on `PATH`.
>
> The same applies if **AntelopeIO CDT 3.0+** is installed: both projects publish `cdt-cpp`,
> `cdt-cc`, `cdt-ld` and `cdt-init`, so those names collide. Check `which cdt-cpp`, or invoke the
> one you want by absolute path.

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
| `eosio-wast2wasm` / `eosio-wasm2wast` | `cdt-wast2wasm` / `cdt-wasm2wast` |
| `eosio-pp` | `sysio-pp` (also aliased `cdt-pp`) |
| `find_package(cdt)` / `${CDT_ROOT}` | unchanged — the CMake package is still named `cdt` |

`cleos` becomes `clio`, `nodeos` becomes `nodeop`, and `keosd` becomes `kiod`.

### Create a project

```bash
cdt-init -project=mycontract          # add -path=<dir> to place it elsewhere
cd mycontract/build && cmake .. && make
```

`cdt-init` scaffolds `src/`, `include/`, `ricardian/`, `build/` and a CMake project that already
calls `find_package(cdt)` and `add_contract`. `-bare` skips the directories and the CMake
files, emitting four files into the project root: `<name>.hpp`, `<name>.cpp`,
`<name>.contracts.md` and `README.txt`.

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
fatal, because on Wire the contract is the payer. Before an ordinary caller can reach it, a node
owner must issue it a policy:

```bash
clio push action sysio.roa addpolicy '{"owner":"mycontract","issuer":"<nodeowner>","net_weight":"0.1000 SYS","cpu_weight":"0.1000 SYS","ram_weight":"1.0000 SYS","time_block":0,"network_gen":<gen>}' -p <nodeowner>@active
```

`<gen>` is the **network generation the issuer is registered in**, not a constant. `addpolicy`
opens `nodeowners` scoped to the generation you pass and requires the issuer to be present there, so
hard-coding `0` fails once the network has rolled over — or, worse, draws against an older
generation's allocation. Read the current generation from the `roastate` singleton and confirm the
issuer appears in that generation's `nodeowners`:

```bash
clio get table sysio.roa roastate                          # network_gen
clio get table sysio.roa nodeowners --limit 100            # issuer must be listed here
```

`nodeowners` is a scoped table keyed by generation, but **do not pass `-S <gen>`**. CDT declares
every scoped table's scope as a `name`, and the chain honours that: it tries `name(scope)` first
and only falls back to a plain integer when that throws. `1`-`5` are valid `name` characters, so
`-S 1` is read as the name `"1"` — 576460752303423488 — and the query returns `{"rows":[]}`, which
reads as "your issuer is not a node owner" when it means "you asked for the wrong scope".
Generations containing a `0` or a `6`-`9` happen to work, because `name()` rejects those
characters and the integer fallback runs. Omitting `-S` iterates every scope; read `network_gen`
off each row.

Keep the JSON on one line: a `\` used to wrap it would fall *inside* the single quotes and be
passed through as a literal backslash rather than continuing the command.

Without it, an ordinary contract-paid call fails with
`account mycontract net usage is too high: 132 > 0` — which looks like a broken contract and is not
one. "Ordinary" is the operative word: because billing keys on the payer alone, a caller that names
itself with `sysio.payer` (see [Step 3](#step-3--resources-the-contract-pays)) pays for the action
itself and never consults the contract's zero CPU and NET. So an unprovisioned contract is
unreachable by default, not universally inert — a provisioned caller or relayer can still drive it.

That escape hatch covers bandwidth only. RAM the contract bills to **itself** still comes out of the
contract's own quota, so a contract with no policy can be driven only as far as its first write to
its own tables.

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
# GNU sed only. On macOS: `brew install gnu-sed` and use `gsed`. `sed -i ''` alone is not
# enough -- BSD sed has neither `\b` nor `\|`, so `\beosio\b` matches a literal "beosiob"
# (renaming nothing, silently) and the `\(assert\|...\)` group is a syntax error.
grep -rl 'eosio\|EOSIO\|EOSLIB' src include \
  | xargs sed -i -e 's/\beosio\b/sysio/g' \
                 -e 's/\beosio_\(assert\|assert_message\|assert_code\|exit\)\b/sysio_\1/g' \
                 -e 's/\bEOSIO_/SYSIO_/g' \
                 -e 's/\bEOSLIB_/SYSLIB_/g'
```

The second expression is not redundant. `_` is a word character, so `\beosio\b` finds no boundary
in `eosio_assert` and leaves the whole C API — `eosio_assert`, `eosio_assert_message`,
`eosio_assert_code`, `eosio_exit` — untouched. They are named explicitly instead.

Review the diff before trusting it: a `sed` this broad will also rewrite `eosio.token` to
`sysio.token` inside string literals and `"eosio"_n` to `"sysio"_n`. On Wire those renames are
usually correct — the system account is `sysio` and the token contract is `sysio.token` — but the
account names in *your* contract's own tables and constants are yours to decide. Whatever the
recipe misses, the compiler will name for you: with no `eosio` compatibility aliases, every
survivor is an error with a file and line.

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

Do not reach for `cdt-abidiff` to see this particular change. It compares tables only by `name` and
`type`, so `index_type`, `key_names`, `key_types`, `table_id` and the secondary-index metadata are
all invisible to it, and its version check reduces `eosio::abi/1.2` and `sysio::abi/1.2` to the same
number — a port whose tables kept their names can come back reporting no difference at all. For this
comparison, normalize and diff the JSON directly:

```bash
jq -S . old.abi > /tmp/old.json && jq -S . new.abi > /tmp/new.json && diff -u /tmp/old.json /tmp/new.json
```

`cdt-abidiff` remains useful for what it does check — structs, fields, actions, types and variants.

---

## Step 2 — storage

### The legacy database is gone

The `db_store_i64` / `db_find_i64` / `db_idx64_*` / `db_idx128_*` / `db_idx256_*` /
`db_idx_double_*` / `db_idx_long_double_*` intrinsics do not exist on Wire. The chain exports none
of them. Today a contract declaring one still *links*, because CDT's `imports/cdt.imports.in` is
handed to `wasm-ld` as `--allow-undefined-file` and still lists them, and the failure surfaces at
deploy; once [wire-cdt#112](https://github.com/Wire-Network/wire-cdt/pull/112) removes
them from that list it becomes a link error instead. `<sysio/db.h>` is a stub that
forwards to `<sysio/kv.h>`; the last stale declarations are removed from CDT's import list by
[wire-cdt#112](https://github.com/Wire-Network/wire-cdt/pull/112). Contract state lives in a
key-value store addressed by a compile-time `table_id`.

Code that called those intrinsics directly must be rewritten. Code that used `multi_index` — which
is nearly all of it — carries over with one mechanical exception, below.

### `multi_index` still works

`sysio::multi_index` is a compatibility shim implemented over the KV intrinsics. It keeps
`emplace` / `modify` / `erase` / `find` / `require_find` / `get` / `lower_bound` / `upper_bound`,
`available_primary_key()`, `begin`/`end`/`cbegin`/`cend`/`rbegin`/`rend`, `indexed_by` +
`const_mem_fun` with up to 16 secondary indices, and object caching. `sysio::singleton` is likewise
preserved.

**The source change: postfix `++` and `--` on iterators are deleted.** Wire declares
`operator++(int)` and `operator--(int)` as `= delete` on both the primary and the secondary-index
iterator, so the classic loop stops compiling:

```cpp
for (auto it = idx.begin(); it != idx.end(); it++)   // error: call to deleted operator
for (auto it = idx.begin(); it != idx.end(); ++it)   // rewrite to this
```

A postfix increment has to copy the iterator, and copying a KV iterator means duplicating a
host-side handle — affordable to do deliberately, not to do once per loop step, which is why the
postfix forms are deleted rather than merely discouraged. The sweep
is mechanical — `it++` → `++it`, `it--` → `--it` — and the compiler finds every one.

Two behaviours that a port depends on match upstream. **Both arrive with
[wire-cdt#113](https://github.com/Wire-Network/wire-cdt/pull/113) and are not present in a CDT built
before it** — on an older toolchain a duplicate `emplace` silently overwrites the row and strands its
secondary mapping, and the bounds take only `uint64_t`:

- **A duplicate primary key aborts.** `emplace` rejects a key that already exists, as `db_store_i64`
  did on Antelope, so a contract that relied on that failure keeps failing loudly instead of
  silently overwriting the row.
- **`lower_bound` / `upper_bound` accept a `name`** as well as a `uint64_t`, so a table keyed on
  `name` reads as it does upstream. (Taking the bare address of either — `&table_type::lower_bound`
  — does not compile here, because they are an overload set, and does not compile upstream
  either, because upstream's are member templates whose parameter cannot be deduced. A named
  `static_cast<const_iterator (table_type::*)(uint64_t) const>(...)` resolves one on Wire.)

#113 also makes `emplace`, `modify` and `erase` abort when the handle's code is not the receiving
account, again matching upstream. If your port constructs a table handle on another contract's
account, it must be read-only.

Secondary key types carried over: `uint64_t`, `uint128_t`, `double`, `long double`, and
`checksum256`. Iteration order is `memcmp` order over a big-endian encoding, so the fixed-width
numeric types sort exactly as they did.

The constraint is **`std::is_trivially_copyable`**, enforced by a `static_assert` in
`secondary_index_view`, not "has a serializer" — so `std::string` and `std::vector` secondary keys
are rejected at compile time even though CDT can serialize them. A variable-length secondary key
needs a fixed-width surrogate: hash it into a `checksum256`, or truncate to a `uint64_t` and
disambiguate collisions against the primary row.

What changed underneath, and where it shows:

| | Antelope | Wire |
|---|---|---|
| Primary row key | `(code, scope, table, primary_key)` | `table_id` + 16 bytes: `[scope:8B BE][pk:8B BE]` |
| Table identity | `name` embedded in the key | `table_id` (uint16, DJB2 over the 8 big-endian bytes of the table name's raw `uint64`) |
| Per-row RAM | `key_value_object`: 108 + value | `kv_object`: 112 + 16-byte key + value |
| RAM billing | per-row + per-index overhead, plus one 108-byte `table_id_object` per table | no `table_id_object` — the `table_id` is a `uint16` on the row — but a higher per-row constant |

**Budget more RAM, not less.** The `table_id_object` saving is amortised away as soon as a table
has more than a handful of rows, and the per-row cost is higher: for a dense table with a 16-byte
value, wire-sysio's own figures are **124 bytes legacy vs 144 for `sysio::multi_index` (+16%)**,
or 136 for `kv::table` (+10%). Size a ported contract's RAM policy up, not down. The full
breakdown is in
[kv-ram-billing.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/kv-ram-billing.md).
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
`a-z1-5.`. The `_i` literal hashes the identifier instead, so `a-zA-Z0-9_` and longer names are
usable. (`hash_id::max_length` is 128, but nothing validates against it or against an alphabet —
treat both as conventions, not as checks.)

```cpp
#include <sysio/hash_id.hpp>
#include <sysio/kv_table.hpp>

kv::table<"user_balance_history"_i, my_key, my_val> users(get_self());
```

Annotate the value struct with `[[sysio::table("user_balance_history")]]` so the ABI carries the
readable name.

> **Use `_i` only for names longer than 13 characters.** The two sides currently derive `table_id`
> differently for short names: `_i` always DJB2-hashes the string, but the ABI generator routes an
> annotated name of 13 characters or fewer through the legacy `string_to_name` encoding instead. The
> row is written under one id and described in the ABI under another, so RPC metadata points at the
> wrong table:
>
> | Annotated name | Runtime `table_id` (`_i`) | ABI `table_id` |
> |---|---|---|
> | `user_table` (10 chars) | 61956 | 3509 |
> | `user_balance_history` (20 chars) | 26461 | 26461 ✓ |
>
> Above 13 characters both sides hash, so they agree — which is the case `_i` exists for.
>
> **`_n` is not always a way out.** Its alphabet is `.12345a-z`, so a name containing any other
> character cannot be expressed at all: `"user_table"_n` is a *compile* error, because `_` is not in
> the alphabet. `_n` is the fix only for short names that are already valid Antelope names. A short
> identifier that is not — anything with `_`, a digit outside `1-5`, or an uppercase letter — has to
> be renamed, or lengthened past 13 characters, until abigen is fixed.

> **`kv::global` + `_i` is broken at every length — avoid the combination.** The rule above holds
> for `kv::table`, where the annotated name and the `_i` literal resolve to one ABI entry. A
> `kv::global` produces **two**, and neither length is usable:
>
> | `kv::global<"X"_i, T>` with `[[sysio::table("X")]]` | Result |
> |---|---|
> | `app_config` (10 chars) | compiles; ABI carries `app_config` → 38424 **and** a decoded-hash name `idrzzw4ktxljf` → 21489. The row is written under 21489. |
> | `app_config_table` (16 chars) | **fails at link**: `table_id collision: 'app_config_table' and 'wdfp4hyupu.q2' both have table_id 42322` — the two registrations now compute the same id and trip the collision check. (The diagnostic comes from `cdt-codegen`'s link-stage ABI finalize, before `wasm-ld`; `cdt-cpp -c` on the same file succeeds.) |
>
> Use `_n` for a global whose name fits `.12345a-z` within 13 characters, which covers most config
> singletons. `tests/unit/test_contracts/hash_id_tests.cpp` and `examples/hash_id_example` both
> still use the short `_i` form and are affected; they are left as-is here because renaming them
> hits the second row of that table.

---

## Step 3 — resources: the contract pays

This is the part with no Antelope analogue, and the part that decides whether your ported contract
works at all.

**On Wire, the account billed for an action's CPU and NET is the contract that action invokes, not
the account that signed it.** An ordinary transaction names no payer, so the signer is neither
charged nor limit-checked *by consensus*. A user account with zero CPU, zero NET and no tokens can
call every provisioned contract on the network.

The qualifier matters. Objective billing keys on the payer and nothing else, so the signers' own
limits are never consulted — but a producer also runs **subjective** billing, which meters **each
top-level action's first authorizer** where that account is not the payer, and can refuse the
transaction when one of them has burned its subjective budget on earlier failures
(`Subjectively terminated trx ... Authorized account ... exceeded subjective CPU limit`). A
multi-action transaction therefore has as many candidates as it has distinct first authorizers, and
any one of them can stop it. That is node-local rather than consensus, and a spam control rather
than a charge, but "the signer is never metered" is too strong: an account that fails transactions
in a loop can be throttled.

Capacity reaches a contract as a **policy** — a grant of CPU, NET and RAM weight issued by a
registered node owner through the `sysio.roa` contract. A contract with no policy has nothing to pay
with, so ordinary calls into it fail.

The full model — policies, node-owner tiers, how weight becomes throughput, subjective billing, and
how it compares to staking, REX and PowerUp — is documented in wire-sysio:
`wire-sysio`'s `docs/roa-overview.md`, which lands with
[wire-sysio#583](https://github.com/Wire-Network/wire-sysio/pull/583) — read it there until that
merges; the path does not exist on `master` yet. What follows is only what changes in *contract
code*.

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
adding it also makes that user the CPU and NET payer for **that action** — so the user then needs
their own allocation. For the direct, top-level call shown above, there is no way to charge a user
for storage while the contract absorbs their bandwidth. Most ports want (a).

> **The separation is possible, but not at this level.** If the user pre-delegates a real permission
> to `<contract>@sysio.code`, the contract can send an *inline* action authorized
> `{user, sysio.payer}, {user, delegated}`. RAM validation reads the marker on the inline action, so
> the row bills to the user; objective CPU and NET are accounted only over the transaction's
> top-level actions, so the contract remains the bandwidth payer. It costs a persistent, up-front
> delegation from every user — not just a signature on the call — so it is an advanced pattern
> rather than an alternative to the two options above.

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
payer. NET is likewise charged per top-level action, but not on the whole transaction's bytes:

```
overhead   = 16 + signatures + extensions + header      (whole transaction)
per_action = overhead / number_of_actions + 1           (integer division, then +1)

action NET = that action's own serialized billable size
           + per_action
           + its matching context_free_data, for a context-free action
```

The `+ 1` is unconditional, not a rounding step: consensus divides, then adds one. Where the
overhead divides evenly — every single-action transaction, for instance — that is one byte *more*
than a true ceiling, so the split over-bills by up to a byte per action rather than under-billing.
Immaterial for sizing, but it is the arithmetic, and `transaction.cpp` says as much.

Inline actions add no NET at all, because they never appear on the wire. Two consequences worth
designing around: **signatures cost NET**, so a co-signed action is dearer than a solo one; and
**batching amortizes the overhead**, so ten actions in one transaction cost less NET than ten
separate transactions.

So the payer of the top-level action absorbs the CPU of the whole call tree beneath it:

- A user calls **your** contract, which inlines `sysio.token::transfer`. Your contract is the
  top-level payer, so **your** policy pays for the transfer's execution too.
- Another contract's action inlines into yours. **Their** contract is the top-level payer, so their
  policy pays for your code running.
- A contract notifies your `on_notify` handler. The top-level payer pays your handler's CPU; your
  own policy covers only the RAM your handler writes.

**Only CPU has that property.** A contract's CPU provisioning has to cover everything its actions
*cause*, since the whole tree is timed against the top-level payer. Its NET provisioning does not:
NET is fixed by what arrives on the wire — the input action's own bytes, its share of the
transaction overhead, and its context-free data — and no inline action or notification adds to it,
however deep the tree goes.

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
  only on the success path. They are not free, but the cost does not land on your contract:
  a failure accrues *subjective* CPU against the transaction's **first authorizer** on the node
  that ran it, and a stock node ships with `disable-subjective-payer-billing` **on**
  (`producer_plugin.cpp`), so `subjective_bill_failure` skips the payer entirely and bills only
  the authorizer. Under contract-pays those are different accounts — the payer is your contract,
  the first authorizer is the caller — so a caller's failures are throttled against the caller,
  which is the whole point of that default. Budget objective headroom for peaks; failures belong
  in the caller's retry logic, not your policy.

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
| `db_store_i64`, `db_update_i64`, `db_remove_i64`, `db_get_i64`, `db_next_i64`, `db_previous_i64`, `db_find_i64`, `db_lowerbound_i64`, `db_upperbound_i64`, `db_end_i64` | Use `multi_index` (see the [compatibility adjustments](#step-2--storage)) or the `kv_*` intrinsics. |
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

1. Install Wire CDT — the **base and `-dev` packages both** — plus CMake and a build tool. Confirm
   `cdt-cpp --version` resolves to the binary you meant.
2. Rename `eosio` → `sysio` across sources, headers and CMake, including the `eosio_assert` /
   `eosio_exit` C API that a `\beosio\b` pass skips. Review the diff for string literals.
3. Build. Every remaining `eosio` reference is now a compiler error with a file and line.
4. Sweep `it++` → `++it` and `it--` → `--it` on table iterators; the postfix forms are deleted.
5. Search for direct `db_*_i64` / `db_idx*` calls — those must be rewritten. `multi_index` users
   need only the iterator sweep in step 4.
6. Check secondary-index key types are `std::is_trivially_copyable`; give any `std::string` or
   `std::vector` key a fixed-width surrogate.
7. **Find every `emplace` / `modify` that names a user as payer.** Decide, per table, whether the
   contract absorbs the RAM (`get_self()`) or the client will supply `sysio.payer`.
8. Replace any `send_deferred` with an inline action, an off-chain relayer, or a crank action.
9. Regenerate the ABI and diff it — `jq -S . old.abi > a && jq -S . new.abi > b && diff -u a b`,
   not `cdt-abidiff`, which does not compare the table metadata that changed.
10. Test natively — see [native-tester-compilation.md](native-tester-compilation.md).
11. Deploy to a test network. Get a policy on the **contract account** before the first call, sized
    from the ×10 `setcode` charge plus the rows the contract will hold.
12. Update front-end `get_table_rows` calls for the `{key, value}` response shape.
13. Measure `cpu_usage_us` and the action's billable NET size from a real trace, and size the
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
| `docs/roa-overview.md` — in [wire-sysio#583](https://github.com/Wire-Network/wire-sysio/pull/583) until it merges | The resource model in full — policies, tiers, throughput, spam control, scenarios |
| [docs/kv-ram-billing.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/kv-ram-billing.md) | KV vs legacy RAM billing, with the per-row constants |
| [docs/get-table-rows-api.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/get-table-rows-api.md) | Querying tables, and what changed from the old endpoint |
| [docs/key-formats.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/key-formats.md) | Supported public-key formats |
| [docs/contract-upgrade-order.md](https://github.com/Wire-Network/wire-sysio/blob/master/docs/contract-upgrade-order.md) | Deploying a coupled set of contracts safely |
