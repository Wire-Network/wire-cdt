#!/usr/bin/env bash
#
# Regression tests for sysio-pp (the WABT post-pass), de-vendored onto the
# vcpkg WABT 1.0.41 port. Guards the WSA-020 / SEC-10 findings:
#
#   * FillFromSegments out-of-bounds write: a module whose data segments are
#     NOT ordered by ascending offset (lowest-offset segment last) used to
#     overflow the reconstructed-memory buffer (heap-buffer-overflow / segfault).
#     Post-fix it must size the buffer by the max segment end, so it neither
#     crashes nor corrupts — and the OUTPUT for normal (ascending) layouts is
#     unchanged.
#
# Usage: postpass_tests.sh <cdt-bin-dir>
#   <cdt-bin-dir> contains sysio-pp, sysio-wast2wasm, sysio-wasm2wast.
set -u

BIN="${1:?usage: postpass_tests.sh <cdt-bin-dir>}"
PP="$BIN/sysio-pp"
WAT2WASM="$BIN/sysio-wast2wasm"
WASM2WAT="$BIN/sysio-wasm2wast"

for t in "$PP" "$WAT2WASM" "$WASM2WAT"; do
   [ -x "$t" ] || { echo "FAIL: missing tool $t"; exit 1; }
done

TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
fail=0

# run_case <name> <wat-source>: post-pass must exit 0 and yield parseable wasm.
run_case() {
   local name="$1" wat="$2"
   printf '%s\n' "$wat" > "$TMP/$name.wat"
   if ! "$WAT2WASM" "$TMP/$name.wat" -o "$TMP/$name.wasm" 2>"$TMP/$name.w2w.err"; then
      echo "FAIL[$name]: wat2wasm failed"; cat "$TMP/$name.w2w.err"; fail=1; return
   fi
   "$PP" -o "$TMP/$name.out.wasm" "$TMP/$name.wasm" 2>"$TMP/$name.pp.err"
   local rc=$?
   if [ $rc -ne 0 ]; then
      echo "FAIL[$name]: sysio-pp exit=$rc (regression: WSA-020 OOB?)"; cat "$TMP/$name.pp.err"; fail=1; return
   fi
   if ! "$WASM2WAT" "$TMP/$name.out.wasm" -o /dev/null 2>"$TMP/$name.v.err"; then
      echo "FAIL[$name]: post-passed output is not valid wasm"; cat "$TMP/$name.v.err"; fail=1; return
   fi
   echo "ok[$name]"
}

# Normal layout: highest-offset segment last (ascending). Always worked.
run_case normal_layout '(module
  (memory 2)
  (global (mut i32) (i32.const 8192))
  (global i32 (i32.const 16400))
  (export "__heap_base" (global 1))
  (data (i32.const 0) "\aa\bb\cc\dd")
  (data (i32.const 16384) "\11\22\33\44\55\66\77\88"))'

# WSA-020 OOB layout: offset-0 segment LAST. Pre-fix this overflowed/segfaulted.
run_case wsa020_oob_offset0_last '(module
  (memory 2)
  (global (mut i32) (i32.const 8192))
  (global i32 (i32.const 16400))
  (export "__heap_base" (global 1))
  (data (i32.const 16384) "\11\22\33\44\55\66\77\88")
  (data (i32.const 0) "\aa\bb\cc\dd"))'

# Single contiguous segment (typical contract shape).
run_case single_segment '(module
  (memory 1)
  (global (mut i32) (i32.const 8192))
  (global i32 (i32.const 8320))
  (export "__heap_base" (global 1))
  (data (i32.const 1024) "the quick brown fox jumps over the lazy dog 0123456789"))'

if [ $fail -ne 0 ]; then
   echo "postpass_tests: FAILED"
   exit 1
fi
echo "postpass_tests: all passed"
exit 0
