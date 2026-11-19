#!/bin/bash
# Pins the order in which the generated apply() lists action and notify handlers, driven through
# `cdt-codegen --finalize` with hand-written descriptors.
#
# Actions are ordered by name, and two handlers for one action keep the first. Notify handlers are
# grouped by action name, each group lists named contracts in order, and the `*` wildcard comes
# last in its group. The order reaches the dispatch source and therefore every contract's WASM.
#
# Usage: dispatch_order_tests.sh <bin_dir>
set -euo pipefail

BIN_DIR="$1"
CODEGEN="${BIN_DIR}/cdt-codegen"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# $1 is the wasm_actions array, $2 the wasm_notifies array.
desc() {
    cat <<EOF
{
  "____comment": "hand-written test descriptor",
  "version": "sysio::abi/1.2",
  "structs": [], "types": [], "actions": [],
  "ricardian_clauses": [], "variants": [], "abi_extensions": [], "action_results": [],
  "wasm_actions": [$1],
  "wasm_notifies": [$2],
  "wasm_entries": [], "pb_types": [], "tables": [], "____table_annotations": []
}
EOF
}

# Each descriptor lists its handlers out of order. The second repeats the first's wildcard
# transfer handler, which must appear once.
TU_A_ACTIONS='{"name": "zeta", "handler": "act_zeta"}, {"name": "alpha", "handler": "act_alpha"}'
TU_A_NOTIFIES='{"name": "transfer", "contract": "*", "handler": "on_transfer_any"},
               {"name": "transfer", "contract": "sysio.token", "handler": "on_transfer_token"},
               {"name": "issue", "contract": "*", "handler": "on_issue_any"}'
TU_B_ACTIONS='{"name": "mid", "handler": "act_mid"}, {"name": "alpha", "handler": "act_alpha_dup"}'
TU_B_NOTIFIES='{"name": "transfer", "contract": "alice", "handler": "on_transfer_alice"},
               {"name": "transfer", "contract": "*", "handler": "on_transfer_any"},
               {"name": "issue", "contract": "bob", "handler": "on_issue_bob"}'

# cdt-codegen merges descriptors in filename order, so the two layouts swap which one goes first.
mkdir -p "$WORK/a_first" "$WORK/b_first"
desc "$TU_A_ACTIONS" "$TU_A_NOTIFIES" > "$WORK/a_first/1_a.desc"
desc "$TU_B_ACTIONS" "$TU_B_NOTIFIES" > "$WORK/a_first/2_b.desc"
desc "$TU_B_ACTIONS" "$TU_B_NOTIFIES" > "$WORK/b_first/1_b.desc"
desc "$TU_A_ACTIONS" "$TU_A_NOTIFIES" > "$WORK/b_first/2_a.desc"

# The dispatch lines that carry order: action cases, notify action tests, contract cases and
# wildcard defaults, and the handler calls. Whitespace is dropped.
dispatch_order() {
    grep -E '^ *(case "|default:|if \(a ==|else if \(a ==|(act|on)_[a-z_]+\(r, c\);)' "$1" | sed -E 's/^ +//'
}

# act_alpha belongs to the descriptor merged first, so the expected order differs only there.
expected() {
    cat <<EOF
case "alpha"_n.value:
$1(r, c);
case "mid"_n.value:
act_mid(r, c);
case "zeta"_n.value:
act_zeta(r, c);
default:
if (a == "issue"_n.value) {
case "bob"_n.value:
on_issue_bob(r, c);
default:
on_issue_any(r, c);
else if (a == "transfer"_n.value) {
case "alice"_n.value:
on_transfer_alice(r, c);
case "sysio.token"_n.value:
on_transfer_token(r, c);
default:
on_transfer_any(r, c);
EOF
}

check_order() {
    local label="$1" first_alpha="$2"
    local dispatch="$WORK/${label}.dispatch.cpp"
    if ! "$CODEGEN" --finalize --contract order_probe \
            --desc-file "$WORK/${label}/1_"*.desc --desc-file "$WORK/${label}/2_"*.desc \
            --abi-output "$WORK/${label}.abi" --dispatch-output "$dispatch" 2>"$WORK/${label}.err"; then
        fail "${label}: cdt-codegen exited non-zero"
        sed 's/^/      /' "$WORK/${label}.err"
        return
    fi
    if [ ! -s "$dispatch" ]; then
        fail "${label}: no dispatch source generated"
        return
    fi
    if diff -u <(expected "$first_alpha") <(dispatch_order "$dispatch") > "$WORK/${label}.diff"; then
        pass "${label}: handlers in name order, wildcard last in each notify group"
    else
        fail "${label}: unexpected handler order"
        sed 's/^/      /' "$WORK/${label}.diff"
    fi
}

echo "Dispatch handler order"
check_order a_first act_alpha
check_order b_first act_alpha_dup

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
