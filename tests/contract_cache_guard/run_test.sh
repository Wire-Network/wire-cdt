#!/usr/bin/env bash

set -euo pipefail

build_root=$(cd "$1" && pwd)
source_dir=$(cd "$2" && pwd)
test_root="$build_root/tests/contract_cache_guard_work"
project_build="$test_root/project"
cache_dir="$test_root/ccache"

if ! ccache_path=$(command -v ccache); then
  echo "Skipping contract cache guard test: ccache is unavailable"
  exit 77
fi

vcpkg_installed=$(awk -F= \
  '$1 == "VCPKG_INSTALLED_DIR:PATH" { print substr($0, index($0, "=") + 1) }' \
  "$build_root/CMakeCache.txt")
if [[ -z "$vcpkg_installed" ]]; then
  vcpkg_installed="$build_root/vcpkg_installed"
fi
if [[ ! -d "$vcpkg_installed" ]]; then
  echo "Skipping contract cache guard test: vcpkg tree is unavailable"
  exit 77
fi

magic_enum_header=$(find "$vcpkg_installed" \
  -path '*/include/magic_enum/magic_enum.hpp' -print -quit)
if [[ -z "$magic_enum_header" ]]; then
  echo "Skipping contract cache guard test: magic_enum headers are unavailable"
  exit 77
fi
contract_include=${magic_enum_header%/magic_enum/magic_enum.hpp}

cmake -E remove_directory "$test_root"
cmake -E make_directory "$cache_dir"

export CCACHE_DIR="$cache_dir"
export CCACHE_COMPILERCHECK=content
unset CCACHE_DEPEND CCACHE_DISABLE CCACHE_NODIRECT CCACHE_RECACHE CCACHE_READONLY

cache_stat() {
  local stat_name=$1
  "$ccache_path" --print-stats |
    awk -v stat_name="$stat_name" '$1 == stat_name { print $2 }'
}

cache_activity() {
  local misses
  local direct_hits
  local preprocessed_hits
  misses=$(cache_stat cache_miss)
  direct_hits=$(cache_stat direct_cache_hit)
  preprocessed_hits=$(cache_stat preprocessed_cache_hit)
  echo $((misses + direct_hits + preprocessed_hits))
}

configure_project() {
  cmake -S "$source_dir" -B "$project_build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$build_root/lib/cmake/cdt/CDTWasmToolchain.cmake" \
    -Dcdt_DIR="$build_root/lib/cmake/cdt" \
    -DCDT_CONTRACT_INCLUDE_PATH="$contract_include" \
    -DCMAKE_C_COMPILER_LAUNCHER="$ccache_path" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$ccache_path" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

assert_contract_outputs() {
  local wasm="$project_build/contract_cache_guard.wasm"
  local abi="$project_build/contract_cache_guard.abi"
  local finalize
  local descriptor

  if [[ ! -s "$wasm" || ! -s "$abi" ]]; then
    echo "Contract build did not produce non-empty WASM and ABI outputs" >&2
    exit 1
  fi
  if ! grep -q '"name": "ping"' "$abi"; then
    echo "Contract ABI does not contain the ping action" >&2
    exit 1
  fi

  finalize=$(find "$project_build" -name '*.finalize' -print -quit)
  descriptor=$(find "$project_build" -name '*.desc' -print -quit)
  if [[ -z "$finalize" || -z "$descriptor" ]]; then
    echo "Contract build did not regenerate compiler sidecars" >&2
    exit 1
  fi
}

build_contract() {
  local pass_name=$1
  local activity

  configure_project
  "$ccache_path" --zero-stats
  if ! cmake --build "$project_build"; then
    echo "$pass_name same-path contract build failed" >&2
    exit 1
  fi
  assert_contract_outputs
  activity=$(cache_activity)
  if [[ "$activity" != 0 ]]; then
    echo "$pass_name contract build used ccache despite producing sidecars" >&2
    "$ccache_path" --show-stats >&2
    exit 1
  fi
}

build_contract cold
cmake -E remove_directory "$project_build"
build_contract warm
