#include <boost/test/unit_test.hpp>
#include <contracts.hpp>
#include <fc/crypto/elliptic_ed.hpp>
#include <fc/crypto/hex.hpp>
#include <sysio/testing/tester.hpp>

#include <array>
#include <limits>
#include <tuple>

using namespace sysio::chain;
using namespace sysio::testing;

namespace {
constexpr auto contract_account = "edverify"_n;
constexpr auto verify_action = "verify"_n;
constexpr size_t length_size = sizeof(uint32_t);
constexpr size_t signature_size = crypto_sign_BYTES;
constexpr size_t key_size = crypto_sign_PUBLICKEYBYTES;
constexpr size_t header_size = length_size + signature_size + key_size;
constexpr uint32_t maximum_message_size = 65'536;
constexpr unsigned bits_per_byte = 8;
constexpr uint32_t byte_mask = 0xff;
static_assert(header_size == 100);

/// Encode the host's fixed-width length; sysio::pack(vector) uses a different format.
void set_length(bytes& entry, uint32_t length) {
   for (size_t i = 0; i < length_size; ++i)
      entry[i] = static_cast<char>((length >> (i * bits_per_byte)) & byte_mask);
}

/// RFC 8032 section 7.1 test 1 verifies a signed empty message without a generated signature.
bytes known_record() {
   constexpr auto signature = "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555f"
                              "b8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b";
   constexpr auto public_key = "d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a";
   auto decoded = fc::from_hex(std::string(signature) + public_key);
   bytes entry(length_size, 0);
   entry.insert(entry.end(), decoded.begin(), decoded.end());
   return entry;
}

/// Generate valid signatures over arbitrary bytes so message-size tests isolate the host limit.
bytes signed_record(const bytes& message) {
   BOOST_REQUIRE_GE(sodium_init(), 0);
   std::array<unsigned char, crypto_sign_SEEDBYTES> seed{};
   std::array<unsigned char, crypto_sign_SECRETKEYBYTES> secret{};
   std::array<unsigned char, key_size> key{};
   std::array<unsigned char, signature_size> signature{};
   BOOST_REQUIRE_EQUAL(crypto_sign_seed_keypair(key.data(), secret.data(), seed.data()), 0);
   BOOST_REQUIRE_EQUAL(crypto_sign_detached(signature.data(), nullptr,
      reinterpret_cast<const unsigned char*>(message.data()), message.size(), secret.data()), 0);
   bytes entry(length_size);
   set_length(entry, message.size());
   entry.insert(entry.end(), signature.begin(), signature.end());
   entry.insert(entry.end(), key.begin(), key.end());
   entry.insert(entry.end(), message.begin(), message.end());
   return entry;
}

/// Deploy the CDT-built fixture and exercise both public language interfaces in real transactions.
struct ed25519_tester : tester {
   ed25519_tester() {
      create_accounts({contract_account});
      set_code(contract_account, contracts::ed25519_verify_wasm());
      const auto abi = contracts::ed25519_verify_abi();
      set_abi(contract_account, std::string(abi.begin(), abi.end()));
      produce_block();
   }

   /// A false verification result must be observable without aborting the calling action.
   void verify(const bytes& entries, uint32_t count, bool expected) {
      for (const bool use_c : {false, true}) {
         signed_transaction trx;
         trx.actions.emplace_back(vector<permission_level>{{contract_account, config::active_name}},
            contract_account, verify_action, fc::raw::pack(std::make_tuple(entries, count, expected, use_c)));
         set_transaction_headers(trx);
         trx.sign(get_private_key(contract_account, "active"), control->get_chain_id());
         BOOST_REQUIRE(push_transaction(trx)->receipt);
         produce_block();
      }
   }
};
} // namespace

BOOST_AUTO_TEST_SUITE(ed25519_verify_tests)

/// Known-answer and binary messages pass through both the installed C and C++ headers unchanged.
BOOST_FIXTURE_TEST_CASE(raw_messages, ed25519_tester) {
   verify(known_record(), 1, true);
   const bytes binary{'a', '\0', static_cast<char>(byte_mask)};
   const auto good = signed_record(binary);
   verify(good, 1, true);
   for (const size_t offset : {length_size, length_size + signature_size, header_size}) {
      auto bad = good;
      bad[offset] ^= 1;
      verify(bad, 1, false);
   }
}

/// One call handles one signature or a batch, including repeated keys and failures at every position.
BOOST_FIXTURE_TEST_CASE(batches, ed25519_tester) {
   const auto entry = known_record();
   for (const uint32_t count : {1u, 24u, 68u}) {
      bytes batch;
      for (uint32_t i = 0; i < count; ++i)
         batch.insert(batch.end(), entry.begin(), entry.end());
      verify(batch, count, true);
      for (const uint32_t index : {0u, count / 2, count - 1}) {
         auto bad = batch;
         bad[index * entry.size() + length_size] ^= 1;
         verify(bad, count, false);
      }
   }
}

/// Malformed buffers and impossible counts reach the host and return false through either API.
BOOST_FIXTURE_TEST_CASE(malformed, ed25519_tester) {
   verify({}, 0, false);
   verify({}, 1, false);
   auto entry = known_record();
   verify(entry, 0, false);
   verify(entry, 2, false);
   verify(entry, std::numeric_limits<uint32_t>::max(), false);
   entry.pop_back();
   verify(entry, 1, false);
   entry = known_record();
   entry.push_back(0);
   verify(entry, 1, false);
   set_length(entry, std::numeric_limits<uint32_t>::max());
   verify(entry, 1, false);
}

/// Pin both sides of the consensus message limit using correctly signed messages.
BOOST_FIXTURE_TEST_CASE(message_size_limit, ed25519_tester) {
   for (const auto length : {maximum_message_size - 1, maximum_message_size, maximum_message_size + 1}) {
      bytes message(length, '\0');
      message.back() = 1;
      verify(signed_record(message), 1, length <= maximum_message_size);
   }
}

BOOST_AUTO_TEST_SUITE_END()
