#include <sysio/sysio.hpp>
#include <sysio/kv_global.hpp>

using namespace sysio;

class [[sysio::contract("kv_global_tests")]] kv_global_tests : public contract {
public:
   using contract::contract;

   // ── Trivially-copyable value (exercises is_fixed_serializable fast path) ──

   struct pod_cfg {
      uint64_t rate;
      uint64_t flags;
      SYSLIB_SERIALIZE(pod_cfg, (rate)(flags))
   };
   using pod_global = kv::global<"podcfg"_n, pod_cfg>;

   // ── Non-trivially-copyable value (string field, exercises datastream path) ──

   struct str_cfg {
      uint64_t    version;
      std::string label;
      SYSLIB_SERIALIZE(str_cfg, (version)(label))
   };
   using str_global = kv::global<"strcfg"_n, str_cfg>;

   // ── POD: set / get / exists / remove ─────────────────────────────────────

   [[sysio::action]]
   void podsetget() {
      pod_global g(get_self());

      check(!g.exists(), "podsetget: should not exist initially");

      g.set({42, 0xFF}, get_self());
      check(g.exists(), "podsetget: should exist after set");

      auto val = g.get();
      check(val.rate == 42, "podsetget: rate");
      check(val.flags == 0xFF, "podsetget: flags");

      // Overwrite
      g.set({99, 0x01}, get_self());
      val = g.get();
      check(val.rate == 99, "podsetget: overwrite rate");
      check(val.flags == 0x01, "podsetget: overwrite flags");

      // Remove
      g.remove();
      check(!g.exists(), "podsetget: should not exist after remove");

      // Double remove is safe
      g.remove();
   }

   // ── String: set / get / exists / remove ──────────────────────────────────

   [[sysio::action]]
   void strsetget() {
      str_global g(get_self());

      check(!g.exists(), "strsetget: should not exist");

      g.set({1, "hello"}, get_self());
      check(g.exists(), "strsetget: should exist");

      auto val = g.get();
      check(val.version == 1, "strsetget: version");
      check(val.label == "hello", "strsetget: label");

      // Overwrite with different-length string
      g.set({2, "a much longer label value"}, get_self());
      val = g.get();
      check(val.version == 2, "strsetget: overwrite version");
      check(val.label == "a much longer label value", "strsetget: overwrite label");

      g.remove();
      check(!g.exists(), "strsetget: removed");
   }

   // ── get_or_default ───────────────────────────────────────────────────────

   [[sysio::action]]
   void getdefault() {
      pod_global g(get_self());

      // Not set — should return default
      auto val = g.get_or_default({7, 0});
      check(val.rate == 7, "getdefault: default rate");

      // After set — should return stored
      g.set({100, 0}, get_self());
      val = g.get_or_default({7, 0});
      check(val.rate == 100, "getdefault: stored rate");

      g.remove();
   }

   // ── get_or_create ────────────────────────────────────────────────────────

   [[sysio::action]]
   void getorcreate() {
      // Use a different name to avoid collisions with other tests
      kv::global<"goc"_n, pod_cfg> g(get_self());

      // First call: creates
      auto val = g.get_or_create(get_self(), {55, 0x10});
      check(val.rate == 55, "getorcreate: created rate");

      // Second call: returns existing
      val = g.get_or_create(get_self(), {99, 0x20});
      check(val.rate == 55, "getorcreate: existing rate unchanged");

      g.remove();
   }

   // ── get on unset should assert ───────────────────────────────────────────

   [[sysio::action]]
   void getunset() {
      kv::global<"empty"_n, pod_cfg> g(get_self());
      g.get(); // should abort: "global does not exist"
   }

   // ── Two globals with different names are independent ─────────────────────

   [[sysio::action]]
   void twonames() {
      kv::global<"cfg1"_n, pod_cfg> g1(get_self());
      kv::global<"cfg2"_n, pod_cfg> g2(get_self());

      g1.set({10, 1}, get_self());
      g2.set({20, 2}, get_self());

      check(g1.get().rate == 10, "twonames: g1 rate");
      check(g2.get().rate == 20, "twonames: g2 rate");

      // Remove one, other unaffected
      g1.remove();
      check(!g1.exists(), "twonames: g1 removed");
      check(g2.exists(), "twonames: g2 still exists");
      check(g2.get().rate == 20, "twonames: g2 still 20");

      g2.remove();
   }

   // ── String get_or_default / get_or_create ────────────────────────────────

   [[sysio::action]]
   void strdefault() {
      str_global g(get_self());

      auto val = g.get_or_default({0, "fallback"});
      check(val.label == "fallback", "strdefault: default label");

      kv::global<"strgoc"_n, str_cfg> g2(get_self());
      val = g2.get_or_create(get_self(), {1, "created"});
      check(val.label == "created", "strdefault: created label");

      val = g2.get_or_create(get_self(), {2, "other"});
      check(val.label == "created", "strdefault: existing label");

      g2.remove();
   }
};

static_assert(sysio::kv::is_fixed_serializable_v<kv_global_tests::pod_cfg>,
              "pod_cfg must hit the zero-copy fast path");
