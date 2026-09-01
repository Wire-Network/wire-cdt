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
elif grep -qE "^[<>] (version|struct|type|action|table|clause|variant|action_result)" <<< "$out"; then
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

# --- struct matching -------------------------------------------------------------------
#
# find_structs kept a success flag across its field loop and broke out of it on a mismatch
# without clearing it, so only a difference in the FIRST field was ever detected. It also
# seeded the flag false and set it only inside that loop, so two identical zero-field
# structs -- which every parameterless action generates -- compared as different.

mkstruct() {   # $1=path  $2=fields JSON
    cat > "$1" <<EOF
{
  "version": "sysio::abi/1.2",
  "types": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "structs": [ { "name": "s", "base": "", "fields": $2 } ]
}
EOF
}

expect_reports()  { # $1=desc $2=a $3=b $4=needle
    if ! capture "$1" "$2" "$3"; then :
    elif grep -q "$4" <<< "$out"; then pass "$1"
    else fail "$1"; sed 's/^/      /' <<< "$out"; fi
}
expect_quiet()    { # $1=desc $2=a $3=b $4=needle
    if ! capture "$1" "$2" "$3"; then :
    elif grep -q "$4" <<< "$out"; then fail "$1"; sed 's/^/      /' <<< "$out"
    else pass "$1"; fi
}

mkstruct "${WORK}/s_ab_int.abi"  '[{"name":"a","type":"uint64"},{"name":"b","type":"uint64"}]'
mkstruct "${WORK}/s_ab_str.abi"  '[{"name":"a","type":"uint64"},{"name":"b","type":"string"}]'
expect_reports "a change in a non-first struct field is reported" \
    "${WORK}/s_ab_int.abi" "${WORK}/s_ab_str.abi" "struct"

mkstruct "${WORK}/s_first.abi"   '[{"name":"z","type":"uint64"},{"name":"b","type":"uint64"}]'
expect_reports "a change in the first struct field is still reported" \
    "${WORK}/s_ab_int.abi" "${WORK}/s_first.abi" "struct"

expect_quiet "an identical multi-field struct reports no difference" \
    "${WORK}/s_ab_int.abi" "${WORK}/s_ab_int.abi" "struct"

mkstruct "${WORK}/s_empty1.abi" '[]'
mkstruct "${WORK}/s_empty2.abi" '[]'
expect_quiet "an identical zero-field struct reports no difference" \
    "${WORK}/s_empty1.abi" "${WORK}/s_empty2.abi" "struct"

mkstruct "${WORK}/s_reorder.abi" '[{"name":"b","type":"uint64"},{"name":"a","type":"uint64"}]'
expect_reports "reordered struct fields are reported (order is serialization order)" \
    "${WORK}/s_ab_int.abi" "${WORK}/s_reorder.abi" "struct"

# --- table matching --------------------------------------------------------------------
#
# find_tables compared only name and type, so index_type, key_names, key_types and table_id
# could all change with no difference reported -- the metadata a contract upgrade turns on.

mktable() {   # $1=path  $2=index_type  $3=key_names  $4=key_types  $5=table_id
    cat > "$1" <<EOF
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "tables": [ { "name": "t", "type": "row", "index_type": "$2",
                "key_names": $3, "key_types": $4, "table_id": $5 } ]
}
EOF
}

mktable "${WORK}/t_base.abi"  i64  '["id"]'          '["uint64"]'         100
mktable "${WORK}/t_idx.abi"   kv64 '["id"]'          '["uint64"]'         100
# key_names alone -- an earlier version varied key_types at the same time, so deleting the
# key_names comparison left the suite green.
mktable "${WORK}/t_names.abi" i64  '["owner"]'       '["uint64"]'         100
mktable "${WORK}/t_types.abi" i64  '["id"]'          '["name"]'           100
mktable "${WORK}/t_id.abi"    i64  '["id"]'          '["uint64"]'         200

cat > "${WORK}/t_type.abi" <<EOF
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "tables": [ { "name": "t", "type": "other_row", "index_type": "i64",
                "key_names": ["id"], "key_types": ["uint64"], "table_id": 100 } ]
}
EOF
expect_reports "a changed table row type is reported"   "${WORK}/t_base.abi" "${WORK}/t_type.abi"  "table"
expect_reports "a changed table index_type is reported" "${WORK}/t_base.abi" "${WORK}/t_idx.abi"   "table"
expect_reports "changed table key_names are reported"   "${WORK}/t_base.abi" "${WORK}/t_names.abi" "table"
expect_reports "changed table key_types are reported"   "${WORK}/t_base.abi" "${WORK}/t_types.abi" "table"
expect_reports "a changed table_id is reported"         "${WORK}/t_base.abi" "${WORK}/t_id.abi"    "table"
expect_quiet   "an identical table reports no difference" "${WORK}/t_base.abi" "${WORK}/t_base.abi" "table"

# secondary_indexes is a table field too, and each entry carries its own table_id.
cat > "${WORK}/t_sec_a.abi" <<'EOF'
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "tables": [ { "name": "t", "type": "row", "index_type": "i64",
                "key_names": ["id"], "key_types": ["uint64"], "table_id": 100,
                "secondary_indexes": [ { "name": "byowner", "type": "name", "table_id": 37799 } ] } ]
}
EOF
sed 's/37799/60481/' "${WORK}/t_sec_a.abi" > "${WORK}/t_sec_b.abi"
expect_reports "a changed secondary index table_id is reported" \
    "${WORK}/t_sec_a.abi" "${WORK}/t_sec_b.abi" "table"
expect_quiet "an identical table with secondary indexes reports no difference" \
    "${WORK}/t_sec_a.abi" "${WORK}/t_sec_a.abi" "table"

# --- optional keys ---------------------------------------------------------------------
#
# table_id, index_type and secondary_indexes are Wire extensions: a stock Antelope/eosio-cdt
# ABI carries none of them. Reading an absent key through jsoncons' const operator[] throws,
# so comparing them naively aborted the tool (exit 255) on every such ABI -- including two
# byte-identical ones. capture() already fails a case whose process exits non-zero, so these
# assert the comparison happens at all, not merely that it is quiet.
cat > "${WORK}/t_antelope.abi" <<'EOF'
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "tables": [ { "name": "t", "type": "row", "index_type": "i64",
                "key_names": ["id"], "key_types": ["uint64"] } ]
}
EOF
expect_quiet "an ABI with no table_id diffs cleanly against itself" \
    "${WORK}/t_antelope.abi" "${WORK}/t_antelope.abi" "table"
expect_reports "an ABI with no table_id differs from one that has it" \
    "${WORK}/t_antelope.abi" "${WORK}/t_base.abi" "table"

cat > "${WORK}/t_minimal.abi" <<'EOF'
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "tables": [ { "name": "t", "type": "row" } ]
}
EOF
expect_quiet "a name+type-only table diffs cleanly against itself" \
    "${WORK}/t_minimal.abi" "${WORK}/t_minimal.abi" "table"

# A struct with no "base" key, as ABIs from other toolchains emit.
cat > "${WORK}/s_nobase.abi" <<'EOF'
{
  "version": "sysio::abi/1.2",
  "types": [], "actions": [], "tables": [], "ricardian_clauses": [], "variants": [],
  "action_results": [],
  "structs": [ { "name": "s", "fields": [ {"name":"a","type":"uint64"} ] } ]
}
EOF
expect_quiet "a struct with no base key diffs cleanly against itself" \
    "${WORK}/s_nobase.abi" "${WORK}/s_nobase.abi" "struct"

# --- ricardian clauses -----------------------------------------------------------------
#
# find_clauses iterates "ricardian_clauses" but print_clause read "clauses", so the tool
# aborted the moment it had a clause difference to report -- it could never report one.
mkclause() {   # $1=path  $2=body
    cat > "$1" <<EOF
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "tables": [], "variants": [],
  "action_results": [],
  "ricardian_clauses": [ { "id": "c1", "body": "$2" } ]
}
EOF
}
mkclause "${WORK}/c_a.abi" "original text"
mkclause "${WORK}/c_b.abi" "revised text"
expect_reports "a changed ricardian clause is reported" "${WORK}/c_a.abi" "${WORK}/c_b.abi" "clause"
expect_quiet   "an identical ricardian clause reports no difference" \
    "${WORK}/c_a.abi" "${WORK}/c_a.abi" "clause"

# --- sections that were compared by nothing ----------------------------------------------
mkenum() {   # $1=path  $2=values JSON
    cat > "$1" <<EOF
{
  "version": "sysio::abi/1.2",
  "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [],
  "variants": [], "action_results": [],
  "enums": [ { "name": "e", "type": "uint8", "values": $2 } ]
}
EOF
}
mkenum "${WORK}/e_a.abi" '["A","B"]'
mkenum "${WORK}/e_b.abi" '["A","B","C"]'
expect_reports "a changed enum is reported"            "${WORK}/e_a.abi" "${WORK}/e_b.abi" "enum"
expect_quiet   "an identical enum reports no difference" "${WORK}/e_a.abi" "${WORK}/e_a.abi" "enum"

mkpb() {   # $1=path  $2=package
    cat > "$1" <<EOF
{
  "version": "sysio::abi/1.3",
  "types": [], "structs": [], "actions": [], "tables": [], "ricardian_clauses": [],
  "variants": [], "action_results": [],
  "protobuf_types": { "file": [ { "name": "a.proto", "package": "$2" } ] }
}
EOF
}
mkpb "${WORK}/pb_a.abi" "test"
mkpb "${WORK}/pb_b.abi" "other"
expect_reports "a changed protobuf descriptor is reported" \
    "${WORK}/pb_a.abi" "${WORK}/pb_b.abi" "protobuf_types"
expect_quiet "an identical protobuf descriptor reports no difference" \
    "${WORK}/pb_a.abi" "${WORK}/pb_a.abi" "protobuf_types"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
