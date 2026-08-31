#!/bin/bash
# Regression tests for cdt-abidiff's ABI version handling.
#
# get_version used to be `stod(ver.substr(ver.size() - 3)) * 10`, a fixed-width suffix
# read that returns 1 for "sysio::abi/1.10" (it sees ".10"). Both capability gates in
# diff() compared that against 11 and 12, so for any two-digit minor the variant and
# action-result diffs were silently skipped -- a real difference reported as none. The
# same suffix read also collapsed "eosio::abi/1.2" and "sysio::abi/1.2" to one number.
#
# cdt-abidiff now shares abi_version::parse_version_string and the supports_* predicates
# with the rest of the toolchain, so the gates compare (major, minor) components.
#
# Usage: abidiff_tests.sh <bin_dir>
set -euo pipefail

BIN_DIR="$1"
ABIDIFF="${BIN_DIR}/cdt-abidiff"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Two ABIs identical but for one action_result entry. At any version that supports the
# section, the difference must be reported.
write_pair() {
    local version="$1"
    cat > "${WORK}/a.abi" <<EOF
{
  "version": "${version}",
  "types": [], "structs": [], "actions": [], "tables": [],
  "ricardian_clauses": [], "variants": [],
  "action_results": [ { "name": "geta", "result_type": "uint64" } ]
}
EOF
    cat > "${WORK}/b.abi" <<EOF
{
  "version": "${version}",
  "types": [], "structs": [], "actions": [], "tables": [],
  "ricardian_clauses": [], "variants": [],
  "action_results": [ { "name": "getb", "result_type": "uint64" } ]
}
EOF
}

check_reports_diff() {
    local version="$1" desc="$2"
    write_pair "$version"
    # cdt-abidiff exits non-zero when it finds differences, so tolerate that.
    local out
    out="$("$ABIDIFF" "${WORK}/a.abi" "${WORK}/b.abi" 2>&1 || true)"
    if grep -qE "geta|getb" <<< "$out"; then
        pass "$desc"
    else
        fail "$desc"
        echo "    expected the action_results difference to be reported; got:"
        sed 's/^/      /' <<< "$out"
    fi
}

echo "=== cdt-abidiff Tests ==="

check_reports_diff "sysio::abi/1.2"  "1.2 reports an action_results difference"
check_reports_diff "sysio::abi/1.3"  "1.3 reports an action_results difference"
# The regression: the suffix read scored this 1, below both gates, and reported nothing.
check_reports_diff "sysio::abi/1.10" "1.10 reports an action_results difference"

# A version difference must be reported on its own, and must not be masked by two
# spellings collapsing to the same number.
# Both carry action_results: at any version at or above 1.2 cdt-abidiff will diff that
# section, and abigen always emits the array, so omitting it is not a valid document.
cat > "${WORK}/v1.abi" <<'EOF'
{ "version": "sysio::abi/1.2", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [], "action_results": [] }
EOF
cat > "${WORK}/v2.abi" <<'EOF'
{ "version": "sysio::abi/1.10", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [], "action_results": [] }
EOF
out="$("$ABIDIFF" "${WORK}/v1.abi" "${WORK}/v2.abi" 2>&1 || true)"
if grep -q "version" <<< "$out"; then
    pass "1.2 vs 1.10 reports a version difference"
else
    fail "1.2 vs 1.10 reports a version difference"
    sed 's/^/      /' <<< "$out"
fi

# Identical inputs must stay quiet.
out="$("$ABIDIFF" "${WORK}/v1.abi" "${WORK}/v1.abi" 2>&1 || true)"
if grep -qE "version|action_results" <<< "$out"; then
    fail "identical ABIs report no difference"
    sed 's/^/      /' <<< "$out"
else
    pass "identical ABIs report no difference"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
