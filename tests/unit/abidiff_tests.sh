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

# Valid input: cdt-abidiff must exit 0 whether or not it reports differences. A non-zero
# status is a crash or a rejection, never "found a difference", so it propagates to the
# caller instead of being folded into the output text with `|| true`.
run_abidiff() { "$ABIDIFF" "$@" 2>&1; }

# Capture output, failing the named case outright if the process did not exit 0.
# Sets `out`; returns non-zero when the case has already been failed.
capture() {
    local desc="$1"; shift
    out="$(run_abidiff "$@")" && return 0
    fail "${desc} (cdt-abidiff exited non-zero)"
    sed 's/^/      /' <<< "$out"
    return 1
}
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
    # Via capture(), not a manual rc=$? -- under `set -e` a failing command substitution in an
    # assignment aborts the script before $? can be read, so that branch was unreachable.
    capture "$desc" "${WORK}/a.abi" "${WORK}/b.abi" || return
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
if capture "1.2 vs 1.10 reports a version difference" "${WORK}/v1.abi" "${WORK}/v2.abi" &&
   grep -q "version" <<< "$out"; then
    pass "1.2 vs 1.10 reports a version difference"
else
    fail "1.2 vs 1.10 reports a version difference"
    sed 's/^/      /' <<< "$out"
fi

# Identical inputs must stay quiet.
if ! capture "identical ABIs report no difference" "${WORK}/v1.abi" "${WORK}/v1.abi"; then
    :
elif grep -qE "version|action_results" <<< "$out"; then
    fail "identical ABIs report no difference"
    sed 's/^/      /' <<< "$out"
else
    pass "identical ABIs report no difference"
fi

# Reordered but equivalent action_results must NOT report a difference. find_action_results
# compared the matched entry against abi2[...].at(i) instead of .at(j), so once a name matched
# at a different index the result_type comparison read the wrong entry and reported all four
# sides as changed. Pre-existing, but this PR routes 1.10 through that path.
cat > "${WORK}/r1.abi" <<'EOF'
{ "version": "sysio::abi/1.10", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [],
  "action_results": [ { "name": "geta", "result_type": "uint64" }, { "name": "getb", "result_type": "uint32" } ] }
EOF
cat > "${WORK}/r2.abi" <<'EOF'
{ "version": "sysio::abi/1.10", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [],
  "action_results": [ { "name": "getb", "result_type": "uint32" }, { "name": "geta", "result_type": "uint64" } ] }
EOF
if ! capture "reordered equivalent action_results report no difference" \
        "${WORK}/r1.abi" "${WORK}/r2.abi"; then
    :
elif grep -qE "geta|getb" <<< "$out"; then
    fail "reordered equivalent action_results report no difference"
    sed 's/^/      /' <<< "$out"
else
    pass "reordered equivalent action_results report no difference"
fi

# An unsupported or unparsable version must be refused, not silently read as the 1.2 default.
# parse() rejects majors above 1, so seeding the outputs with 1.2 and ignoring the result made
# a 2.0 document compare equal to a 1.2 one.
cat > "${WORK}/v20.abi" <<'EOF'
{ "version": "sysio::abi/2.0", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [], "action_results": [] }
EOF
if "$ABIDIFF" "${WORK}/v20.abi" "${WORK}/v1.abi" > "${WORK}/v20.log" 2>&1; then
    fail "an unsupported ABI version is refused"
elif grep -q "unsupported ABI version" "${WORK}/v20.log"; then
    pass "an unsupported ABI version is refused with a diagnostic"
else
    fail "unsupported ABI version refused, but without the expected diagnostic"
    sed 's/^/      /' "${WORK}/v20.log"
fi

# Variants at 1.10. find_variants broke out of the element loop on a type mismatch and then
# set found unconditionally, so a same-named variant counted as unchanged however its types
# differed; with no length check, at(k) threw on a shorter right-hand side. This PR is what
# routes 1.10 into that matcher.
mkvariant() { # file, types-json
    cat > "$1" <<EOF
{ "version": "sysio::abi/1.10", "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [],
  "variants": [ { "name": "v", "types": $2 } ], "action_results": [] }
EOF
}

mkvariant "${WORK}/va.abi" '["uint64"]'
mkvariant "${WORK}/vb.abi" '["string"]'
if capture "1.10 variants differing by type report a difference" "${WORK}/va.abi" "${WORK}/vb.abi" &&
   grep -q "variant" <<< "$out"; then
    pass "1.10 variants differing by type report a difference"
else
    fail "1.10 variants differing by type report a difference"
    sed 's/^/      /' <<< "$out"
fi

mkvariant "${WORK}/vlong.abi"  '["uint64", "string"]'
mkvariant "${WORK}/vshort.abi" '["uint64"]'
if ! capture "1.10 variants of differing length report a difference" \
        "${WORK}/vlong.abi" "${WORK}/vshort.abi"; then
    :
elif grep -q "variant" <<< "$out"; then
    pass "1.10 variants of differing length report a difference"
else
    fail "1.10 variants of differing length report a difference"
    sed 's/^/      /' <<< "$out"
fi

mkvariant "${WORK}/vsame1.abi" '["uint64", "string"]'
mkvariant "${WORK}/vsame2.abi" '["uint64", "string"]'
if ! capture "identical 1.10 variants report no difference" "${WORK}/vsame1.abi" "${WORK}/vsame2.abi"; then
    :
elif grep -q "variant" <<< "$out"; then
    fail "identical 1.10 variants report no difference"
    sed 's/^/      /' <<< "$out"
else
    pass "identical 1.10 variants report no difference"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
