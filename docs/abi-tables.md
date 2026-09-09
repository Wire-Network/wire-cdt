# What abigen describes, and what it refuses

The ABI is what a client, `clio get table` and SHiP use to read a contract's state. abigen builds
it from the contract's declarations, and it can only describe shapes it can name.

**One rule: a table row is a struct the contract declares.** Everything below follows from it.

This is the same rule upstream Antelope CDT has always had — its abigen takes the row's
`CXXRecordDecl` and dereferences it — so a contract ported from another Antelope chain behaves
here exactly as it did there.

## The three things a table needs

| | how |
|---|---|
| a **row struct** | `struct [[sysio::table]] row { … };` |
| a **name** | the table's first template parameter, or `[[sysio::table("name")]]` on the row |
| to be **visible** | declared in the contract class (alias or data member), or its row annotated |

```cpp
class [[sysio::contract("mycontract")]] mycontract : public contract {
public:
   using contract::contract;

   struct [[sysio::table]] account {
      sysio::name  owner;
      sysio::asset balance;
      uint64_t primary_key() const { return owner.value; }
      SYSLIB_SERIALIZE(account, (owner)(balance))
   };

   using accounts = sysio::multi_index<"accounts"_n, account>;   // alias in the class
   sysio::singleton<"config"_n, cfg_row> cfg{get_self(), get_self().value};   // or a member
};
```

Both forms work, and so do `typedef`, `sysio::kv_singleton`, `kv::table`, `kv::scoped_table` and
`kv::global`. A table declared **only** as a local variable inside an action is described when its
row carries `[[sysio::table]]`, and otherwise is not seen at all — declare it in the class.

## Naming

A table's name comes from its template parameter:

```cpp
sysio::multi_index<"accounts"_n, account>       // -> table "accounts"
```

`_n` names are at most 13 characters from `.12345a-z`. For anything longer, use `_i`, which
DJB2-hashes its argument — and because a hash is not reversible, **the readable name has to come
from the row's annotation**:

```cpp
struct [[sysio::table("user_preferences")]] preference { … };
using prefs = kv::table<"user_preferences"_i, pref_key, preference>;   // -> "user_preferences"
```

Without the annotation an `_i` table is published under the decode of its hash, which no client
can address. A row is always a struct, so there is always somewhere to put the annotation.

If the row struct is declared at namespace scope rather than inside the contract class, say which
contract it belongs to — nothing else associates it:

```cpp
struct [[sysio::table("user_preferences"), sysio::contract("mycontract")]] preference { … };
```

## What is refused

A row that is not a struct the contract declares:

```
error: abigen error (table 'balances' has row type 'unsigned long long', which is not a
       contract struct; a table row must be a struct this contract declares -- wrap the
       value in one)
```

That covers a scalar, `std::string`, `sysio::checksum256`, `fixed_bytes<N>`, a container
(`std::vector`, `std::map`, `std::optional`), `std::variant`, `std::tuple` and
`binary_extension`. The fix is one line:

```cpp
sysio::singleton<"config"_n, uint64_t>  cfg;             // refused

struct [[sysio::table]] cfg_row { uint64_t value; };     // supported
sysio::singleton<"config"_n, cfg_row>   cfg;
```

The wrapper is not ceremony: it is what gives the value a field name, which is what makes the
table readable by anything that is not the contract itself.

## Two tables, one name

The ABI holds one table per name. Two that ask for the same one are reported and only the first
is described:

```
warning: abigen warning (two different tables are both called 'same': table_id 6122 over
         'c::row' keyed on (id), and table_id 6122 over 'c::row' keyed on (owner); the ABI
         can describe only one, and the second is not described)
```

`[[sysio::table("name")]]` is resolved across the whole link, so it renames a table only when the
row backs exactly one and the name is free; otherwise it is dropped with a warning saying which
condition failed. A rename that only becomes possible by swapping two names — `alpha` to `bravo`
and `bravo` to `alpha` at once — is refused: give one of them a different name.

## See also

- [KV Storage Guide](kv-storage-guide.md) — which table type to use
- [KV ABI key metadata](kv-abi-key-metadata.md) — `[[sysio::kv_key]]` and key layouts
