/**
 * On a chain: an action's data must hold exactly its arguments, through the generated dispatcher, through
 * SYSIO_DISPATCH and through a hand-written apply() that calls execute_action.
 *
 * Covers plain, `binary_extension` and `ignore<T>` parameters, return values and notifications (not checked).
 */
#include <boost/test/unit_test.hpp>
#include <sysio/testing/tester.hpp>

#include <fc/io/raw.hpp>

#include <contracts.hpp>

#include <string>
#include <vector>

using namespace sysio;
using namespace sysio::testing;
using namespace sysio::chain;

namespace {

/// A tail longer than the dispatcher's 512-byte stack buffer, so it is read through `malloc`.
constexpr size_t large_tail_size = 4096;

/// The message an action fails with when its data holds bytes after the arguments.
constexpr char trailing_bytes_message[] = "action data has bytes left after the arguments";

/// `values` packed one after another, followed by `tail` zero bytes.
template <typename... Ts>
bytes packed(size_t tail, const Ts&... values) {
   bytes data;
   const auto append = [&](const bytes& value) { data.insert(data.end(), value.begin(), value.end()); };
   (append(fc::raw::pack(values)), ...);
   data.insert(data.end(), tail, 0);
   return data;
}

/// Pushes `contract::act` with raw `data`, authorized and signed by `actor`.
transaction_trace_ptr push_raw(tester& t, name contract, name act, bytes data, name actor) {
   signed_transaction trx;
   trx.actions.emplace_back(vector<permission_level>{{actor, config::active_name}}, contract, act, std::move(data));
   t.set_transaction_headers(trx);
   trx.sign(t.get_private_key(actor, "active"), t.get_chain_id());
   return t.push_transaction(trx);
}

/// Checks that `push` fails because the action data holds bytes after the arguments.
template <typename Push>
void check_trailing_bytes_rejected(Push&& push) {
   BOOST_CHECK_EXCEPTION(push(), sysio_assert_message_exception, sysio_assert_message_is(trailing_bytes_message));
}

} // namespace

BOOST_AUTO_TEST_SUITE(action_data_tests)

BOOST_FIXTURE_TEST_CASE(generated_dispatcher_checks_action_data, tester) try {
   const name contract = "test"_n;
   create_accounts({contract});
   produce_block();
   set_code(contract, contracts::action_data_wasm());
   set_abi(contract, contracts::action_data_abi().data());
   produce_block();

   const auto push = [&](name act, bytes data) { return push_raw(*this, contract, act, std::move(data), contract); };

   // Plain parameters.
   push("args"_n, packed(0, "alice"_n, uint64_t{42}));
   check_trailing_bytes_rejected([&] { return push("args"_n, packed(1, "alice"_n, uint64_t{42})); });
   check_trailing_bytes_rejected([&] { return push("args"_n, packed(large_tail_size, "alice"_n, uint64_t{42})); });

   push("noargs"_n, {});
   check_trailing_bytes_rejected([&] { return push("noargs"_n, packed(1)); });

   // A binary_extension is an argument when present; anything after it is left over.
   push("ext"_n, packed(0, "alice"_n));
   push("ext"_n, packed(0, "alice"_n, "bob"_n));
   check_trailing_bytes_rejected([&] { return push("ext"_n, packed(1, "alice"_n, "bob"_n)); });

   // An action with an ignore parameter reads the rest itself.
   const std::vector<char> rest = {'a', 'b', 'c'};
   push("ignored"_n, packed(0, "alice"_n, rest));
   check_trailing_bytes_rejected([&] { return push("ignored"_n, packed(1, "alice"_n, rest)); });
   push("ignoredraw"_n, packed(large_tail_size, "alice"_n));
   push("ignoredref"_n, packed(large_tail_size, "alice"_n));
   push("ignoredalias"_n, packed(large_tail_size, "alice"_n));

   // A return value still reaches the trace.
   const auto trace = push("retval"_n, packed(0, uint64_t{41}));
   BOOST_REQUIRE_EQUAL(trace->action_traces.size(), 1u);
   BOOST_CHECK(trace->action_traces[0].return_value == fc::raw::pack(uint64_t{42}));
   check_trailing_bytes_rejected([&] { return push("retval"_n, packed(1, uint64_t{41})); });
   produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(notification_handlers_are_not_checked, tester) try {
   const name sender = "sender"_n;
   const name receiver = "receiver"_n;
   create_accounts({sender, receiver});
   produce_block();
   for (const name account : {sender, receiver}) {
      set_code(account, contracts::action_data_wasm());
      set_abi(account, contracts::action_data_abi().data());
   }
   produce_block();

   // The receiver's handler reads only `to`; the rest of notify's data is not its to check.
   const auto trace = push_raw(*this, sender, "notify"_n, packed(0, receiver, "alice"_n, uint64_t{42}), sender);
   BOOST_REQUIRE_EQUAL(trace->action_traces.size(), 2u);
   BOOST_CHECK_EQUAL(trace->action_traces[1].receiver, receiver);

   // The sender's own action is still checked.
   check_trailing_bytes_rejected(
      [&] { return push_raw(*this, sender, "notify"_n, packed(1, receiver, "alice"_n, uint64_t{42}), sender); });
   produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(sysio_dispatch_checks_action_data, tester) try {
   const name contract = "test"_n;
   create_accounts({contract});
   produce_block();
   set_code(contract, contracts::action_data_macro_wasm());
   set_abi(contract, contracts::action_data_macro_abi().data());
   produce_block();

   const auto push = [&](name act, bytes data) { return push_raw(*this, contract, act, std::move(data), contract); };

   push("args"_n, packed(0, "alice"_n, uint64_t{42}));
   check_trailing_bytes_rejected([&] { return push("args"_n, packed(1, "alice"_n, uint64_t{42})); });
   check_trailing_bytes_rejected([&] { return push("args"_n, packed(large_tail_size, "alice"_n, uint64_t{42})); });
   push("noargs"_n, {});
   check_trailing_bytes_rejected([&] { return push("noargs"_n, packed(1)); });
   produce_block();
} FC_LOG_AND_RETHROW()

BOOST_FIXTURE_TEST_CASE(execute_action_checks_own_actions_only, tester) try {
   const name sender = "sender"_n;
   const name receiver = "receiver"_n;
   create_accounts({sender, receiver});
   produce_block();
   set_code(sender, contracts::action_data_wasm());
   set_abi(sender, contracts::action_data_abi().data());
   set_code(receiver, contracts::action_data_route_wasm());
   set_abi(receiver, contracts::action_data_route_abi().data());
   produce_block();

   // The receiver's apply() routes the notification to a handler that reads only `to`.
   const auto trace = push_raw(*this, sender, "notify"_n, packed(0, receiver, "alice"_n, uint64_t{42}), sender);
   BOOST_REQUIRE_EQUAL(trace->action_traces.size(), 2u);
   BOOST_CHECK_EQUAL(trace->action_traces[1].receiver, receiver);

   // The receiver's own action, routed the same way, is checked.
   push_raw(*this, receiver, "args"_n, packed(0, "alice"_n, uint64_t{42}), receiver);
   check_trailing_bytes_rejected(
      [&] { return push_raw(*this, receiver, "args"_n, packed(1, "alice"_n, uint64_t{42}), receiver); });
   produce_block();
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
