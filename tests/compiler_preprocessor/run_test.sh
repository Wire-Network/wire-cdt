#!/usr/bin/env bash

set -euo pipefail

build_root=$(cd "$1" && pwd)
source_dir=$(cd "$2" && pwd)
test_root="$build_root/tests/compiler_preprocessor_work"

cmake -E remove_directory "$test_root"
cmake -E make_directory "$test_root"

run_preprocessor_tests() {
  local compiler=$1
  local extension=$2
  local label=$3
  local compiler_root="$test_root/$label"
  local stdout_output="$compiler_root/cache_probe_a.stdout.i"
  local second_stdout_output="$compiler_root/cache_probe_b.stdout.i"
  local explicit_output="$compiler_root/cache_probe_a.explicit.i"
  local explicit_stdout="$compiler_root/cache_probe_a.explicit.stdout"
  local dash_output="$compiler_root/cache_probe_a.dash.i"

  cmake -E make_directory "$compiler_root"
  (
    cd "$compiler_root"
    "$compiler" -E "$source_dir/cache_probe_a.$extension" >"$stdout_output"
  )
  if [[ ! -s "$stdout_output" ]]; then
    echo "$label -E produced empty stdout" >&2
    exit 1
  fi
  if [[ -e "$compiler_root/cache_probe_a.wasm" ]]; then
    echo "$label -E unexpectedly created cache_probe_a.wasm" >&2
    exit 1
  fi

  "$compiler" -E "$source_dir/cache_probe_b.$extension" \
    >"$second_stdout_output"
  if [[ ! -s "$second_stdout_output" ]] ||
      cmp -s "$stdout_output" "$second_stdout_output"; then
    echo "$label did not produce distinct output for distinct sources" >&2
    exit 1
  fi

  "$compiler" -E -o "$explicit_output" \
    "$source_dir/cache_probe_a.$extension" >"$explicit_stdout"
  if [[ ! -s "$explicit_output" ]]; then
    echo "$label -E -o did not create the requested output" >&2
    exit 1
  fi
  if [[ -s "$explicit_stdout" ]]; then
    echo "$label -E -o unexpectedly wrote to stdout" >&2
    exit 1
  fi
  if ! cmp -s "$stdout_output" "$explicit_output"; then
    echo "$label implicit stdout and explicit preprocessor outputs differ" >&2
    exit 1
  fi

  "$compiler" -E -o - "$source_dir/cache_probe_a.$extension" >"$dash_output"
  if [[ ! -s "$dash_output" ]] || ! cmp -s "$stdout_output" "$dash_output"; then
    echo "$label -E -o - did not reproduce the stdout output" >&2
    exit 1
  fi
}

cache_stat() {
  local ccache_path=$1
  local stat_name=$2
  "$ccache_path" --print-stats |
    awk -v stat_name="$stat_name" '$1 == stat_name { print $2 }'
}

run_ccache_regression() {
  local ccache_path=$1
  local compiler=$2
  local extension=$3
  local label=$4
  local compiler_root="$test_root/$label"
  local cache_dir="$compiler_root/ccache"
  local direct_a="$compiler_root/direct_a.o"
  local direct_b="$compiler_root/direct_b.o"
  local cached_a="$compiler_root/cached_a.o"
  local cached_b="$compiler_root/cached_b.o"
  local misses
  local hits

  cmake -E make_directory "$cache_dir"
  "$compiler" -c -o "$direct_a" "$source_dir/cache_probe_a.$extension"
  "$compiler" -c -o "$direct_b" "$source_dir/cache_probe_b.$extension"

  export CCACHE_DIR="$cache_dir"
  export CCACHE_NODIRECT=true
  unset CCACHE_DEPEND CCACHE_DISABLE CCACHE_RECACHE CCACHE_READONLY
  "$ccache_path" --zero-stats
  "$ccache_path" "$compiler" -c -o "$cached_a" \
    "$source_dir/cache_probe_a.$extension"
  "$ccache_path" "$compiler" -c -o "$cached_b" \
    "$source_dir/cache_probe_b.$extension"

  if ! cmp -s "$direct_a" "$cached_a" || ! cmp -s "$direct_b" "$cached_b"; then
    echo "ccache returned an object for the wrong $label source" >&2
    exit 1
  fi
  if cmp -s "$cached_a" "$cached_b"; then
    echo "Distinct $label sources collapsed to the same object" >&2
    exit 1
  fi

  misses=$(cache_stat "$ccache_path" cache_miss)
  if [[ "$misses" != 2 ]]; then
    echo "Expected two cold $label ccache misses, got ${misses:-<missing>}" >&2
    "$ccache_path" --show-stats >&2
    exit 1
  fi

  "$ccache_path" --zero-stats
  "$ccache_path" "$compiler" -c -o "$cached_a" \
    "$source_dir/cache_probe_a.$extension"
  "$ccache_path" "$compiler" -c -o "$cached_b" \
    "$source_dir/cache_probe_b.$extension"
  hits=$(cache_stat "$ccache_path" preprocessed_cache_hit)
  if [[ "$hits" != 2 ]]; then
    echo "Expected two warm $label preprocessed ccache hits, got ${hits:-<missing>}" >&2
    "$ccache_path" --show-stats >&2
    exit 1
  fi
}

cdt_cc="$build_root/bin/cdt-cc"
cdt_cpp="$build_root/bin/cdt-cpp"
run_preprocessor_tests "$cdt_cc" c cdt-cc
run_preprocessor_tests "$cdt_cpp" cpp cdt-cpp

if ccache_path=$(command -v ccache); then
  echo "Testing $("$ccache_path" --version | sed -n '1p')"
  run_ccache_regression "$ccache_path" "$cdt_cc" c cdt-cc
  run_ccache_regression "$ccache_path" "$cdt_cpp" cpp cdt-cpp
else
  echo "ccache unavailable; direct CDT preprocessing checks passed"
fi
