#!/bin/bash
# Test ABI version and protobuf_types generation
# Usage: abi_version_tests.sh <build_dir>
set -euo pipefail

BUILD_DIR="$1"
CONTRACTS_DIR="${BUILD_DIR}/tests/unit/test_contracts"
PASS=0
FAIL=0

check() {
    local desc="$1" file="$2" pattern="$3"
    if grep -q "$pattern" "$file"; then
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    else
        echo "  FAIL: $desc"
        echo "    expected pattern: $pattern"
        echo "    in file: $file"
        FAIL=$((FAIL + 1))
    fi
}

fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

check_absent() {
    local desc="$1" file="$2" pattern="$3"
    if grep -q "$pattern" "$file" 2>/dev/null; then
        echo "  FAIL: $desc"
        echo "    pattern should NOT be present: $pattern"
        echo "    in file: $file"
        FAIL=$((FAIL + 1))
    else
        echo "  PASS: $desc"
        PASS=$((PASS + 1))
    fi
}

echo "=== ABI Version Tests ==="

# Non-protobuf contract should be ABI 1.2
echo "-- simple_tests (non-protobuf) --"
check "version is 1.2" \
    "${CONTRACTS_DIR}/simple_tests.abi" \
    '"version": "sysio::abi/1.2"'
check_absent "no protobuf_types section" \
    "${CONTRACTS_DIR}/simple_tests.abi" \
    '"protobuf_types"'

# Protobuf contract should be ABI 1.3
echo "-- pb_tests (protobuf) --"
check "version is 1.3" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"version": "sysio::abi/1.3"'
check "has protobuf_types section" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"protobuf_types"'
check "protobuf_types contains proto file metadata" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"package": "test"'
check "protobuf_types contains ActData message" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "ActData"'
check "protobuf_types contains ActResult message" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "ActResult"'
check "single-param action type is protobuf (flattened)" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"type": "protobuf::test.ActData"'
check "action result uses protobuf type prefix" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"result_type": "protobuf::test.ActResult"'
check_absent "single-param actions have no wrapper struct" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "hiproto".*"fields"'
# Multi-param protobuf action generates a wrapper struct
check "multi-param action has wrapper struct" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "pbmulti"'
check "multi-param wrapper has data field" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "data"'
check "multi-param wrapper has result field" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"name": "result"'
check "proto syntax is proto3" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"syntax": "proto3"'

# --- -abi-version parsing ------------------------------------------------------
#
# The driver used to derive the minor by float round-trip:
#
#     float tmp = std::stof(v); minor = (int)((tmp - (int)tmp) * 10);
#
# which truncates whenever the decimal has no exact binary expansion. "1.3"
# parsed as minor 2 and "1.4" as minor 3, so `-abi-version 1.3` silently emitted
# sysio::abi/1.2. Both driver and cdt-codegen now parse the components as
# integers from a single shared implementation (abi_version::parse). These cases
# pin that: they FAIL on the float arithmetic and pass on integer parsing.
echo "-- -abi-version parsing --"

CDT_CPP="${BUILD_DIR}/bin/cdt-cpp"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

cat > "${WORK}/verparse.cpp" <<'CONTRACT'
#include <sysio/sysio.hpp>
class [[sysio::contract("verparse")]] verparse : public sysio::contract {
public:
   using contract::contract;
   [[sysio::action]] void hi(sysio::name nm) { (void)nm; }
};
CONTRACT

check_emitted_version() {
    local requested="$1" expected="$2"
    local abi="${WORK}/v${expected}.abi"
    local desc="-abi-version ${requested} emits sysio::abi/${expected}"
    local args=(-abigen -contract verparse "-abigen_output=${abi}"
                "${WORK}/verparse.cpp" -o "${WORK}/v${expected}.wasm")
    [ -n "$requested" ] && args=(-abi-version "$requested" "${args[@]}")

    if ! "$CDT_CPP" "${args[@]}" > "${WORK}/build.log" 2>&1; then
        fail "$desc (build failed)"
        sed 's/^/    /' "${WORK}/build.log"
        return
    fi
    check "$desc" "$abi" "\"version\": \"sysio::abi/${expected}\""
}

check_emitted_version ""    "1.2"   # no flag -> the toolchain baseline
check_emitted_version "1.2" "1.2"
check_emitted_version "1.3" "1.3"   # float parse gave 1.2 here
check_emitted_version "1.4" "1.4"   # float parse gave 1.3 here

# A malformed version must be rejected with a diagnostic, not silently coerced.
if "$CDT_CPP" -abi-version "not-a-version" -abigen -contract verparse \
      "-abigen_output=${WORK}/bad.abi" "${WORK}/verparse.cpp" \
      -o "${WORK}/bad.wasm" > "${WORK}/bad.log" 2>&1; then
    fail "malformed -abi-version is rejected"
else
    if grep -q "invalid -abi-version" "${WORK}/bad.log"; then
        echo "  PASS: malformed -abi-version is rejected with a diagnostic"
        PASS=$((PASS + 1))
    else
        fail "malformed -abi-version rejected, but without the expected diagnostic"
        sed 's/^/    /' "${WORK}/bad.log"
    fi
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]