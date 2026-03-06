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
check "action param uses protobuf type prefix" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"type": "protobuf::test.ActData"'
check "action result uses protobuf type prefix" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"result_type": "protobuf::test.ActResult"'
check "proto syntax is proto3" \
    "${CONTRACTS_DIR}/pb_tests.abi" \
    '"syntax": "proto3"'

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]