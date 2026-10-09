cmake_minimum_required(VERSION 3.19)
project(cdt_native_contract_types LANGUAGES CXX)

# Match wire-sysio's language mode; current sysiolib headers use C++20 concepts.
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(cdt REQUIRED)

# Compile the same assertions as the WASM toolchain test, using the public
# native-module macro so its definitions, forced includes, and SDK staging are tested.
add_native_contract(
   TARGET integer_type_aliases_native
   SOURCES ../toolchain/compile-pass/integer_type_aliases.cpp
)

# A forced header must not introduce pedantic errors in consumers. Limit these
# diagnostics to the regression source, leaving unrelated sysiolib sources alone.
set_source_files_properties(../toolchain/compile-pass/integer_type_aliases.cpp
   PROPERTIES COMPILE_OPTIONS "-Wpedantic;-Werror=pedantic")
