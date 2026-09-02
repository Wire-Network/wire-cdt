#!/bin/bash
# The generated apply() must record the RECEIVER in sysio_contract_name.
#
# multi_index's receiving_account() reads that global and falls back to the current_receiver
# intrinsic only when it is 0, so the guards on emplace/modify/erase are only correct if the
# dispatcher stores `r` (the receiver) rather than `c` (the code), exactly once, before any
# dispatch. Every in-tree action is self-sent, so r == c and the whole unit + integration suite
# stays green if that is broken -- the divergence appears only under notification, on chain.
# This pins it at the source: the emitted dispatch text, which no other test inspects.
#
# The checker is a function over a dispatch FILE, and it is exercised twice: against the real
# generated dispatch, and against a table of crafted counterexamples that must each be
# rejected. Earlier revisions of this test were defeated four times in review -- by handler
# names it did not match, by a branch on the signature line, by two calls on one line, and by a
# comment between the identifier and its paren -- because each fix pattern-matched the last
# evasion. Checking the checker is what stops that: a new evasion is one row below, not a round
# trip.
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

# Normalise the source the way the front end does, in the same order: phase 2 splices
# backslash-newline pairs, then phase 3 replaces comments with a space. Doing either on
# physical lines lets a second call hide -- `name/**/(c)` behind a comment, or
# `sysio_set_contract_\<newline>name(c)` behind a splice, both of which compile as an ordinary
# call. This is not a full preprocessor: it does not expand macros or paste tokens, which the
# generated dispatch does not use.
normalise_source() {
    python3 - "$1" <<'PYEOF'
import re, sys
src = open(sys.argv[1]).read()
src = src.replace('\\\n', '')        # phase 2: line splicing, before anything else
out, i, n = [], 0, len(src)
while i < n:
    c = src[i]
    if c == '"' or c == "'":                       # string / char literal: copy verbatim
        q = c; out.append(c); i += 1
        while i < n:
            out.append(src[i])
            if src[i] == '\\': 
                i += 2
                if i <= n: out.append(src[i-1])
                continue
            if src[i] == q: i += 1; break
            i += 1
        continue
    if src.startswith('//', i):
        while i < n and src[i] != '\n': i += 1
        continue
    if src.startswith('/*', i):
        j = src.find('*/', i + 2)
        out.append(' ')                            # a comment is whitespace, not nothing
        i = (j + 2) if j != -1 else n
        continue
    out.append(c); i += 1
print(''.join(out))
PYEOF
}

# Decide whether one dispatch file satisfies the contract. Echoes OK, or a reason.
check_dispatch() {
    local file="$1" clean
    clean="$(mktemp)"
    normalise_source "$file" > "$clean"

    # Every occurrence of the identifier, however spelled. Two are expected: the declaration
    # in the extern "C" block and the single call inside apply(). Counting matching LINES, or
    # only `identifier(`, both let a second call hide.
    local occurrences
    occurrences="$(grep -owE 'sysio_set_contract_name' "$clean" | wc -l)"
    if [ "$occurrences" -ne 2 ]; then
        echo "expected 2 occurrences of sysio_set_contract_name (1 declaration + 1 call), found ${occurrences}"
        rm -f "$clean"; return 1
    fi

    local apply_line
    apply_line="$(grep -nE '^[[:space:]]*(__attribute__.*)?void apply\(' "$clean" | head -1 | cut -d: -f1 || true)"
    if [ -z "$apply_line" ]; then
        echo "no apply() definition found"
        rm -f "$clean"; return 1
    fi

    # The body from immediately after the opening brace, INCLUDING any suffix on the signature
    # line, joined into one line. Reading from the next line down misses
    # `void apply(...) { if (c == r) {`.
    local body first_stmt
    body="$(awk -v a="$apply_line" '
        NR <  a { next }
        NR == a { sub(/^[^{]*\{/, "") }
        { print }
    ' "$clean" | tr '\n' ' ' | sed 's/[[:space:]][[:space:]]*/ /g; s/^ //')"
    first_stmt="${body%%;*};"
    rm -f "$clean"

    # Normalise spacing so `set (r)` and `set(r)` compare alike.
    local normalised
    normalised="$(printf '%s' "$first_stmt" | tr -d ' ')"
    if [ "$normalised" != "sysio_set_contract_name(r);" ]; then
        echo "first statement of apply() is: ${first_stmt}"
        return 1
    fi
    echo OK
}

echo "=== Dispatch Receiver Tests ==="

# --- 1. the real generated dispatch --------------------------------------------------------
cat > "${WORK}/c.cpp" <<'EOF'
#include <sysio/sysio.hpp>
class [[sysio::contract("dispatchrcv")]] dispatchrcv : public sysio::contract {
public:
   using contract::contract;
   [[sysio::action]] void go() {}
   [[sysio::on_notify("sysio.token::transfer")]] void onxfer(sysio::name from, sysio::name to) {}
};
EOF

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

# `|| true`: check_dispatch returns non-zero on rejection, and under `set -e` the assignment
# would take that status and abort the script -- turning a real failure into a truncated run
# instead of a reported one.
verdict="$(check_dispatch "$DISPATCH" || true)"
if [ "$verdict" = OK ]; then
    pass "the generated apply() records the receiver once, before any dispatch"
else
    fail "the generated apply() records the receiver once, before any dispatch"
    echo "    ${verdict}"
    sed 's/^/      /' "$DISPATCH"
fi

# --- 2. the checker itself -----------------------------------------------------------------
#
# Each of these compiles and each breaks the contract. Every one defeated some earlier
# revision of this test, so they are kept as regressions on the CHECKER.
# $2 is appended directly after the opening brace, so a fixture can put text on the SIGNATURE
# line by starting without a newline. mkbad used to emit one unconditionally, which meant no
# counterexample ever exercised that escape even though the header claimed one did.
mkbad() {   # $1=name  $2=apply-body (leading newline optional)
    cat > "${WORK}/bad_$1.cpp" <<EOF
extern "C" {
  void sysio_set_contract_name(uint64_t n);
  void __sysio_action_go_x(uint64_t r, uint64_t c);
  void __sysio_notify_on_x(uint64_t r, uint64_t c);
  void apply(uint64_t r, uint64_t c, uint64_t a) {$2
  }
}
EOF
}

mkbad code_not_receiver '
    sysio_set_contract_name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkbad inside_branch     '
    if (c == r) { sysio_set_contract_name(r); __sysio_action_go_x(r, c); }
    else { __sysio_notify_on_x(r, c); }'
mkbad double_call       '
    sysio_set_contract_name(r); sysio_set_contract_name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkbad comment_split     '
    sysio_set_contract_name(r); sysio_set_contract_name/**/(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A branch opened on the SIGNATURE line, with the setter first on the line below. This is the
# shape that discriminates: read the body from the brace and the first statement is
# `if (c == r) {`, so it is rejected; read it from the next line down -- as an earlier revision
# did -- and the setter looks like the first statement and it is accepted. A fixture whose
# signature line also closes its branch is rejected either way and pins nothing.
mkbad signature_line    ' if (c == r) {
    sysio_set_contract_name(r);
    __sysio_action_go_x(r, c);
  } else { __sysio_notify_on_x(r, c); }'
# Split across a phase-2 line splice, which compiles as one identifier.
mkbad spliced_call      '
    sysio_set_contract_name(r); sysio_set_contract_\
name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkbad missing_entirely  '
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkbad after_dispatch    '    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }
    sysio_set_contract_name(r);'

for bad in code_not_receiver inside_branch signature_line double_call comment_split \
           spliced_call missing_entirely after_dispatch; do
    verdict="$(check_dispatch "${WORK}/bad_${bad}.cpp" || true)"
    if [ "$verdict" = OK ]; then
        fail "the checker rejects: ${bad}"
        echo "    accepted a dispatch that breaks the contract"
        sed 's/^/      /' "${WORK}/bad_${bad}.cpp"
    else
        pass "the checker rejects: ${bad}"
    fi
done

# ...and must not reject a well-formed one that merely looks unusual.
mkbad spaced_ok '
    sysio_set_contract_name (r);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
verdict="$(check_dispatch "${WORK}/bad_spaced_ok.cpp" || true)"
if [ "$verdict" = OK ]; then
    pass "the checker accepts: a space before the paren"
else
    fail "the checker accepts: a space before the paren"
    echo "    ${verdict}"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
