#!/bin/bash
# The generated apply() must record the RECEIVER in sysio_contract_name.
#
# multi_index's receiving_account() reads that global and falls back to the current_receiver
# intrinsic only when it is 0, so the guards on emplace/modify/erase are only correct if the
# dispatcher stores `r` (the receiver) rather than `c` (the code). Every in-tree action is
# self-sent, so r == c and the entire unit + integration suite stays green if that argument is
# changed -- the divergence appears only under notification, on chain. This pins it at the
# source: the emitted dispatch text, which no other test inspects.
#
# Usage: dispatch_receiver_tests.sh <build_dir>
set -euo pipefail

BUILD_DIR="$1"
CDT_CPP="${BUILD_DIR}/bin/cdt-cpp"
PASS=0
FAIL=0
pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "${WORK}/c.cpp" <<'EOF'
#include <sysio/sysio.hpp>
class [[sysio::contract("dispatchrcv")]] dispatchrcv : public sysio::contract {
public:
   using contract::contract;
   [[sysio::action]] void go() {}
   [[sysio::on_notify("sysio.token::transfer")]] void onxfer(sysio::name from, sysio::name to) {}
};
EOF

echo "=== Dispatch Receiver Tests ==="

if ! ( cd "$WORK" && "$CDT_CPP" -abigen -abigen_output=c.abi -contract=dispatchrcv \
          -o c.wasm c.cpp ) > "${WORK}/build.log" 2>&1; then
    fail "contract builds"
    sed 's/^/    /' "${WORK}/build.log"
    echo "Results: ${PASS} passed, ${FAIL} failed"
    exit 1
fi
pass "contract builds"

DISPATCH="$(find "$WORK" -name '*.dispatch.cpp' | head -1)"
if [ -z "$DISPATCH" ]; then
    fail "a dispatch.cpp was generated"
    echo "Results: ${PASS} passed, ${FAIL} failed"
    exit 1
fi
pass "a dispatch.cpp was generated"

# apply(uint64_t r, uint64_t c, uint64_t a): the receiver is the FIRST parameter.
if grep -qE 'sysio_set_contract_name\(\s*r\s*\)' "$DISPATCH"; then
    pass "apply() records the receiver, not the code"
else
    fail "apply() records the receiver, not the code"
    echo "    expected: sysio_set_contract_name(r)"
    grep -n "sysio_set_contract_name" "$DISPATCH" | sed 's/^/      got: /' || echo "      (no call at all)"
fi

# It must be the FIRST executable statement in apply(), not merely the first textually.
#
# Lexical ordering alone is not enough. An apply() shaped as
#
#     void apply(uint64_t r, uint64_t c, uint64_t a) {
#       if (c == r) { sysio_set_contract_name(r); __sysio_action_...(r, c); }
#       else        { __sysio_notify_...(r, c); }
#     }
#
# has exactly one setter, passes the exact-`r` check, and puts the setter before the first
# textual handler -- while every NOTIFICATION runs with the global still 0. The only assertion
# that rules that out is structural: the setter must sit at the top of the function body,
# before `pre_dispatch` and before the `if (c == r)` split, so it dominates both branches.
apply_line="$(grep -nE '^\s*(__attribute__.*)?void apply\(' "$DISPATCH" | head -1 | cut -d: -f1 || true)"
# -o | wc -l counts OCCURRENCES. `grep -c` counts matching LINES, which let
# `sysio_set_contract_name(r); sysio_set_contract_name(c);` on one line read as a single
# call: the count came to the expected 2, the first statement was still the `r` call, and the
# second call silently overwrote the global before either branch ran.
setter_count="$(grep -oE 'sysio_set_contract_name[[:space:]]*\(' "$DISPATCH" | wc -l)"
# The function body, starting immediately after the opening brace -- INCLUDING any text that
# follows it on the signature line. Reading from the next line down would miss
# `void apply(...) { if (c == r) {`, which puts a branch ahead of the setter while leaving the
# setter as the first thing on its own line.
body="$(awk -v a="${apply_line:-0}" '
    NR <  a { next }
    NR == a { sub(/^[^{]*\{/, "") }
    { print }
' "$DISPATCH" | sed 's://.*::' | tr '\n' ' ' | sed 's/[[:space:]][[:space:]]*/ /g; s/^ //')"
# The first statement is everything up to and including the first semicolon.
first_stmt="${body%%;*};"

if [ -z "$apply_line" ]; then
    fail "apply() is defined in the generated dispatch"
elif [ "$setter_count" -ne 2 ]; then
    # one declaration in the extern "C" block, one call inside apply()
    fail "the dispatch declares and calls the setter exactly once each"
    echo "    found ${setter_count} occurrence(s), expected 2"
    grep -n "sysio_set_contract_name" "$DISPATCH" | sed 's/^/      /'
elif [ "$first_stmt" = "sysio_set_contract_name(r);" ]; then
    pass "the receiver is recorded as apply()'s first statement, before any branch"
else
    fail "the receiver is recorded as apply()'s first statement, before any branch"
    echo "    first statement after apply() is: ${first_stmt}"
    sed 's/^/      /' "$DISPATCH"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
