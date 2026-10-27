#include <boost/test/unit_test.hpp>
#include <contracts.hpp>
#include <fc/crypto/elliptic_ed.hpp>
#include <fc/crypto/hex.hpp>
#include <sysio/testing/tester.hpp>
#include <array>
#include <tuple>

using namespace sysio::chain;
using namespace sysio::testing;

/// Match fixture action serialization; the host import has no record format.
struct ed25519_input {
   bytes signature;
   bytes public_key;
   bytes message;
};
FC_REFLECT(ed25519_input, (signature)(public_key)(message))

namespace {
constexpr auto contract_account = "edverify"_n;
constexpr auto verify_action = "verify"_n;
constexpr auto batch_action = "batch"_n;
constexpr int32_t verification_success = 0;
constexpr int32_t verification_failure = -1;
constexpr size_t signature_size = crypto_sign_BYTES;
constexpr size_t key_size = crypto_sign_PUBLICKEYBYTES;
constexpr uint32_t maximum_message_size = 65'536;
constexpr uint32_t byte_mask = 0xff;

/// RFC 8032 section 7.1 test 1, independent of generated signatures.
ed25519_input known_input() {
   constexpr auto signature = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
                              "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
   constexpr auto public_key = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
   const auto sig = fc::from_hex(signature);
   const auto pub = fc::from_hex(public_key);
   return {bytes(sig.begin(), sig.end()), bytes(pub.begin(), pub.end()), {}};
}

/// Generate valid signatures over arbitrary bytes so message-size tests isolate the host limit.
ed25519_input signed_input(const bytes& message) {
   BOOST_REQUIRE_GE(sodium_init(), 0);
   std::array<unsigned char, crypto_sign_SEEDBYTES> seed{};
   std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret{};
   std::array<unsigned char, key_size> key{};
   std::array<unsigned char, signature_size> signature{};
   BOOST_REQUIRE_EQUAL(crypto_sign_seed_keypair(key.data(), secret.data(), seed.data()), 0);
   BOOST_REQUIRE_EQUAL(crypto_sign_detached(signature.data(), nullptr,
      reinterpret_cast<const unsigned char*>(message.data()), message.size(), secret.data()), 0);
   return {bytes(signature.begin(), signature.end()), bytes(key.begin(), key.end()), message};
}

/// Deploy the installed CDT's fixture and execute both language APIs in real transactions.
struct ed25519_tester : tester {
   ed25519_tester() {
      create_accounts({contract_account});
      set_code(contract_account, contracts::ed25519_verify_wasm());
      const auto abi = contracts::ed25519_verify_abi();
      set_abi(contract_account, std::string(abi.begin(), abi.end()));
      produce_block();
   }

   /// Execute an action whose arguments are already ABI serialized.
   void run(name action, const bytes& payload) {
      signed_transaction trx;
      trx.actions.emplace_back(vector<permission_level>{{contract_account, config::active_name}},
                                contract_account, action, payload);
      set_transaction_headers(trx);
      trx.sign(get_private_key(contract_account, "active"), control->get_chain_id());
      BOOST_REQUIRE(push_transaction(trx)->receipt);
      produce_block();
   }

   /// The public span API exposes the host success/failure return code.
   void verify(const ed25519_input& entry, int32_t expected) {
      run(verify_action, fc::raw::pack(std::make_tuple(entry, expected)));
   }

   /// Exercise the public C++ batch helper compiled into WASM.
   void batch(const std::vector<ed25519_input>& entries, int32_t expected) {
      run(batch_action, fc::raw::pack(std::make_tuple(entries, expected)));
   }
};
} // namespace

BOOST_AUTO_TEST_SUITE(ed25519_verify_tests)

/// Published empty-message and binary-message signatures pass through both public language APIs.
BOOST_FIXTURE_TEST_CASE(raw_messages, ed25519_tester) {
   verify(known_input(), verification_success);
   const auto good = signed_input({'a', '\0', static_cast<char>(byte_mask)});
   verify(good, verification_success);
   for (auto member : {&ed25519_input::signature, &ed25519_input::public_key, &ed25519_input::message}) {
      auto bad = good;
      (bad.*member).front() ^= 1;
      verify(bad, verification_failure);
   }
}

/// Independent verification in WASM permits repeated buffers and stops on a failed entry.
BOOST_FIXTURE_TEST_CASE(batches, ed25519_tester) {
   batch({}, verification_failure);
   for (uint32_t count : {1u, 24u, 68u}) {
      const std::vector<ed25519_input> entries(count, known_input());
      batch(entries, verification_success);
      for (uint32_t index : {0u, count / 2, count - 1}) {
         auto bad = entries;
         bad[index].signature.front() ^= 1;
         batch(bad, verification_failure);
      }
   }
}

/// Size checks occur in the host and return -1 through either language API.
BOOST_FIXTURE_TEST_CASE(invalid_lengths, ed25519_tester) {
   for (auto member : {&ed25519_input::signature, &ed25519_input::public_key}) {
      const auto good = known_input();
      for (size_t size : {size_t{0}, (good.*member).size() - 1, (good.*member).size() + 1}) {
         auto bad = good;
         (bad.*member).resize(size);
         verify(bad, verification_failure);
      }
   }
}

/// Correctly signed messages isolate both sides of the host's consensus size limit.
BOOST_FIXTURE_TEST_CASE(message_size_limit, ed25519_tester) {
   for (auto size : {maximum_message_size - 1, maximum_message_size, maximum_message_size + 1}) {
      bytes message(size, '\0');
      message.back() = 1;
      const auto entry = signed_input(message);
      const auto expected = size <= maximum_message_size ? verification_success : verification_failure;
      verify(entry, expected);
      batch({known_input(), entry}, expected);
   }
}

BOOST_AUTO_TEST_SUITE_END()
