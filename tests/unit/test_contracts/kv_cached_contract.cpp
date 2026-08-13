/**
 *  @file
 *  @copyright defined in sysio.cdt/LICENSE.txt
 *
 *  Contract-side coverage for kv::cached_global and sysio::cached_kv_singleton.
 *
 *  Both singletons are held as CONTRACT MEMBERS, which is the shape that matters: the
 *  generated dispatcher instantiates the contract as a temporary and destroys it the moment
 *  the action returns, so a member that unconditionally writes itself back at destruction
 *  makes EVERY action -- including a pure query -- issue a kv_set and fail inside a read-only
 *  transaction. The cached types only write when an action actually mutated something, so
 *  `roread` below is legal read-only while `bump` is correctly still rejected.
 *
 *  Driven by tests/integration/kv_cached_tests.cpp.
 */

#include <sysio/sysio.hpp>
#include <sysio/kv_global.hpp>
#include <sysio/kv_singleton.hpp>

using namespace sysio;

class [[sysio::contract("kv_cached_contract")]] kv_cached_contract : public contract {
public:
   // No default member initializers below. An NSDMI on a NESTED class is parsed in the
   // ENCLOSING class's complete-class context, so while kv_cached_contract is still being
   // defined the payload's implicit default constructor is not yet known and the
   // is_default_constructible_v assertions inside kv::global / kv_multi_index both fail.

   /// Unscoped payload, fixed-serializable (kv::global's zero-copy path).
   struct [[sysio::table("cachcfg")]] cfg {
      uint64_t rate;
      uint64_t flags;
      SYSLIB_SERIALIZE(cfg, (rate)(flags))
   };

   /// Scoped payload, variable length (kv_singleton's pack/unpack path).
   struct [[sysio::table("cachsng")]] scoped_cfg {
      uint64_t    version;
      std::string label;
      SYSLIB_SERIALIZE(scoped_cfg, (version)(label))
   };

   using cfg_cached = kv::cached_global<"cachcfg"_n, cfg>;
   using sng_cached = cached_kv_singleton<"cachsng"_n, scoped_cfg>;

   kv_cached_contract(name s, name code, datastream<const char*> ds)
      : contract(s, code, ds)
      , _cfg(s)
      , _sng(s, s.value) {}

   /// Create both rows. Deferred: the writes land when the members destruct.
   [[sysio::action]]
   void seed() {
      _cfg.set({42, 7}, get_self());
      _sng.set({1, "hello"}, get_self());
   }

   /// Pure reads. Issues no kv_set at all, so this action is legal inside a read-only
   /// transaction -- the whole point of the cached types.
   [[sysio::action]]
   void roread() {
      check(_cfg.exists(), "roread: cfg row missing");
      check(_cfg.get().rate == 42, "roread: cfg rate");
      check(_cfg.get().flags == 7, "roread: cfg flags");

      check(_sng.exists(), "roread: sng row missing");
      check(_sng.get().version == 1, "roread: sng version");
      check(_sng.get().label == "hello", "roread: sng label");
   }

   /// Reads that miss are still reads: no write, so this stays read-only legal even though
   /// the rows are absent.
   [[sysio::action]]
   void roabsent() {
      check(!_cfg.exists(), "roabsent: cfg row should be absent");
      check(_cfg.get_or_default({99, 99}).rate == 99, "roabsent: cfg default");
      check(_sng.get_or_default({7, "def"}).label == "def", "roabsent: sng default");
   }

   /// Mutates both singletons. Must still be REJECTED inside a read-only transaction.
   [[sysio::action]]
   void bump() {
      _cfg.modify(get_self(), [](cfg& c) { c.rate += 1; });
      _sng.modify(get_self(), [](scoped_cfg& s) { s.version += 1; });
   }

   /// Confirms a previous bump's deferred writes actually reached the store.
   [[sysio::action]]
   void chkbump() {
      check(_cfg.get().rate == 43, "chkbump: cfg rate did not persist");
      check(_cfg.get().flags == 7, "chkbump: cfg flags changed unexpectedly");
      check(_sng.get().version == 2, "chkbump: sng version did not persist");
      check(_sng.get().label == "hello", "chkbump: sng label changed unexpectedly");
   }

   /// Repeated mutations inside one action must collapse into a single stored result.
   [[sysio::action]]
   void multibump() {
      for (int i = 0; i < 5; ++i) {
         _cfg.modify(get_self(), [](cfg& c) { c.rate += 10; });
      }
   }

   /// Confirms multibump stored ONE result rather than five: 43 + 5*10.
   [[sysio::action]]
   void chkmulti() {
      check(_cfg.get().rate == 93, "chkmulti: cfg rate did not persist");
   }

   /// modify_or_create() creates from the default when the row is absent.
   [[sysio::action]]
   void createnew() {
      _cfg.modify_or_create(get_self(), {100, 1}, [](cfg& c) { c.rate += 1; });
   }

   /// Confirms the row createnew created actually persisted.
   [[sysio::action]]
   void chkcreate() {
      check(_cfg.get().rate == 101, "chkcreate: cfg rate did not persist");
      check(_cfg.get().flags == 1, "chkcreate: cfg flags did not persist");
   }

   /// Deferred erase.
   [[sysio::action]]
   void rmall() {
      _cfg.remove();
      _sng.remove();
      check(!_cfg.exists(), "rmall: cfg should read absent immediately");
      check(!_sng.exists(), "rmall: sng should read absent immediately");
   }

private:
   cfg_cached _cfg;
   sng_cached _sng;
};
