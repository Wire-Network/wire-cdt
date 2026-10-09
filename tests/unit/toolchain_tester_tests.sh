#!/bin/bash
# Regression tests for how the toolchain test runner (tools/toolchain-tester) judges a cdt-cpp
# run.
#
# The runner recorded a crashed compiler as a passing test. Python's subprocess reports a child
# killed by signal N as return code -N, and Test.handle_test_result failed a *-pass case only on
# `returncode > 0` and a *-fail case only on `returncode == 0`. A cdt-cpp that segfaulted was
# therefore a success in a *-pass case and "the expected failure" in a *-fail case, and the case
# passed unless one of its other expectations happened to object. The one a case uses to say
# "must succeed" never did: Test.handle_expecteds tested `expected.get("exit-code")` for truth,
# so "exit-code": 0 was not compared with anything.
#
# Both are fixed. A cdt-cpp killed by a signal fails the case in every kind, with a message
# naming the signal; a *-pass case fails on any non-zero status; and "exit-code" is compared
# whenever the key is present.
#
# The REAL runner is driven end to end, one case per run, against a FAKE cdt-cpp -- a script
# that compiles nothing and ends the way the case's compile_flags tell it to. The cases live in
# a temporary tree laid out like tests/toolchain, because the runner takes a case's kind from
# the name of the directory it is in.
#
# Usage: toolchain_tester_tests.sh <toolchain_tester>
#   <toolchain_tester> is the launcher the build generates:
#   <build_dir>/tools/toolchain-tester/toolchain-tester
set -euo pipefail

TESTER="${1:?usage: toolchain_tester_tests.sh <toolchain_tester>}"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

# The runner's exit status for a run in which no case failed, and for one in which any did.
readonly RUNNER_PASSED=0
readonly RUNNER_FAILED=1
# Its summary line for a run of exactly one case, which passed.
readonly ONE_CASE_PASSED="100% of tests passed, 0 tests failed out of 1"
# Every test file written here holds a single case, which the runner names <file>_0.
readonly CASE_INDEX=0

[ -x "$TESTER" ] || { echo "FAIL: missing toolchain-tester launcher $TESTER"; exit 1; }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

FAKE_BIN="${WORK}/bin"     # the "CDT" the runner is pointed at: nothing but the fake cdt-cpp
TESTS="${WORK}/tests"      # the tests tree: one directory per kind, as in tests/toolchain
RUNNER_TMP="${WORK}/tmp"   # where the runner makes its work root, so that goes with $WORK too
mkdir -p "$FAKE_BIN" "$TESTS" "$RUNNER_TMP"

# The stand-in for cdt-cpp. It ignores the kind's own flags and the source path the runner
# passes it; the flags a case adds through its compile_flags decide how it ends:
#
#   --fake-stderr=TEXT   print TEXT on stderr
#   --fake-exit=N        exit with status N (0 when not given)
#   --fake-signal=NAME   die of signal NAME, spelled the way `kill -s` takes it
cat > "${FAKE_BIN}/cdt-cpp" <<'EOF'
#!/bin/bash
exit_status=0
signal=""
for arg in "$@"; do
    case "$arg" in
        --fake-stderr=*) printf '%s\n' "${arg#--fake-stderr=}" >&2 ;;
        --fake-exit=*)   exit_status="${arg#--fake-exit=}" ;;
        --fake-signal=*) signal="${arg#--fake-signal=}" ;;
    esac
done
if [ -n "$signal" ]; then
    # No core file: where core dumps are enabled, dying of SIGSEGV would leave one behind.
    ulimit -c 0
    kill -s "$signal" $$
    # Reached only if the signal was ignored or blocked when this script started.
    echo "fake cdt-cpp: still running after SIG${signal}" >&2
    exit 1
fi
exit "$exit_status"
EOF
chmod +x "${FAKE_BIN}/cdt-cpp"

# add_case <suite> <case> <test JSON>: write a single-case test file the way a test author
# would -- <case>.json beside <case>.cpp, in the directory of its kind. <test JSON> is the one
# element of the file's "tests" array.
add_case() {
    local suite="$1" name="$2" json="$3"
    mkdir -p "${TESTS}/${suite}"
    echo "// Never read: the fake cdt-cpp compiles nothing." > "${TESTS}/${suite}/${name}.cpp"
    printf '{ "tests": [ %s ] }\n' "$json" > "${TESTS}/${suite}/${name}.json"
}

# run_case <suite> <case>: run that one case through the real runner, on one worker. Sets
# `runner_status` to the runner's exit status and `out` to its output with the colour codes
# removed.
run_case() {
    runner_status=0
    out="$(TMPDIR="$RUNNER_TMP" "$TESTER" "$TESTS" --cdt "$FAKE_BIN" -j 1 \
               -t "${1}/${2}_${CASE_INDEX}" 2>&1)" || runner_status=$?
    out="$(sed $'s/\033\\[[0-9;]*m//g' <<< "$out")"
}

# expect_pass <description> <suite> <case> <test JSON>: the runner must report the case as
# passed.
expect_pass() {
    local desc="$1" suite="$2" name="$3"
    add_case "$suite" "$name" "$4"
    run_case "$suite" "$name"
    if [ "$runner_status" -eq "$RUNNER_PASSED" ] && grep -qxF "$ONE_CASE_PASSED" <<< "$out"; then
        pass "$desc"
    else
        fail "${desc} (runner exit status ${runner_status}; the case was not reported as passed)"
        sed 's/^/      /' <<< "$out"
    fi
}

# expect_fail <description> <suite> <case> <test JSON> <text>...: the runner must report the
# case as failed, and its output must contain every <text> -- the part of the failure message
# that says why. The exit status alone would not do: the runner exits the same way when it dies
# of an exception of its own, having judged nothing.
expect_fail() {
    local desc="$1" suite="$2" name="$3" text
    add_case "$suite" "$name" "$4"
    shift 4
    run_case "$suite" "$name"
    if [ "$runner_status" -ne "$RUNNER_FAILED" ] ||
       ! grep -qF "Failure: ${suite}/${name}_${CASE_INDEX} failed with message:" <<< "$out"; then
        fail "${desc} (runner exit status ${runner_status}; the case was not reported as failed)"
        sed 's/^/      /' <<< "$out"
        return
    fi
    for text in "$@"; do
        if ! grep -qF -- "$text" <<< "$out"; then
            fail "${desc} (the failure message lacks \"${text}\")"
            sed 's/^/      /' <<< "$out"
            return
        fi
    done
    pass "$desc"
}

# Signal numbers as this platform assigns them.
SIGSEGV_NUM="$(kill -l SEGV)"
SIGKILL_NUM="$(kill -l KILL)"

echo "=== toolchain-tester Tests ==="

# Each kind has a class of its own in tests.py, so the rules that turn on the kind are run
# through all six.
for family in compile build abigen; do
    echo ""
    echo "${family}-pass and ${family}-fail"

    expect_pass "${family}-pass passes when cdt-cpp exits 0" \
        "${family}-pass" exit_0 '{ "compile_flags": ["--fake-exit=0"] }'
    expect_fail "${family}-pass fails when cdt-cpp exits 1" \
        "${family}-pass" exit_1 '{ "compile_flags": ["--fake-exit=1"] }' \
        "failed with the following stderr"
    expect_fail "${family}-pass fails when cdt-cpp is killed by SIGSEGV" \
        "${family}-pass" segv '{ "compile_flags": ["--fake-signal=SEGV"] }' \
        "killed by signal ${SIGSEGV_NUM} (SIGSEGV)"

    expect_pass "${family}-fail passes when cdt-cpp exits 1" \
        "${family}-fail" exit_1 '{ "compile_flags": ["--fake-exit=1"] }'
    expect_fail "${family}-fail fails when cdt-cpp exits 0" \
        "${family}-fail" exit_0 '{ "compile_flags": ["--fake-exit=0"] }' \
        "expected to fail compilation/linking but didn't."
    # No "expected" block, so the old runner had nothing left to object with: this was not an
    # exit 0, hence "the expected failure".
    expect_fail "${family}-fail fails when cdt-cpp is killed by SIGSEGV" \
        "${family}-fail" segv '{ "compile_flags": ["--fake-signal=SEGV"] }' \
        "killed by signal ${SIGSEGV_NUM} (SIGSEGV)"
done

echo ""
echo "Signals"

# What the kernel's OOM killer sends: nothing runs in the victim, nothing reaches stderr.
expect_fail "a case fails when cdt-cpp is killed by SIGKILL" \
    compile-pass sigkill '{ "compile_flags": ["--fake-signal=KILL"] }' \
    "killed by signal ${SIGKILL_NUM} (SIGKILL)"

expect_fail "the failure names the signal and carries what cdt-cpp wrote to stderr" \
    compile-pass segv_stderr \
    '{ "compile_flags": ["--fake-stderr=PLEASE submit a bug report", "--fake-signal=SEGV"] }' \
    "killed by signal ${SIGSEGV_NUM} (SIGSEGV)" \
    "with the following stderr PLEASE submit a bug report"

# A signal Python has no name for is reported by its number alone. signal.Signals names the
# standard signals plus SIGRTMIN and SIGRTMAX, so a real-time signal between those two has
# none. Where there are no real-time signals (macOS) no such signal can be delivered, and
# there is nothing to check.
if UNNAMED_NUM="$(kill -l RTMIN+1 2>/dev/null)"; then
    expect_fail "a signal Python has no name for is reported by its number" \
        compile-pass unnamed_signal \
        '{ "compile_flags": ["--fake-stderr=crashed", "--fake-signal=RTMIN+1"] }' \
        "killed by signal ${UNNAMED_NUM} with the following stderr crashed"
else
    echo "  NOTE: no real-time signals on this platform, so none that Python cannot name"
fi

# The case the runner waved through even with an expectation in place: cdt-cpp printed the
# diagnostic the case is after and THEN crashed. The first check is the control -- it shows the
# expectation matches, which leaves the signal as the only reason the second one fails.
expect_pass "a *-fail case whose stderr expectation matches passes when cdt-cpp exits 1" \
    compile-fail diagnosed \
    '{ "compile_flags": ["--fake-stderr=error: diagnosed", "--fake-exit=1"],
       "expected": { "stderr": "error: diagnosed" } }'
expect_fail "...and fails when cdt-cpp is killed by SIGSEGV instead" \
    compile-fail diagnosed_then_crashed \
    '{ "compile_flags": ["--fake-stderr=error: diagnosed", "--fake-signal=SEGV"],
       "expected": { "stderr": "error: diagnosed" } }' \
    "killed by signal ${SIGSEGV_NUM} (SIGSEGV)"

echo ""
echo "\"exit-code\""

expect_pass "\"exit-code\": 0 passes a *-pass case that exits 0" \
    compile-pass exit_code_0 \
    '{ "compile_flags": ["--fake-exit=0"], "expected": { "exit-code": 0 } }'
expect_pass "\"exit-code\": 255 passes a *-fail case that exits 255" \
    compile-fail exit_code_255 \
    '{ "compile_flags": ["--fake-exit=255"], "expected": { "exit-code": 255 } }'
expect_fail "\"exit-code\": 255 fails a *-fail case that exits 1" \
    compile-fail exit_code_255_got_1 \
    '{ "compile_flags": ["--fake-exit=1"], "expected": { "exit-code": 255 } }' \
    "expected 255 exit code but got 1"

# The regression: 0 is falsy, so the comparison was skipped. A *-fail case that exits non-zero
# is the only place this shows. One that exits 0 fails for its kind before "exit-code" is
# looked at, and so does a *-pass case that exits anything else.
expect_fail "\"exit-code\": 0 is compared: it fails a *-fail case that exits 1" \
    compile-fail exit_code_0_got_1 \
    '{ "compile_flags": ["--fake-exit=1"], "expected": { "exit-code": 0 } }' \
    "expected 0 exit code but got 1"

# A signal is reported as a signal, not as an exit code that failed to match. These are the
# two shapes in which the real cases declare "exit-code".
expect_fail "a *-pass case declaring \"exit-code\": 0 fails when cdt-cpp is killed by SIGSEGV" \
    compile-pass exit_code_0_segv \
    '{ "compile_flags": ["--fake-signal=SEGV"], "expected": { "exit-code": 0 } }' \
    "killed by signal ${SIGSEGV_NUM} (SIGSEGV)"
expect_fail "a *-fail case declaring \"exit-code\": 255 names the signal that killed cdt-cpp" \
    compile-fail exit_code_255_segv \
    '{ "compile_flags": ["--fake-signal=SEGV"], "expected": { "exit-code": 255 } }' \
    "killed by signal ${SIGSEGV_NUM} (SIGSEGV)"

echo ""
echo "A crash the runner cannot see"

# cdt-cpp reports a subprogram that crashed with the status it uses for one that diagnosed an
# error, so no signal reaches the runner. What still catches it is the case's stderr
# expectation: the diagnostic the case is after is not there.
expect_fail "a *-fail case's stderr expectation rejects an exit 255 without the diagnostic" \
    compile-fail subprogram_crashed \
    '{ "compile_flags": ["--fake-stderr=cdt: failed to execute subprogram", "--fake-exit=255"],
       "expected": { "stderr": "error: diagnosed" } }' \
    "expected error: diagnosed stderr but got cdt: failed to execute subprogram"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
