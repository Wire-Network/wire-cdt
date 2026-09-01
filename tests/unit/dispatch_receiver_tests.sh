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

# It must run before any action is dispatched, or a guard could read a stale value.
line_set="$(grep -n 'sysio_set_contract_name(' "$DISPATCH" | tail -1 | cut -d: -f1 || true)"
line_apply="$(grep -n 'void apply' "$DISPATCH" | head -1 | cut -d: -f1 || true)"
line_first_action="$(grep -nE 'sysio_wasm_action|executed|action_wrapper|::go' "$DISPATCH" | awk -F: -v s="${line_set:-0}" '$1 > s {print $1; exit}' || true)"
if [ -n "$line_set" ] && [ -n "$line_apply" ] && [ "$line_set" -gt "$line_apply" ] \
   && { [ -z "$line_first_action" ] || [ "$line_set" -lt "$line_first_action" ]; }; then
    pass "the receiver is recorded before any action runs"
else
    fail "the receiver is recorded before any action runs"
    echo "    apply at ${line_apply:-?}, set at ${line_set:-?}, first action at ${line_first_action:-none}"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
