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

# It must run before anything is dispatched, or a guard could read a stale value.
#
# Match the names the generator actually emits -- `pre_dispatch(`, `__sysio_action_*(` and
# `__sysio_notify_*(`. An earlier version of this test grepped for names that appear nowhere in
# the output, so the "first dispatch" line came back empty and the comparison was skipped as a
# pass: moving the setter below every handler would have satisfied it. The search deliberately
# does NOT start from the setter's line, which would make any match tautologically later.
apply_line="$(grep -nE '^\s*(__attribute__.*)?void apply\(' "$DISPATCH" | head -1 | cut -d: -f1 || true)"
first_dispatch="$(awk -v a="${apply_line:-0}" \
    'NR > a && /(pre_dispatch\(|__sysio_(action|notify)_[A-Za-z0-9_]*\()/ { print NR; exit }' "$DISPATCH")"
setter_lines="$(grep -nE 'sysio_set_contract_name\(' "$DISPATCH" | awk -F: -v a="${apply_line:-0}" '$1 > a {print $1}')"
setter_count="$(printf '%s\n' "$setter_lines" | grep -c . || true)"

if [ -z "$apply_line" ]; then
    fail "apply() is defined in the generated dispatch"
elif [ -z "$first_dispatch" ]; then
    fail "the generated apply() dispatches to a handler"
    echo "    no pre_dispatch/__sysio_action_*/__sysio_notify_* call found after line ${apply_line}"
    sed 's/^/      /' "$DISPATCH"
elif [ "$setter_count" -ne 1 ]; then
    fail "apply() records the receiver exactly once"
    echo "    found ${setter_count} call(s) inside apply(), expected 1"
elif [ "$setter_lines" -lt "$first_dispatch" ]; then
    pass "the receiver is recorded before anything is dispatched"
else
    fail "the receiver is recorded before anything is dispatched"
    echo "    setter at line ${setter_lines}, first dispatch at line ${first_dispatch}"
    sed 's/^/      /' "$DISPATCH"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
