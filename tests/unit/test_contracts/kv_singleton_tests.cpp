#include <sysio/sysio.hpp>
#include <sysio/singleton.hpp>

using namespace sysio;

class [[sysio::contract("kv_singleton_tests")]] kv_singleton_tests : public contract {
public:
   using contract::contract;

   struct config {
      uint64_t    max_supply;
      std::string symbol;
      SYSLIB_SERIALIZE(config, (max_supply)(symbol))
   };

   using config_singleton = singleton<"config"_n, config>;

   // ── exists / set / get ───────────────────────────────────────────────────

   [[sysio::action]]
   void setget() {
      config_singleton cfg(get_self(), get_self().value);

      check(!cfg.exists(), "should not exist initially");

      cfg.set({1000000, "SYS"}, get_self());

      check(cfg.exists(), "should exist after set");

      auto val = cfg.get();
      check(val.max_supply == 1000000, "max_supply mismatch");
      check(val.symbol == "SYS", "symbol mismatch");
   }

   // ── get_or_default ───────────────────────────────────────────────────────

   [[sysio::action]]
   void getdefault() {
      config_singleton cfg(get_self(), "default"_n.value);

      // Should return default when not set
      auto val = cfg.get_or_default({500, "DEF"});
      check(val.max_supply == 500, "default max_supply mismatch");
      check(val.symbol == "DEF", "default symbol mismatch");

      // After set, should return stored value
      cfg.set({999, "SET"}, get_self());
      val = cfg.get_or_default({500, "DEF"});
      check(val.max_supply == 999, "stored max_supply mismatch");
      check(val.symbol == "SET", "stored symbol mismatch");
   }

   // ── get_or_create ────────────────────────────────────────────────────────

   [[sysio::action]]
   void getorcreate() {
      config_singleton cfg(get_self(), "goc"_n.value);

      // First call: creates with default
      auto val = cfg.get_or_create(get_self(), {777, "NEW"});
      check(val.max_supply == 777, "created max_supply mismatch");

      // Second call: returns existing
      val = cfg.get_or_create(get_self(), {888, "OTHER"});
      check(val.max_supply == 777, "should return existing, not new default");
   }

   // ── remove ───────────────────────────────────────────────────────────────

   [[sysio::action]]
   void removetest() {
      config_singleton cfg(get_self(), "remove"_n.value);

      cfg.set({100, "RM"}, get_self());
      check(cfg.exists(), "should exist before remove");

      cfg.remove();
      check(!cfg.exists(), "should not exist after remove");

      // Double remove should be safe
      cfg.remove();
      check(!cfg.exists(), "still should not exist");
   }

   // ── set overwrites existing value ────────────────────────────────────────

   [[sysio::action]]
   void settwice() {
      config_singleton cfg(get_self(), "twice"_n.value);

      cfg.set({100, "FIRST"}, get_self());
      cfg.set({200, "SECOND"}, get_self());

      auto val = cfg.get();
      check(val.max_supply == 200, "should be overwritten value");
      check(val.symbol == "SECOND", "should be overwritten symbol");
   }

   // ── Different scopes are independent ─────────────────────────────────────

   // Negative: get() on unset singleton should assert (T2)
   [[sysio::action]]
   void getunset() {
      config_singleton cfg(get_self(), "empty"_n.value);
      cfg.get(); // should abort: "singleton does not exist"
   }

   [[sysio::action]]
   void scopetest() {
      config_singleton cfg1(get_self(), "scope1"_n.value);
      config_singleton cfg2(get_self(), "scope2"_n.value);

      cfg1.set({111, "S1"}, get_self());
      cfg2.set({222, "S2"}, get_self());

      check(cfg1.get().max_supply == 111, "scope1 value mismatch");
      check(cfg2.get().max_supply == 222, "scope2 value mismatch");

      // Removing from one scope shouldn't affect the other
      cfg1.remove();
      check(!cfg1.exists(), "scope1 should be removed");
      check(cfg2.exists(), "scope2 should still exist");
      check(cfg2.get().max_supply == 222, "scope2 value should be unchanged");
   }
};
