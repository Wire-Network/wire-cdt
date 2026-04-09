#include <kv_global_example.hpp>

[[sysio::action]]
void kv_global_example::setconfig(uint64_t max_transfer, uint32_t fee_bps) {
   require_auth(get_self());
   config.set({max_transfer, fee_bps, false}, get_self());
}

[[sysio::action]]
void kv_global_example::pause() {
   require_auth(get_self());
   auto c = config.get("config not set");
   c.paused = true;
   config.set(c, get_self());
}

[[sysio::action]]
void kv_global_example::unpause() {
   require_auth(get_self());
   auto c = config.get("config not set");
   c.paused = false;
   config.set(c, get_self());
}

[[sysio::action]]
void kv_global_example::transfer(name from, name to, uint64_t amount) {
   require_auth(from);
   auto c = config.get_or_default({1000000, 0, false});
   check(!c.paused, "transfers are paused");
   check(amount <= c.max_transfer, "exceeds max transfer limit");
   // ... apply fee_bps, execute transfer ...
   print("Transfer ", amount, " from ", from, " to ", to, " (fee=", c.fee_bps, "bps)\n");
}

[[sysio::action]]
void kv_global_example::getconfig() {
   if (config.exists()) {
      auto c = config.get();
      print("max=", c.max_transfer, " fee=", c.fee_bps, "bps paused=", c.paused, "\n");
   } else {
      print("config not set\n");
   }
}
