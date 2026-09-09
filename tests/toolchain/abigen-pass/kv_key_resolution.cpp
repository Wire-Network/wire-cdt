// Where a [[sysio::kv_key]] struct is looked for, and which one wins.
//
// The rule: the row, then the enclosing class, then each enclosing namespace out to the
// translation unit; a struct or an alias to one; and the FIRST scope that declares the name
// decides. It is not C++ name lookup -- Sema is gone by the time abigen runs -- so it is stated
// rather than approximated, and anything outside it is refused by name rather than guessed at.
//
// The shadowed case is why the "first scope wins" half matters. The lookup used to match only
// direct struct declarations, so an ALIAS was invisible: a contract-local `using logical_key =
// actual_key;` was skipped, the walk climbed past it into the namespace, and a global struct of
// the same name was silently chosen instead -- publishing another table's key schema, with the
// build clean. Climbing past a nearer declaration is what makes an approximation dangerous.
#include <sysio/sysio.hpp>
#include <sysio/kv_table.hpp>

using namespace sysio;

struct phys_key {
   uint64_t raw_id;
   SYSLIB_SERIALIZE(phys_key, (raw_id))
};

// Found from a row nested in the contract class -- an enclosing namespace is in scope.
struct namespace_key {
   name owner;
   SYSLIB_SERIALIZE(namespace_key, (owner))
};

// Shadowed below by a contract-local alias of the same name, and must NOT be chosen.
struct logical_key {
   name wrong_outer_owner;
   SYSLIB_SERIALIZE(logical_key, (wrong_outer_owner))
};

class [[sysio::contract("kv_key_resolution")]] kv_key_resolution : public contract {
public:
   using contract::contract;

   struct actual_key {
      uint64_t intended_inner_id;
      SYSLIB_SERIALIZE(actual_key, (intended_inner_id))
   };

   using logical_key = actual_key;   // nearer than the global, and an alias

   struct class_key {
      uint64_t class_id;
      SYSLIB_SERIALIZE(class_key, (class_id))
   };

   // resolved from the row itself
   struct [[sysio::table, sysio::kv_key("row_key")]] nested_val {
      struct row_key {
         uint64_t row_id;
         SYSLIB_SERIALIZE(row_key, (row_id))
      };
      uint64_t balance;
      SYSLIB_SERIALIZE(nested_val, (balance))
   };

   // resolved from the enclosing class
   struct [[sysio::table, sysio::kv_key("class_key")]] class_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(class_val, (balance))
   };

   // resolved from the enclosing namespace
   struct [[sysio::table, sysio::kv_key("namespace_key")]] ns_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(ns_val, (balance))
   };

   // resolved to the nearer ALIAS, not the global struct it shadows
   struct [[sysio::table, sysio::kv_key("logical_key")]] shadowed_val {
      uint64_t balance;
      SYSLIB_SERIALIZE(shadowed_val, (balance))
   };

   using nested_rows   = kv::table<"nestedrows"_n, phys_key, nested_val>;
   using class_rows    = kv::table<"classrows"_n,  phys_key, class_val>;
   using ns_rows       = kv::table<"nsrows"_n,     phys_key, ns_val>;
   using shadowed_rows = kv::table<"shadowrows"_n, phys_key, shadowed_val>;

   [[sysio::action]]
   void test() {
      nested_rows   a(get_self());
      class_rows    b(get_self());
      ns_rows       c(get_self());
      shadowed_rows d(get_self());
      a.emplace(get_self(), {1}, {10});
      b.emplace(get_self(), {2}, {20});
      c.emplace(get_self(), {3}, {30});
      d.emplace(get_self(), {4}, {40});
   }
};
