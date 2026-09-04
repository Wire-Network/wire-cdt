#!/bin/bash
# The generated apply() must record the RECEIVER in sysio_contract_name.
#
# multi_index's receiving_account() reads that global and falls back to the current_receiver
# intrinsic only when it is 0, so the guards on emplace/modify/erase are only correct if the
# dispatcher stores `r` (the receiver) rather than `c` (the code), exactly once, before any
# dispatch. Every in-tree action is self-sent, so r == c and the whole unit + integration suite
# stays green if that is broken -- the divergence appears only under notification, on chain.
# This pins it at the source, which no other test inspects, in two independent ways.
#
# check_dispatch reads the preprocessed dispatch TEXT: it is what can see that the argument is
# `r` and not `c`, and that the call is the first statement. check_dispatch_symbols reads the
# RELOCATIONS of the emitted object: it is what can see a second call however it was spelled,
# including one reaching the import through an asm label that never spells the identifier
# twice. Neither subsumes the other, so both run.
#
# Each is exercised three ways: against the real generated dispatch, against a table of crafted
# counterexamples that must each be rejected, and against positive controls that must NOT be.
# Earlier revisions were defeated seven times in review -- by handler names the checker did not
# match, by a branch on the signature line, by two calls on one line, by a comment between the
# identifier and its paren, by a raw string closing at column 1, by a marker whose filename
# carried an escaped quote, and by that asm label -- because each fix pattern-matched the last
# evasion. Checking the checker is what stops that: a new evasion is one row below, not a round
# trip.
#
# The marker filter is pinned from BOTH sides. Too narrow and it leaves a marker in the
# normalised source, which is read as apply()'s first statement and rejects a correct dispatch;
# too broad and it deletes a line of real code, taking a second setter call with it. Each of
# the four parts of that pattern -- the `^`, the filename grammar, the trailing flags and the
# `$` -- has a row that fails when it alone is weakened, positive rows for the first sense and
# counterexamples for the second.
#
# The checker reports three outcomes, not two, and callers distinguish all three: accepted,
# rejected, and INFRA_ERROR -- the check could not be performed. Collapsing the third into
# either verdict is how a broken toolchain reads as a green run.
#
# Usage: dispatch_receiver_tests.sh <build_dir>
set -euo pipefail

BUILD_DIR="$1"
CDT_CPP="${BUILD_DIR}/bin/cdt-cpp"
LLVM_OBJDUMP="${BUILD_DIR}/bin/llvm-objdump"
PASS=0
FAIL=0
pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# check_dispatch's exit statuses. 0 is acceptance; these two are not interchangeable.
readonly REJECTED=1      # the dispatch was read, and it breaks the contract
readonly INFRA_ERROR=2   # the dispatch could not be read at all -- no verdict was reached

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# The COMPLETE preprocessor line-marker grammar, anchored at BOTH ends:
#
#     # <line> "<file>" [<flag>...]
#
# with the filename modelled as clang emits it -- any character except an unescaped quote or
# backslash, or a backslash followed by anything. Every part of that is load-bearing, and each
# was added after a shape that a narrower filter got wrong:
#
#   * dropping every '#'-prefixed line deleted `#)d"; <call>` -- a raw string closing at
#     column 1 -- and the executable code on that line with it;
#   * dropping `#` followed by a digit deleted `#1)d"; <call>` the same way;
#   * `[^"]*` for the filename does not match `# 7 "a\"b.cpp"`, which is what cdt-cpp -E emits
#     for `#line 7 "a\"b.cpp"`. The marker then survives into the normalised source and is
#     read as the start of apply()'s first statement, rejecting a CORRECT dispatch;
#   * without the trailing `$`, `# 1 "fake")d"; <call>` matches on its marker-shaped prefix
#     and the line -- call included -- is deleted.
#
# Matching the whole grammar leaves any line that is not literally a marker intact.
readonly LINE_MARKER_RE='^# [0-9]+ "([^"\\]|\\.)*"([[:space:]]+[0-9]+)*$'

# Preprocess $1 into $2, with the driver's diagnostics captured in $3, and strip line markers.
#
# Normalise through the CDT DRIVER, so the tokens counted are the ones that will ship. Earlier
# revisions used the bundled clang++ with the host target and deleted #include lines first.
# Both choices changed the translation unit: the host target evaluates `#ifdef __wasm__` the
# wrong way, so a second setter call guarded on it was invisible while cdt-cpp compiled it
# happily; and dropping includes erases any macro that expands to one. cdt-cpp applies the
# wasm32 target, the CDT include graph and the same predefined macros as the real compile.
#
# -E emits line markers and the driver rejects -P, so they are stripped afterwards. `pipefail`
# is set, so the pipeline reports the driver's status and the caller can tell a preprocessing
# failure from a verdict.
normalise_source() {
    "$CDT_CPP" -E "$1" 2>"$3" | sed -E "/${LINE_MARKER_RE}/d" > "$2"
}

# Decide whether one dispatch file satisfies the contract. Echoes OK, or a reason.
# Returns 0 (accepted), $REJECTED, or $INFRA_ERROR.
check_dispatch() {
    local file="$1" clean pp_log pp_status=0
    clean="$(mktemp "${WORK}/clean.XXXXXX")"
    pp_log="$(mktemp "${WORK}/pplog.XXXXXX")"

    # An explicit status check, because every call site runs this function on the left of a
    # `||` -- which disables errexit for its whole body. Without this, a driver that failed
    # after printing something plausible was analysed anyway: acceptable-looking output read
    # as OK, and truncated output read as a rejection, which in the counterexample loop below
    # is indistinguishable from a PASS.
    normalise_source "$file" "$clean" "$pp_log" || pp_status=$?
    if [ "$pp_status" -ne 0 ]; then
        echo "preprocessing ${file} exited ${pp_status}: $(tr '\n' ' ' < "$pp_log")"
        return "$INFRA_ERROR"
    fi

    # Every occurrence of the identifier, however spelled. Two are expected: the declaration
    # in the extern "C" block and the single call inside apply(). Counting matching LINES, or
    # only `identifier(`, both let a second call hide.
    local occurrences
    occurrences="$(grep -owE 'sysio_set_contract_name' "$clean" | wc -l)"
    if [ "$occurrences" -ne 2 ]; then
        echo "expected 2 occurrences of sysio_set_contract_name (1 declaration + 1 call), found ${occurrences}"
        return "$REJECTED"
    fi

    local apply_line
    apply_line="$(grep -nE '^[[:space:]]*(__attribute__.*)?void apply\(' "$clean" | head -1 | cut -d: -f1 || true)"
    if [ -z "$apply_line" ]; then
        echo "no apply() definition found"
        return "$REJECTED"
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

    # Normalise spacing so `set (r)` and `set(r)` compare alike.
    local normalised
    normalised="$(printf '%s' "$first_stmt" | tr -d ' ')"
    if [ "$normalised" != "sysio_set_contract_name(r);" ]; then
        echo "first statement of apply() is: ${first_stmt}"
        return "$REJECTED"
    fi
    echo OK
}

# The same requirement at the SYMBOL level, over the object the driver actually emits.
#
# The text checker counts SPELLINGS, and a second call can reach the same wasm import without
# adding one:
#
#     extern void again(uint64_t) __asm__("sysio_set_" "contract_name");
#     again(c);
#
# The adjacent string literals are still two tokens after preprocessing -- concatenation is
# translation phase 6, which -E does not reach -- so the identifier is spelled twice in the
# file and the text count stays at 2. The object calls the import twice, and the second call
# overwrites the receiver with the code. Relocations do not care how the symbol was spelled.
#
# Ordering comes with it: the first call relocation inside apply() must be this one, which
# states "before any dispatch" over the emitted code rather than over the source text.
#
# Scope is apply() itself, matching the text checker. A setter call made from some OTHER
# function that apply() calls is out of range of both -- the dispatch TU defines only apply(),
# so there is no such function to write today, but it is a real limit rather than a covered
# case.
#
# Echoes OK, or a reason. Returns 0 or $REJECTED.
check_dispatch_symbols() {   # $1=object file
    local relocs count first
    relocs="$("$LLVM_OBJDUMP" -dr "$1" 2>/dev/null | awk '
        /^[0-9a-f]+ <.*>:$/ { in_apply = ($0 ~ /<apply>:$/); next }
        in_apply && /R_WASM_FUNCTION_INDEX_LEB/ {
            sym = $NF; sub(/\+[0-9]+$/, "", sym); print sym
        }')"
    if [ -z "$relocs" ]; then
        echo "no call relocations inside apply() in $(basename "$1")"
        return "$REJECTED"
    fi
    count="$(printf '%s\n' "$relocs" | grep -cx 'sysio_set_contract_name' || true)"
    if [ "$count" -ne 1 ]; then
        echo "apply() calls sysio_set_contract_name ${count} time(s), not once"
        return "$REJECTED"
    fi
    first="$(printf '%s\n' "$relocs" | head -1)"
    if [ "$first" != sysio_set_contract_name ]; then
        echo "the first call in apply() is ${first}, not sysio_set_contract_name"
        return "$REJECTED"
    fi
    echo OK
}

# Run check_dispatch on $1, setting VERDICT to its reason and VERDICT_STATUS to its status.
#
# The `||` is what keeps a non-zero status from aborting the script under errexit -- turning a
# reported failure into a truncated run -- while still recording which status it was. Reading
# only the text cannot tell a rejection from an infrastructure error.
VERDICT=""
VERDICT_STATUS=0
run_check() {
    VERDICT_STATUS=0
    VERDICT="$(check_dispatch "$1")" || VERDICT_STATUS=$?
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

run_check "$DISPATCH"
if [ "$VERDICT_STATUS" -eq 0 ]; then
    pass "the generated apply() records the receiver once, before any dispatch"
else
    fail "the generated apply() records the receiver once, before any dispatch"
    echo "    ${VERDICT}"
    sed 's/^/      /' "$DISPATCH"
fi

# --- 2. the checker itself -----------------------------------------------------------------
#
# Each fixture compiles. The ones in the negative table each break the contract and every one
# defeated some earlier revision of this test, so they are kept as regressions on the CHECKER;
# the ones in the positive table are correct dispatches that merely look unusual, and pin the
# other direction -- a filter tightened until it rejects real output.
# $2 is appended directly after the opening brace, so a fixture can put text on the SIGNATURE
# line by starting without a newline. This used to emit one unconditionally, which meant no
# counterexample ever exercised that escape even though the header claimed one did.
mkfixture() {   # $1=name  $2=apply-body (leading newline optional)
    cat > "${WORK}/fixture_$1.cpp" <<EOF
typedef unsigned long long uint64_t;   // not <cstdint>: keeps the preprocessed fixture small
                                       // enough to read when a failure dumps it
extern "C" {
  void sysio_set_contract_name(uint64_t n);
  void __sysio_action_go_x(uint64_t r, uint64_t c);
  void __sysio_notify_on_x(uint64_t r, uint64_t c);
  void apply(uint64_t r, uint64_t c, uint64_t a) {$2
  }
}
EOF
}

mkfixture code_not_receiver '
    sysio_set_contract_name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkfixture inside_branch     '
    if (c == r) { sysio_set_contract_name(r); __sysio_action_go_x(r, c); }
    else { __sysio_notify_on_x(r, c); }'
mkfixture double_call       '
    sysio_set_contract_name(r); sysio_set_contract_name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkfixture comment_split     '
    sysio_set_contract_name(r); sysio_set_contract_name/**/(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A branch opened on the SIGNATURE line, with the setter first on the line below. This is the
# shape that discriminates: read the body from the brace and the first statement is
# `if (c == r) {`, so it is rejected; read it from the next line down -- as an earlier revision
# did -- and the setter looks like the first statement and it is accepted. A fixture whose
# signature line also closes its branch is rejected either way and pins nothing.
mkfixture signature_line    ' if (c == r) {
    sysio_set_contract_name(r);
    __sysio_action_go_x(r, c);
  } else { __sysio_notify_on_x(r, c); }'
# Split across a phase-2 line splice, which compiles as one identifier.
mkfixture spliced_call      '
    sysio_set_contract_name(r); sysio_set_contract_\
name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A backslash followed by horizontal whitespace before the newline. Clang splices it (with a
# warning), so this is a second call; a normaliser matching only an adjacent backslash-newline
# does not see it.
mkfixture spliced_ws        '
    sysio_set_contract_name(r); sysio_set_contract_\   
name(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A raw string whose contents look like a comment. A hand-rolled lexer treats the opening quote
# as an ordinary string and the // inside it as a comment, erasing the real call after it.
mkfixture raw_string_comment '
    sysio_set_contract_name(r);
    const char* s = R"d(" // )d"; sysio_set_contract_name(c); (void)s;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# Guarded on the target. The host branch is well-formed, so a checker preprocessing for the
# host counts two occurrences and accepts -- while cdt-cpp compiles the wasm branch, where the
# receiver is immediately overwritten with the code.
mkfixture target_conditional '
#ifdef __wasm__
    sysio_set_contract_name(r); sysio_set_contract_name(c);
#else
    sysio_set_contract_name(r);
#endif
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# The second call arrives from a macro defined in an INCLUDED header -- the shape that a
# checker deleting #include lines cannot see, however well it expands what remains.
cat > "${WORK}/record_again.hpp" <<'EOF'
#pragma once
#define RECORD_AGAIN sysio_set_contract_name(c)
EOF
mkfixture macro_expanded '
#include "record_again.hpp"
    sysio_set_contract_name(r); RECORD_AGAIN;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A multiline raw string closing at column 1 after a '#', with the second call on that same
# line. cdt-cpp compiles it and -E emits both calls; a filter that drops every '#'-prefixed
# line deletes the closing delimiter and the call with it.
mkfixture raw_string_hash '
    sysio_set_contract_name(r);
    const char* s = R"d(
#)d"; sysio_set_contract_name(c);
    (void)s;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# The same raw-string escape, with a digit after the '#'. A filter matching `#` plus a numeric
# prefix deletes this closing line and the call on it.
mkfixture raw_string_hash_num '
    sysio_set_contract_name(r);
    const char* s = R"d(
#1)d"; sysio_set_contract_name(c);
    (void)s;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# The same escape again, but closing on a line whose PREFIX is a complete, well-formed line
# marker. This is what pins the trailing `$`: a filter anchored only at the start matches the
# `# 1 "fake"` prefix, deletes the line, and takes the second call with it -- so the run stays
# green with the anchor removed unless this row is here. The two rows above do not cover it;
# they only pin that the old '#'-prefix filters were too broad.
mkfixture raw_string_marker '
    sysio_set_contract_name(r);
    const char* s = R"d(
# 1 "fake")d"; sysio_set_contract_name(c);
    (void)s;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A marker-shaped SUFFIX: a raw string OPENING on the same line as the second call, whose
# remainder is a well-formed marker. This pins the leading `^`. Without it the line matches on
# its tail, sed deletes the whole line, and the second call goes with it -- 3 occurrences drop
# to 2 and the checker accepts. Every other raw-string row closes at column 1, so none of them
# can pin the start anchor.
mkfixture marker_tail '
    sysio_set_contract_name(r);
    sysio_set_contract_name(c); const char* s = R"z(# 1 "a"
)z";
    (void)s;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# The filename model from ABOVE, where marker_escaped_ok only pins it from below. A closing
# delimiter, the second call, and a later quoted string all on one line: model the filename as
# `.*` and the greedy match runs from the first quote to the last, swallowing the call. The
# real grammar stops at the unescaped quote that ends the filename, so the line is not a marker
# and survives intact.
mkfixture marker_greedy '
    sysio_set_contract_name(r);
    const char* s = R"d(
# 1 "x)d"; sysio_set_contract_name(c); const char* t = "y"
    ;
    (void)s; (void)t;
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkfixture missing_entirely  '
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkfixture after_dispatch    '    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }
    sysio_set_contract_name(r);'

# The positive controls: correct dispatches whose text is awkward.
mkfixture spaced_ok '
    sysio_set_contract_name (r);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# A #line directive whose filename carries an ESCAPED QUOTE, immediately before the setter.
# cdt-cpp -E re-emits it as the legal marker `# 7 "a\"b.cpp"`; a filter modelling the filename
# as `[^"]*` cannot match that, leaves the marker in the normalised source, and then reads it
# as the start of apply()'s first statement -- rejecting a dispatch that is correct.
mkfixture marker_escaped_ok '
#line 7 "a\"b.cpp"
    sysio_set_contract_name(r);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
# An #include immediately before the setter. CDT emits an ENTER and a RETURN marker for it --
# `# 1 "./empty_header.hpp" 1` and `# N "fixture.cpp" 2` -- between the opening brace and the
# first statement, which is the only shape that pins the trailing `([[:space:]]+[0-9]+)*`:
# drop that group and neither line is a marker any more, both survive normalisation, and the
# first is read as apply()'s first statement. Every other marker in these fixtures is
# flagless, so nothing else covers it.
cat > "${WORK}/empty_header.hpp" <<'HDREOF'
#pragma once
HDREOF
# Reaches the import through an asm label whose spelling is split across two string literals,
# so no second contiguous `sysio_set_contract_name` appears in the preprocessed text. Compiles
# under the driver; the text checker ACCEPTS it, which is the whole reason section 4 exists.
mkfixture asm_label '
    sysio_set_contract_name(r);
    extern void again(uint64_t) __asm__("sysio_set_" "contract_name");
    again(c);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'
mkfixture marker_flags_ok '
#include "empty_header.hpp"
    sysio_set_contract_name(r);
    if (c == r) { __sysio_action_go_x(r, c); } else { __sysio_notify_on_x(r, c); }'

# Compiled by the DRIVER, not a host clang: a fixture must be legal in the translation unit
# that actually ships, and the driver supplies the wasm32 target and the CDT include graph.
# (`-c` to an object we discard; the driver has no -fsyntax-only.) Returns non-zero if the
# fixture did not compile, having already reported the failure itself.
compile_fixture() {
    if ( cd "$WORK" && "$CDT_CPP" -c "fixture_$1.cpp" -o "fixture_$1.o" ) \
            > "${WORK}/fixture_$1.log" 2>&1; then
        return 0
    fi
    fail "fixture compiles: $1"
    sed 's/^/      /' "${WORK}/fixture_$1.log"
    return 1
}

# Decide whether one counterexample was CAUGHT, which is the outcome the table requires:
# rejected, having been read. Returns 0 for that, and non-zero -- echoing why -- for either
# other outcome. Acceptance is the obvious failure; an infrastructure error is the quiet one,
# and folding it in with `-ne 0` would report a full green sweep on a machine where the driver
# cannot run at all. Section 3 pins that this distinction is made HERE, at the call site, and
# not only inside check_dispatch.
classify_counterexample() {   # $1=fixture file
    run_check "$1"
    if [ "$VERDICT_STATUS" -eq "$REJECTED" ]; then
        return 0
    fi
    if [ "$VERDICT_STATUS" -eq 0 ]; then
        echo "accepted a dispatch that breaks the contract:"
        sed 's/^/  /' "$1"
    else
        echo "no verdict was reached: ${VERDICT}"
    fi
    return 1
}

for bad in code_not_receiver inside_branch signature_line double_call comment_split \
           spliced_call spliced_ws raw_string_comment raw_string_hash raw_string_hash_num \
           raw_string_marker marker_tail marker_greedy target_conditional macro_expanded \
           missing_entirely after_dispatch; do
    compile_fixture "$bad" || continue
    reason=""; caught=0
    reason="$(classify_counterexample "${WORK}/fixture_${bad}.cpp")" || caught=$?
    if [ "$caught" -eq 0 ]; then
        pass "the checker rejects: ${bad}"
    else
        fail "the checker rejects: ${bad}"
        printf '%s\n' "$reason" | sed 's/^/    /'
    fi
done

# ...and must not reject a well-formed one that merely looks unusual.
for good in spaced_ok marker_escaped_ok marker_flags_ok; do
    compile_fixture "$good" || continue
    run_check "${WORK}/fixture_${good}.cpp"
    if [ "$VERDICT_STATUS" -eq 0 ]; then
        pass "the checker accepts: ${good}"
    else
        fail "the checker accepts: ${good}"
        echo "    ${VERDICT}"
    fi
done

# --- 3. a failing preprocessor is an infrastructure error, not a verdict --------------------
#
# Stand in for the driver with something that prints, then fails. Both shapes below used to be
# reported as verdicts, because check_dispatch never looked at the status: plausible output
# read as acceptance, and truncated output read as a rejection -- which, in the loop above,
# reads as a PASS on a machine where the toolchain is broken.
cat > "${WORK}/fake_cdt_cpp" <<'EOF'
#!/bin/bash
# Prints a canned payload and exits with a canned status, both read from files beside it, so
# one stand-in covers every shape of preprocessor failure.
cat "$(dirname "$0")/fake_pp_out"
exit "$(cat "$(dirname "$0")/fake_pp_status")"
EOF
chmod +x "${WORK}/fake_cdt_cpp"
printf '73\n' > "${WORK}/fake_pp_status"

# Output that WOULD be accepted, so only the status can distinguish it.
cat > "${WORK}/fake_pp_out" <<'EOF'
extern "C" {
  void sysio_set_contract_name(unsigned long long n);
  void apply(unsigned long long r, unsigned long long c, unsigned long long a) {
    sysio_set_contract_name(r);
  }
}
EOF

real_cdt_cpp="$CDT_CPP"
CDT_CPP="${WORK}/fake_cdt_cpp"
for shape in acceptable_output truncated_output; do
    [ "$shape" = truncated_output ] && : > "${WORK}/fake_pp_out"
    run_check "${WORK}/c.cpp"
    if [ "$VERDICT_STATUS" -eq "$INFRA_ERROR" ]; then
        pass "a failing preprocessor reaches no verdict: ${shape}"
    else
        fail "a failing preprocessor reaches no verdict: ${shape}"
        echo "    status ${VERDICT_STATUS}: ${VERDICT}"
    fi

    # ...and the counterexample table must not read that as a catch. This is the half that a
    # status alone does not buy: every row above reports a PASS for any non-zero status unless
    # the call site separates the two, so a driver that cannot run would sweep the table green.
    reason=""; caught=0
    reason="$(classify_counterexample "${WORK}/fixture_code_not_receiver.cpp")" || caught=$?
    if [ "$caught" -ne 0 ]; then
        pass "a counterexample is not counted as caught: ${shape}"
    else
        fail "a counterexample is not counted as caught: ${shape}"
        echo "    the table reported a catch though no verdict was reached"
    fi
done
CDT_CPP="$real_cdt_cpp"

# --- 4. the same requirement over the emitted object ---------------------------------------
#
# Exercised the way the text checker is: against the real generated dispatch, against a
# positive control, and against the counterexample the text checker cannot see.
run_symbols() {   # $1=label  $2=object  $3=expect: accept|reject
    local verdict status=0
    verdict="$(check_dispatch_symbols "$2")" || status=$?
    if [ "$3" = accept ] && [ "$status" -eq 0 ]; then
        pass "the symbol check accepts: $1"
    elif [ "$3" = reject ] && [ "$status" -ne 0 ]; then
        pass "the symbol check rejects: $1"
    else
        fail "the symbol check ${3}s: $1"
        echo "    ${verdict}"
    fi
}

# The real dispatch, compiled on its own: the contract build above already links it, but the
# object is what carries the relocations.
if ( cd "$WORK" && "$CDT_CPP" -c "$DISPATCH" -o real_dispatch.o ) > "${WORK}/real.log" 2>&1; then
    run_symbols "the generated dispatch" "${WORK}/real_dispatch.o" accept
else
    fail "the generated dispatch compiles on its own"
    sed 's/^/      /' "${WORK}/real.log"
fi

run_symbols "a space before the paren" "${WORK}/fixture_spaced_ok.o" accept

# The setter present exactly once but AFTER the dispatch. Its object is already built by the
# counterexample loop, and it is what pins the "first call" branch -- without it, deleting that
# branch leaves this section green.
run_symbols "the setter after the dispatch" "${WORK}/fixture_after_dispatch.o" reject

# A compile failure here must not read as the rejection this row expects, so the check only
# runs once the object exists.
if compile_fixture asm_label; then
    run_symbols "an asm label reaching the same import" "${WORK}/fixture_asm_label.o" reject
fi

# ...and the text checker really does miss that one, which is why both run. Reported rather
# than asserted: a future text checker strong enough to catch it should not fail this suite.
run_check "${WORK}/fixture_asm_label.cpp"
if [ "$VERDICT_STATUS" -eq 0 ]; then
    echo "  NOTE: the text checker accepts asm_label, as expected -- only the symbol check sees it"
else
    echo "  NOTE: the text checker now also rejects asm_label (${VERDICT})"
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
