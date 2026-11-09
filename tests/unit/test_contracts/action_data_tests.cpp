/**
 * Actions covering each shape the generated dispatcher treats differently, for
 * tests/integration/action_data_tests.cpp.
 */
#include <sysio/sysio.hpp>
#include <sysio/binary_extension.hpp>

#include <vector>

using namespace sysio;

/// An alias of an `ignore` parameter type; the dispatcher treats it as `ignore` too.
using ignored_bytes = ignore<std::vector<char>>;

class [[sysio::contract]] action_data_tests : public contract {
public:
   using contract::contract;

   [[sysio::action]] void args(name nm, uint64_t value) {
      check(nm == "alice"_n && value == 42, "unexpected arguments");
   }

   [[sysio::action]] void noargs() {}

   [[sysio::action]] void ext(name nm, binary_extension<name> extra) {
      check(!extra || extra.value() == "bob"_n, "unexpected extension");
   }

   /// Reads its `ignore` parameter itself, then checks the data was read to the end.
   [[sysio::action]] void ignored(name nm, ignore<std::vector<char>> rest) {
      auto& ds = get_datastream();
      std::vector<char> bytes;
      ds >> bytes;
      check_action_data_consumed(ds);
   }

   /// Declares an `ignore` parameter and never reads it.
   [[sysio::action]] void ignoredraw(name nm, ignore<std::vector<char>> rest) {}

   /// The same, taking the `ignore` parameter by const reference.
   [[sysio::action]] void ignoredref(const name& nm, const ignore<std::vector<char>>& rest) {}

   /// The same, naming the `ignore` parameter's type through an alias.
   [[sysio::action]] void ignoredalias(name nm, ignored_bytes rest) {}

   [[sysio::action]] uint64_t retval(uint64_t value) { return value + 1; }

   /// Notifies `to`, whose handler reads only the first argument.
   [[sysio::action]] void notify(name to, name nm, uint64_t value) { require_recipient(to); }

   [[sysio::on_notify("*::notify")]] void on_notify(name to) {
      check(to == get_self(), "notified the wrong account");
   }
};
