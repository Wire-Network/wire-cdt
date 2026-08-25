#!/usr/bin/env bash

set -euo pipefail

build_root=$1
source_dir=$2
version_policy=$3

if [[ ! -x "$version_policy" ]]; then
  echo "Validated ccache version policy is not executable: $version_policy" >&2
  exit 1
fi

if ! ccache_path=$(command -v ccache); then
  echo "Skipping compiler launcher guard test: ccache is unavailable"
  exit 77
fi

ccache_version=$("$ccache_path" --version | awk 'NR == 1 { print $3 }')
if ! "$version_policy" "$ccache_version"; then
  echo "Skipping compiler launcher guard test: ccache $ccache_version is not validated with CDT wrappers"
  exit 77
fi

test_root="$build_root/tests/compiler_launcher_guard_work"
cache_dir="$test_root/ccache"
native_cache_dir="$test_root/ccache-native"
project_build="$test_root/project"
magic_enum_header=$(find "$build_root/vcpkg_installed" \
  -path '*/include/magic_enum/magic_enum.hpp' -print -quit)
if [[ -z "$magic_enum_header" ]]; then
  echo "Could not find the vcpkg magic_enum headers" >&2
  exit 1
fi
contract_include=${magic_enum_header%/magic_enum/magic_enum.hpp}

cmake -E remove_directory "$test_root"
cmake -E make_directory "$cache_dir" "$native_cache_dir"

export CCACHE_DIR="$cache_dir"
export CCACHE_DEPEND=true
unset CMAKE_GENERATOR

configure_contract_project() {
  local output_dir=$1
  cmake -S "$source_dir" -B "$output_dir" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$build_root/lib/cmake/cdt/CDTWasmToolchain.cmake" \
    -Dcdt_DIR="$build_root/lib/cmake/cdt" \
    -DCDT_CONTRACT_INCLUDE_PATH="$contract_include" \
    -DCDT_ALLOW_COMPILER_LAUNCHER=ON \
    -DCDT_NATIVE_CCACHE_DIR="$native_cache_dir" \
    -DCMAKE_C_COMPILER_LAUNCHER="$ccache_path" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$ccache_path" \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
}

cache_stat() {
  local selected_cache=$1
  local stat_name=$2
  CCACHE_DIR="$selected_cache" "$ccache_path" --print-stats |
    awk -v stat_name="$stat_name" '$1 == stat_name { print $2 }'
}

assert_cache_stats() {
  local selected_cache=$1
  local expected_hits=$2
  local expected_misses=$3
  local label=$4
  local direct_hits
  local preprocessed_hits
  local cache_misses

  direct_hits=$(cache_stat "$selected_cache" direct_cache_hit)
  preprocessed_hits=$(cache_stat "$selected_cache" preprocessed_cache_hit)
  cache_misses=$(cache_stat "$selected_cache" cache_miss)
  if [[ -z "$direct_hits" || -z "$preprocessed_hits" || -z "$cache_misses" ]] ||
      (( direct_hits + preprocessed_hits != expected_hits ||
         cache_misses != expected_misses )); then
    echo "Unexpected $label cache statistics" >&2
    CCACHE_DIR="$selected_cache" "$ccache_path" --show-stats >&2
    exit 1
  fi
}

probe_hash() {
  cmake -E sha256sum "$1" | awk '{ print $1 }'
}

assert_project_outputs() {
  local probe_a="$project_build/CMakeFiles/cache_probe_a.dir/cache_probe_a.cpp.obj"
  local probe_b="$project_build/CMakeFiles/cache_probe_b.dir/cache_probe_b.cpp.obj"

  test -f "$probe_a"
  test -f "$probe_b"
  test -f "$project_build/native_probe"
  test -f "$project_build/native_probe_uncached"
  test -f "$project_build/launcher_guard.wasm"
  test -f "$project_build/launcher_guard.abi"
  if cmp -s "$probe_a" "$probe_b"; then
    echo "Distinct Wasm sources collapsed to the same object" >&2
    exit 1
  fi
}

configure_contract_project "$project_build"
cmake --build "$project_build"
assert_project_outputs
assert_cache_stats "$cache_dir" 0 2 "cold Wasm"
assert_cache_stats "$native_cache_dir" 0 1 "cold native"
probe_a_hash=$(probe_hash \
  "$project_build/CMakeFiles/cache_probe_a.dir/cache_probe_a.cpp.obj")
probe_b_hash=$(probe_hash \
  "$project_build/CMakeFiles/cache_probe_b.dir/cache_probe_b.cpp.obj")

"$ccache_path" --zero-stats
CCACHE_DIR="$native_cache_dir" "$ccache_path" --zero-stats
cmake -E remove_directory "$project_build"
configure_contract_project "$project_build"
cmake --build "$project_build"
assert_project_outputs
assert_cache_stats "$cache_dir" 2 0 "warm Wasm"
assert_cache_stats "$native_cache_dir" 1 0 "warm native"

second_probe_a_hash=$(probe_hash \
  "$project_build/CMakeFiles/cache_probe_a.dir/cache_probe_a.cpp.obj")
second_probe_b_hash=$(probe_hash \
  "$project_build/CMakeFiles/cache_probe_b.dir/cache_probe_b.cpp.obj")
if [[ "$probe_a_hash" != "$second_probe_a_hash" ||
      "$probe_b_hash" != "$second_probe_b_hash" ]]; then
  echo "Warm cache hits changed the Wasm probe objects" >&2
  exit 1
fi
