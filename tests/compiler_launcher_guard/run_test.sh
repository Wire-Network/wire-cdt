#!/usr/bin/env bash

set -euo pipefail

build_root=$1
source_dir=$2

if ! ccache_path=$(command -v ccache); then
  echo "Skipping compiler launcher guard test: ccache is unavailable"
  exit 0
fi

ccache_version=$("$ccache_path" --version | awk 'NR == 1 { print $3 }')
case "$ccache_version" in
  4.14|4.14.*)
    echo "Skipping compiler launcher guard test: ccache $ccache_version is unsafe with CDT wrappers"
    exit 0
    ;;
esac

test_root="$build_root/tests/compiler_launcher_guard_work"
cache_dir="$test_root/ccache"
project_build="$test_root/project"
magic_enum_header=$(find "$build_root/vcpkg_installed" \
  -path '*/include/magic_enum/magic_enum.hpp' -print -quit)
if [[ -z "$magic_enum_header" ]]; then
  echo "Could not find the vcpkg magic_enum headers" >&2
  exit 1
fi
contract_include=${magic_enum_header%/magic_enum/magic_enum.hpp}

cmake -E remove_directory "$test_root"
cmake -E make_directory "$cache_dir"

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
    -DCMAKE_C_COMPILER_LAUNCHER="$ccache_path" \
    -DCMAKE_CXX_COMPILER_LAUNCHER="$ccache_path"
}

configure_contract_project "$project_build"
cmake --build "$project_build"
test -f "$project_build/launcher_guard.wasm"
test -f "$project_build/launcher_guard.abi"

"$ccache_path" --zero-stats
cmake -E remove_directory "$project_build"
configure_contract_project "$project_build"
cmake --build "$project_build"
test -f "$project_build/launcher_guard.wasm"
test -f "$project_build/launcher_guard.abi"

direct_hits=$("$ccache_path" --print-stats | awk '$1 == "direct_cache_hit" { print $2 }')
preprocessed_hits=$("$ccache_path" --print-stats | awk '$1 == "preprocessed_cache_hit" { print $2 }')
cache_misses=$("$ccache_path" --print-stats | awk '$1 == "cache_miss" { print $2 }')
if (( direct_hits + preprocessed_hits != 1 || cache_misses != 0 )); then
  echo "Expected exactly one warm hit from the object-only cache probe" >&2
  "$ccache_path" --show-stats >&2
  exit 1
fi
