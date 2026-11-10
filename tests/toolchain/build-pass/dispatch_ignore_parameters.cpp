// Builds the generated action wrappers for each parameter shape the dispatcher treats differently.
//
// The wrapper of an action with plain or `binary_extension` parameters fails the action when its data holds
// bytes after the arguments. The wrapper of an action with `ignore<T>` parameters, taken by value, by
// const reference or through an alias, does not: those bytes stay in the datastream for the action to read,
// and the action checks them itself with sysio::check_action_data_consumed.
// tests/integration/action_data_tests.cpp checks the behavior on a chain; this case checks that every shape
// still builds.
#include <sysio/sysio.hpp>
#include <sysio/binary_extension.hpp>

#include <string>
#include <vector>

using namespace sysio;

using ignored_bytes = ignore<std::vector<char>>;

class [[sysio::contract]] dispatch_ignore_parameters : public contract {
public:
   using contract::contract;

   [[sysio::action]] void plain(name nm, uint64_t value) {}

   [[sysio::action]] void noargs() {}

   [[sysio::action]] void ext(name nm, binary_extension<name> extra) {}

   [[sysio::action]] void ignval(name nm, ignore<std::vector<char>> rest) {
      auto& ds = get_datastream();
      std::vector<char> bytes;
      ds >> bytes;
      check_action_data_consumed(ds);
   }

   [[sysio::action]] void ignref(const name& nm, const ignore<std::string>& first, const ignore<std::string>& second) {}

   [[sysio::action]] void ignalias(name nm, ignored_bytes rest) {}

   [[sysio::on_notify("*::plain")]] void on_plain(name nm) {}
};
