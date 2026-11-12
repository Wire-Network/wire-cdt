/**
 * On a chain: an action's data must hold exactly its arguments, and an action name the contract does not define
 * fails, through the generated dispatcher and through SYSIO_DISPATCH. A hand-written apply() that calls
 * execute_action checks the data of the contract's own actions only.
 *
 * Covers plain, `binary_extension` and `ignore<T>` parameters, return values, notifications (not checked), a
 * contract with no actions, and a contract deployed on `sysio`, where the chain's own actions still pass.
 */
#include <boost/test/unit_test.hpp>
#include <sysio/testing/tester.hpp>
#include <sysio/chain/contract_types.hpp>

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

/// The error code an action fails with when the contract does not define its name.
constexpr uint64_t undefined_action_error_code = 1;

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

/// Checks that `push` fails because the contract does not define the action.
template <typename Push>
void check_undefined_action(Push&& push) {
   BOOST_CHECK_EXCEPTION(push(), sysio_assert_code_exception, sysio_assert_code_is(undefined_action_error_code));
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

   check_undefined_action([&] { return push("nosuchact"_n, {}); });
   check_undefined_action([&] { return push("onblock"_n, {}); });
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

BOOST_FIXTURE_TEST_CASE(contract_without_actions_rejects_direct_actions, tester) try {
   const name listener = "listener"_n;
   create_accounts({listener});
   produce_block();
   set_code(listener, contracts::action_data_notify_only_wasm());
   produce_block();

   check_undefined_action([&] { return push_raw(*this, listener, "notify"_n, packed(0, listener), listener); });
   check_undefined_action([&] { return push_raw(*this, listener, "anything"_n, {}, listener); });
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
   check_undefined_action([&] { return push("nosuchact"_n, {}); });
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

BOOST_FIXTURE_TEST_CASE(contract_on_sysio_rejects_undefined_actions, tester) try {
   set_code(config::system_account_name, contracts::action_data_wasm());
   set_abi(config::system_account_name, contracts::action_data_abi().data());
   produce_block();

   const auto push = [&](name act, bytes data) {
      return push_raw(*this, config::system_account_name, act, std::move(data), config::system_account_name);
   };

   check_undefined_action([&] { return push("nosuchact"_n, {}); });
   check_undefined_action([&] { return push("nosuchact"_n, packed(large_tail_size)); });
   push("args"_n, packed(0, "alice"_n, uint64_t{42}));
   check_trailing_bytes_rejected([&] { return push("args"_n, packed(1, "alice"_n, uint64_t{42})); });

   // The chain's own actions need no handler in the contract: onblock runs in every block, and native actions run.
   bool onblock_ran = false;
   {
      boost::signals2::scoped_connection conn = control->applied_transaction().connect(
         [&](std::tuple<const transaction_trace_ptr&, const packed_transaction_ptr&> x) {
            const auto& trace = std::get<0>(x);
            if (is_onblock(*trace)) {
               BOOST_CHECK_MESSAGE(!trace->except, (trace->except ? trace->except->to_detail_string() : std::string{}));
               onblock_ran = true;
            }
         });
      produce_block();
   }
   BOOST_CHECK(onblock_ran);

   const name account = "newone"_n;
   const authority key{get_public_key(account, "active")};
   push("newaccount"_n, fc::raw::pack(newaccount{.creator = config::system_account_name, .name = account,
                                                .owner = key, .active = key}));
   produce_block();
   BOOST_CHECK(control->find_account(account) != nullptr);
} FC_LOG_AND_RETHROW()

BOOST_AUTO_TEST_SUITE_END()
