cmake_minimum_required(VERSION 3.19)
project(cdt_contract_cache_guard LANGUAGES CXX)

find_package(cdt REQUIRED)

add_contract(contract_cache_guard contract_cache_guard contract_cache_guard.cpp)
