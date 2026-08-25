#!/usr/bin/env sh

set -eu

# Add versions only after compiler_launcher_guard_tests passes its distinct
# source, Wasm/native separation, and cold/warm cache assertions.
case "${1:-}" in
  4.9.1|4.13.6)
    exit 0
    ;;
  *)
    exit 1
    ;;
esac
