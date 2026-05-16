cmake_minimum_required(VERSION 3.16)
project(multi_index_large)

find_package(cdt)

add_contract( multi_index_large multi_index_large multi_index_large.cpp )
# main_record::f128 is `long double`; after PR #51 softfloat is opt-in, so the
# contract must link --use-rt to resolve the __*tf3 / float128 builtins.
target_link_libraries( multi_index_large PUBLIC --use-rt )
target_include_directories( multi_index_large PUBLIC ${CMAKE_SOURCE_DIR}/../include )
target_ricardian_directory( multi_index_large ${CMAKE_SOURCE_DIR}/../ricardian )
