#pragma once

#include <sysio/action.h>
#include <sysio/chain.h>
#include <sysio/crypto.h>
#include <sysio/crypto_ext.h>
#include <sysio/crypto_bls_ext.h>
#include <sysio/db.h>
#include <sysio/kv.h>
#include <sysio/permission.h>
#include <sysio/print.h>
#include <sysio/instant_finality.h>
#include <sysio/privileged.h>
#include <sysio/system.h>
#include <sysio/transaction.h>
#include <sysio/types.h>

#include <type_traits>
#include <functional>

namespace sysio { namespace native {
   template <typename... Args, size_t... Is>
   auto get_args_full(std::index_sequence<Is...>) {
       std::tuple<std::decay_t<Args>...> tup;
       return std::tuple<Args...>{std::get<Is>(tup)...};
   }

   template <typename R, typename... Args>
   auto get_args_full(R(Args...)) {
       return get_args_full<Args...>(std::index_sequence_for<Args...>{});
   }

   template <typename R, typename... Args>
   auto get_args(R(Args...)) {
       return std::tuple<std::decay_t<Args>...>{};
   }

   template <typename R, typename Args, size_t... Is>
   auto create_function(std::index_sequence<Is...>) {
      return std::function<R(typename std::tuple_element<Is, Args>::type ...)>{
         [](typename std::tuple_element<Is, Args>::type ...) {
            sysio_assert(false, "unsupported intrinsic"); return (R)0;
         }
      };
   }

#define INTRINSICS(intrinsic_macro) \
intrinsic_macro(get_resource_limits) \
intrinsic_macro(set_resource_limits) \
intrinsic_macro(set_proposed_producers) \
intrinsic_macro(set_proposed_producers_ex) \
intrinsic_macro(get_blockchain_parameters_packed) \
intrinsic_macro(set_blockchain_parameters_packed) \
intrinsic_macro(is_privileged) \
intrinsic_macro(set_privileged) \
intrinsic_macro(is_feature_activated) \
intrinsic_macro(preactivate_feature) \
intrinsic_macro(get_active_producers) \
intrinsic_macro(assert_recover_key) \
intrinsic_macro(recover_key) \
intrinsic_macro(assert_sha256) \
intrinsic_macro(assert_sha1) \
intrinsic_macro(assert_sha512) \
intrinsic_macro(assert_ripemd160) \
intrinsic_macro(sha1) \
intrinsic_macro(sha256) \
intrinsic_macro(sha512) \
intrinsic_macro(ripemd160) \
intrinsic_macro(check_transaction_authorization) \
intrinsic_macro(check_permission_authorization) \
intrinsic_macro(get_permission_lower_bound) \
intrinsic_macro(current_time) \
intrinsic_macro(publication_time) \
intrinsic_macro(read_action_data) \
intrinsic_macro(action_data_size) \
intrinsic_macro(current_receiver) \
intrinsic_macro(require_recipient) \
intrinsic_macro(require_auth) \
intrinsic_macro(require_auth2) \
intrinsic_macro(has_auth) \
intrinsic_macro(is_account) \
intrinsic_macro(prints) \
intrinsic_macro(prints_l) \
intrinsic_macro(printi) \
intrinsic_macro(printui) \
intrinsic_macro(printi128) \
intrinsic_macro(printui128) \
intrinsic_macro(printsf) \
intrinsic_macro(printdf) \
intrinsic_macro(printqf) \
intrinsic_macro(printn) \
intrinsic_macro(printhex) \
intrinsic_macro(read_transaction) \
intrinsic_macro(transaction_size) \
intrinsic_macro(expiration) \
intrinsic_macro(tapos_block_prefix) \
intrinsic_macro(tapos_block_num) \
intrinsic_macro(get_action) \
intrinsic_macro(send_inline) \
intrinsic_macro(send_context_free_inline) \
intrinsic_macro(get_context_free_data) \
intrinsic_macro(get_sender) \
intrinsic_macro(set_action_return_value) \
intrinsic_macro(blake2_f) \
intrinsic_macro(blake2b_256) \
intrinsic_macro(sha3) \
intrinsic_macro(k1_recover) \
intrinsic_macro(alt_bn128_add) \
intrinsic_macro(alt_bn128_mul) \
intrinsic_macro(alt_bn128_pair) \
intrinsic_macro(mod_exp) \
intrinsic_macro(bls_g1_add) \
intrinsic_macro(bls_g2_add) \
intrinsic_macro(bls_g1_weighted_sum) \
intrinsic_macro(bls_g2_weighted_sum) \
intrinsic_macro(bls_pairing) \
intrinsic_macro(bls_g1_map) \
intrinsic_macro(bls_g2_map) \
intrinsic_macro(bls_fp_mod) \
intrinsic_macro(bls_fp_mul) \
intrinsic_macro(bls_fp_exp) \
intrinsic_macro(set_finalizers) \
intrinsic_macro(get_ram_usage) \
intrinsic_macro(kv_set) \
intrinsic_macro(kv_get) \
intrinsic_macro(kv_erase) \
intrinsic_macro(kv_contains) \
intrinsic_macro(kv_it_create) \
intrinsic_macro(kv_it_destroy) \
intrinsic_macro(kv_it_status) \
intrinsic_macro(kv_it_next) \
intrinsic_macro(kv_it_prev) \
intrinsic_macro(kv_it_lower_bound) \
intrinsic_macro(kv_it_key) \
intrinsic_macro(kv_it_value) \
intrinsic_macro(kv_idx_store) \
intrinsic_macro(kv_idx_remove) \
intrinsic_macro(kv_idx_update) \
intrinsic_macro(kv_idx_find_secondary) \
intrinsic_macro(kv_idx_lower_bound) \
intrinsic_macro(kv_idx_next) \
intrinsic_macro(kv_idx_prev) \
intrinsic_macro(kv_idx_key) \
intrinsic_macro(kv_idx_primary_key) \
intrinsic_macro(kv_idx_destroy)

#define CREATE_ENUM(name) \
   name,

#define GENERATE_TYPE_MAPPING(name) \
   struct __ ## name ## _types { \
      using deduced_full_ts = decltype(sysio::native::get_args_full(::name)); \
      using deduced_ts      = decltype(sysio::native::get_args(::name)); \
      using res_t           = decltype(std::apply(::name, deduced_ts{})); \
      static constexpr auto is = std::make_index_sequence<std::tuple_size<deduced_ts>::value>(); \
   };

#define GET_TYPE(name) \
   decltype(create_function<sysio::native::intrinsics::__ ## name ## _types::res_t, \
         sysio::native::intrinsics::__ ## name ## _types::deduced_full_ts>(sysio::native::intrinsics::__ ## name ## _types::is)),

#define REGISTER_INTRINSIC(name) \
   create_function<sysio::native::intrinsics::__ ## name ## _types::res_t, \
         sysio::native::intrinsics::__ ## name ## _types::deduced_full_ts>(sysio::native::intrinsics::__ ## name ## _types::is),

}} //ns sysio::native
