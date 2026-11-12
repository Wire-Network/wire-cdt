/**
 * Tests for `<sysio/dispatcher.hpp>`: `execute_action` and `SYSIO_DISPATCH`, and the helpers the generated and
 * native-module dispatchers call, `check_action_data_consumed`, `reject_undefined_action` and `is_chain_action`.
 * `execute_action` checks a contract's own actions, not the notifications a hand-written apply() routes through it.
 *
 * The action data is served by mocked `action_data_size` / `read_action_data` intrinsics, so each case packs
 * exactly the bytes it wants the dispatcher to see.
 */
#include <sysio/sysio.hpp>
#include <sysio/binary_extension.hpp>
#include <sysio/tester.hpp>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

using namespace sysio;
using namespace sysio::native;

namespace {
   /// What the mocked intrinsics serve as the current action's data.
   std::vector<char> action_data;

   /// What the handlers below saw on their last call.
   struct observed_t {
      name                    nm;
      uint64_t                value = 0;
      bool                    called = false;
      std::optional<name>     extra;
      std::vector<char>       rest;
   } observed;

   /// The message the dispatcher fails with when action data holds bytes after the arguments.
   constexpr char trailing_bytes_message[] = "action data has bytes left after the arguments";

   /// The error code (as the native `sysio_assert_code` prints it) for an undefined action.
   constexpr char undefined_action_message[] = "1";

   constexpr name account        = "test"_n;
   constexpr name system_account = "sysio"_n;

   /// The account whose action `account` is notified of.
   constexpr name sender = "sender"_n;

   /// A tail longer than the dispatcher's 512-byte stack buffer, so it is read through `malloc`.
   constexpr size_t large_tail_size = 100'000;

   /// Packs `args` into the action data the next dispatch reads, then appends `tail` bytes.
   template <typename... Ts>
   void set_action_data(size_t tail, const Ts&... args) {
      action_data.clear();
      if constexpr (sizeof...(Ts) > 0) {
         action_data = pack(std::make_tuple(args...));
      }
      action_data.insert(action_data.end(), tail, '\x7f');
   }

   void dispatch(name receiver, name action) {
      apply(receiver.value, receiver.value, action.value);
   }

   void reset() {
      observed = observed_t{};
   }
}

/// A contract whose actions cover each shape the dispatcher distinguishes.
class dispatcher_test_contract : public contract {
public:
   using contract::contract;

   void args(name nm, uint64_t value) {
      observed.nm = nm;
      observed.value = value;
      observed.called = true;
   }

   void noargs() { observed.called = true; }

   void ext(name nm, binary_extension<name> extra) {
      observed.nm = nm;
      if (extra)
         observed.extra = extra.value();
      observed.called = true;
   }

   /// Reads its `ignore` parameter itself, then checks the data was read to the end.
   void ignored(name nm, ignore<std::vector<char>>) {
      auto& ds = get_datastream();
      ds >> observed.rest;
      check_action_data_consumed(ds);
      observed.nm = nm;
      observed.called = true;
   }

   /// Declares an `ignore` parameter and never reads it.
   void ignoredraw(name nm, ignore<std::vector<char>>) {
      observed.nm = nm;
      observed.called = true;
   }

   /// A notification handler that declares only the first argument of the action it is notified of.
   void notified(name nm) {
      observed.nm = nm;
      observed.called = true;
   }
};

SYSIO_DISPATCH(dispatcher_test_contract, (args)(noargs)(ext)(ignored)(ignoredraw))

// The dispatcher skips its check only when an argument is an `ignore<T>`.
static_assert(is_ignore_v<ignore<int>>);
static_assert(is_ignore_v<ignore<std::vector<char>>>);
static_assert(!is_ignore_v<int>);
static_assert(!is_ignore_v<binary_extension<name>>);
static_assert(!is_ignore_v<ignore_wrapper<int>>);

// Only the chain's own actions, and only on `sysio`.
static_assert(is_chain_action("sysio"_n, "newaccount"_n));
static_assert(is_chain_action("sysio"_n, "setcode"_n));
static_assert(is_chain_action("sysio"_n, "setabi"_n));
static_assert(is_chain_action("sysio"_n, "updateauth"_n));
static_assert(is_chain_action("sysio"_n, "deleteauth"_n));
static_assert(is_chain_action("sysio"_n, "linkauth"_n));
static_assert(is_chain_action("sysio"_n, "unlinkauth"_n));
static_assert(is_chain_action("sysio"_n, "onblock"_n));
static_assert(!is_chain_action("sysio"_n, "transfer"_n));
static_assert(!is_chain_action("sysio"_n, "onerror"_n));
static_assert(!is_chain_action("sysio"_n, name{}));
static_assert(!is_chain_action("sysio.token"_n, "onblock"_n));
static_assert(!is_chain_action("test"_n, "setcode"_n));
static_assert(!is_chain_action(name{}, "onblock"_n));

SYSIO_TEST_BEGIN(check_action_data_consumed_test)
   const char buf[4] = {1, 2, 3, 4};

   datastream<const char*> empty(buf, 0);
   check_action_data_consumed(empty);

   datastream<const char*> read_to_end(buf, sizeof(buf));
   read_to_end.skip(sizeof(buf));
   check_action_data_consumed(read_to_end);

   CHECK_ASSERT(trailing_bytes_message, [&]() {
      datastream<const char*> one_left(buf, sizeof(buf));
      one_left.skip(sizeof(buf) - 1);
      check_action_data_consumed(one_left);
   });

   CHECK_ASSERT(trailing_bytes_message, [&]() {
      datastream<const char*> unread(buf, sizeof(buf));
      check_action_data_consumed(unread);
   });
SYSIO_TEST_END

SYSIO_TEST_BEGIN(reject_undefined_action_test)
   // Returns for the chain's own actions on `sysio`.
   for (const name action : {"newaccount"_n, "setcode"_n, "setabi"_n, "updateauth"_n, "deleteauth"_n, "linkauth"_n,
                             "unlinkauth"_n, "onblock"_n}) {
      reject_undefined_action(system_account, action);
   }

   // Fails everything else, on `sysio` and elsewhere.
   CHECK_ASSERT(undefined_action_message, []() { reject_undefined_action(system_account, "nosuchact"_n); });
   CHECK_ASSERT(undefined_action_message, []() { reject_undefined_action(system_account, "transfer"_n); });
   CHECK_ASSERT(undefined_action_message, []() { reject_undefined_action(account, "nosuchact"_n); });
   CHECK_ASSERT(undefined_action_message, []() { reject_undefined_action(account, "onblock"_n); });
   CHECK_ASSERT(undefined_action_message, []() { reject_undefined_action("sysio.token"_n, "setcode"_n); });
SYSIO_TEST_END

SYSIO_TEST_BEGIN(dispatch_exact_arguments_test)
   reset();
   set_action_data(0, "alice"_n, uint64_t{42});
   dispatch(account, "args"_n);
   CHECK_EQUAL(observed.called, true);
   CHECK_EQUAL(observed.nm, "alice"_n);
   CHECK_EQUAL(observed.value, uint64_t{42});

   reset();
   set_action_data(0);
   dispatch(account, "noargs"_n);
   CHECK_EQUAL(observed.called, true);
SYSIO_TEST_END

SYSIO_TEST_BEGIN(dispatch_trailing_bytes_test)
   for (const size_t tail : {size_t{1}, size_t{2}, large_tail_size}) {
      reset();
      set_action_data(tail, "alice"_n, uint64_t{42});
      CHECK_ASSERT(trailing_bytes_message, []() { dispatch(account, "args"_n); });
      CHECK_EQUAL(observed.called, false);

      reset();
      set_action_data(tail);
      CHECK_ASSERT(trailing_bytes_message, []() { dispatch(account, "noargs"_n); });
      CHECK_EQUAL(observed.called, false);
   }
SYSIO_TEST_END

SYSIO_TEST_BEGIN(dispatch_binary_extension_test)
   // Without the extension.
   reset();
   set_action_data(0, "alice"_n);
   dispatch(account, "ext"_n);
   CHECK_EQUAL(observed.called, true);
   CHECK_EQUAL(observed.extra.has_value(), false);

   // With the extension: its bytes are arguments, not leftovers.
   reset();
   set_action_data(0, "alice"_n, "bob"_n);
   dispatch(account, "ext"_n);
   CHECK_EQUAL(observed.called, true);
   CHECK_EQUAL(observed.extra.value_or(name{}), "bob"_n);

   // Anything after the extension is left over.
   reset();
   set_action_data(1, "alice"_n, "bob"_n);
   CHECK_ASSERT(trailing_bytes_message, []() { dispatch(account, "ext"_n); });
   CHECK_EQUAL(observed.called, false);
SYSIO_TEST_END

SYSIO_TEST_BEGIN(dispatch_ignore_parameter_test)
   const std::vector<char> rest = {'a', 'b', 'c'};

   // The action reads its `ignore` parameter itself and finishes with check_action_data_consumed.
   reset();
   set_action_data(0, "alice"_n, rest);
   dispatch(account, "ignored"_n);
   CHECK_EQUAL(observed.called, true);
   CHECK_EQUAL(observed.rest == rest, true);

   reset();
   set_action_data(1, "alice"_n, rest);
   CHECK_ASSERT(trailing_bytes_message, []() { dispatch(account, "ignored"_n); });
   CHECK_EQUAL(observed.called, false);

   // The dispatcher leaves an `ignore` parameter's bytes to the action, so it does not check them itself.
   reset();
   set_action_data(large_tail_size, "alice"_n);
   dispatch(account, "ignoredraw"_n);
   CHECK_EQUAL(observed.called, true);
SYSIO_TEST_END

// A hand-written apply() routes notifications through execute_action, with the receiver as `self` and the
// action's account as `code`.
SYSIO_TEST_BEGIN(execute_action_notification_test)
   // A notification: the rest of the sender's action data is not the handler's to check.
   for (const size_t tail : {size_t{0}, size_t{1}, large_tail_size}) {
      reset();
      set_action_data(tail, "alice"_n, "bob"_n, uint64_t{42});
      execute_action(account, sender, &dispatcher_test_contract::notified);
      CHECK_EQUAL(observed.called, true);
      CHECK_EQUAL(observed.nm, "alice"_n);
   }

   // The same handler run as the receiver's own action is checked.
   reset();
   set_action_data(0, "alice"_n, "bob"_n, uint64_t{42});
   CHECK_ASSERT(trailing_bytes_message,
                []() { execute_action(account, account, &dispatcher_test_contract::notified); });
   CHECK_EQUAL(observed.called, false);

   reset();
   set_action_data(0, "alice"_n);
   execute_action(account, account, &dispatcher_test_contract::notified);
   CHECK_EQUAL(observed.called, true);
SYSIO_TEST_END

SYSIO_TEST_BEGIN(dispatch_undefined_action_test)
   reset();
   set_action_data(0);
   CHECK_ASSERT(undefined_action_message, []() { dispatch(account, "nosuchact"_n); });
   CHECK_ASSERT(undefined_action_message, []() { dispatch(account, "onblock"_n); });
   CHECK_ASSERT(undefined_action_message, []() { dispatch(system_account, "nosuchact"_n); });

   // On `sysio`, an action the chain handles itself needs no handler.
   dispatch(system_account, "onblock"_n);
   dispatch(system_account, "newaccount"_n);
   CHECK_EQUAL(observed.called, false);

   // A notification of another contract's action reaches no handler under SYSIO_DISPATCH.
   apply(account.value, "sysio.token"_n.value, "nosuchact"_n.value);
   CHECK_EQUAL(observed.called, false);
SYSIO_TEST_END

int main(int argc, char* argv[]) {
   bool verbose = false;
   if (argc >= 2 && std::strcmp(argv[1], "-v") == 0) {
      verbose = true;
   }
   silence_output(!verbose);

   intrinsics::set_intrinsic<intrinsics::action_data_size>([]() {
      return static_cast<uint32_t>(action_data.size());
   });
   intrinsics::set_intrinsic<intrinsics::read_action_data>([](void* msg, uint32_t len) {
      const uint32_t n = std::min(len, static_cast<uint32_t>(action_data.size()));
      std::memcpy(msg, action_data.data(), n);
      return n;
   });

   SYSIO_TEST(check_action_data_consumed_test);
   SYSIO_TEST(reject_undefined_action_test);
   SYSIO_TEST(dispatch_exact_arguments_test);
   SYSIO_TEST(dispatch_trailing_bytes_test);
   SYSIO_TEST(dispatch_binary_extension_test);
   SYSIO_TEST(dispatch_ignore_parameter_test);
   SYSIO_TEST(execute_action_notification_test);
   SYSIO_TEST(dispatch_undefined_action_test);
   return has_failed();
}
