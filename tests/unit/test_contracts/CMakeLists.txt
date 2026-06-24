add_contract(action_results_test action_results_test action_results_test.cpp)
add_contract(malloc_tests malloc_tests malloc_tests.cpp)
add_contract(malloc_tests old_malloc_tests malloc_tests.cpp)
add_contract(simple_tests simple_tests simple_tests.cpp)
add_contract(array_tests array_tests array_tests.cpp)
add_contract(explicit_nested_tests explicit_nested_tests explicit_nested_tests.cpp)
add_contract(transfer_contract transfer_contract transfer.cpp)
add_contract(minimal_tests minimal_tests minimal_tests.cpp)
add_contract(crypto_primitives_tests crypto_primitives_tests crypto_primitives_tests.cpp)
add_contract(bls_primitives_tests bls_primitives_tests bls_primitives_tests.cpp)
add_contract(instant_finality_tests instant_finality_tests instant_finality_tests.cpp)
add_contract(get_code_hash_tests get_code_hash_write get_code_hash_write.cpp)
add_contract(get_code_hash_tests get_code_hash_read get_code_hash_read.cpp)
add_contract(name_pk_tests name_pk_tests name_pk_tests.cpp)
add_contract(test_multi_index test_multi_index multi_index_tests.cpp)
add_contract(test_multi_index2 test_multi_index2 multi_index_tests2.cpp)
add_contract(mi_scope_tests mi_scope_tests multi_index_scope_tests.cpp)
add_contract(kv_indexed_table_tests kv_indexed_table_tests kv_indexed_table_tests.cpp)
add_contract(kv_singleton_tests kv_singleton_tests kv_singleton_tests.cpp)
add_contract(kv_global_tests kv_global_tests kv_global_tests.cpp)
add_contract(kv_scoped_table_tests kv_scoped_table_tests kv_scoped_table_tests.cpp)
add_contract(hash_id_tests hash_id_tests hash_id_tests.cpp)
add_contract(capi_tests capi_tests capi/capi.c capi/action.c capi/chain.c capi/crypto.c capi/db.c capi/permission.c
                                   capi/print.c capi/privileged.c capi/system.c capi/transaction.c)

# Regression contract for cdt-ld's link-time finalize across multiple source SUBDIRECTORIES:
# the two action handlers live in multidir/part_a/ and multidir/part_b/, so their objects (and the
# per-object finalize sidecars) land in different directories. The dispatcher path the sidecars
# record must be target-level, or cdt-ld rejects the disagreement and the link below fails outright.
# multidir_contract_tests.sh additionally checks both actions made it into the merged ABI.
add_contract(multidir_contract multidir_contract multidir/part_a/multidir_a.cpp multidir/part_b/multidir_b.cpp)

configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/simple_wrong.abi ${CMAKE_CURRENT_BINARY_DIR}/simple_wrong.abi COPYONLY )
configure_file( ${CMAKE_CURRENT_SOURCE_DIR}/capi/capi_tests.abi ${CMAKE_CURRENT_BINARY_DIR}/capi_tests.abi COPYONLY )

target_link_libraries(old_malloc_tests PUBLIC --use-freeing-malloc)

# These tests exercise long double (multi-index secondary keys, malloc alignment).
# After the cdt.imports prune, contracts that use long double need --use-rt so that
# libsf.a (softfloat) gets linked locally to satisfy the float128 compiler builtins.
target_link_libraries(malloc_tests PUBLIC --use-rt)
target_link_libraries(old_malloc_tests PUBLIC --use-rt)
target_link_libraries(test_multi_index PUBLIC --use-rt)
target_link_libraries(test_multi_index2 PUBLIC --use-rt)

add_contract(permission_tests permission_tests permission_tests.cpp)

# Protobuf test contract
add_library(pb_protos INTERFACE)
target_add_protobuf(pb_protos OUTPUT_DIRECTORY test FILES test.proto)
add_contract(pb_tests pb_tests pb_tests.cpp)
contract_use_protobuf(pb_tests pb_protos)
