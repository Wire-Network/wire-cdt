#!/bin/bash
# Test ABI version and protobuf_types generation
# Usage: abi_version_tests.sh <build_dir> [source_dir]
set -euo pipefail

BUILD_DIR="$1"
SOURCE_DIR="${2:-}"
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

check_emitted_version ""     "1.2"   # no flag -> the toolchain baseline
check_emitted_version "1.2"  "1.2"
check_emitted_version "1.3"  "1.3"   # float parse gave 1.2 here
check_emitted_version "1.4"  "1.4"   # float parse gave 1.3 here
check_emitted_version "1.10" "1.10"  # two-digit minor: see below

# A two-digit minor exercised three separate parsers, each of which got it wrong:
# the driver's stof/modf, the abigen plugin's stof/modf, and ABIMerger deriving the
# version from the string's last three characters (".10" -> 0.10). The last one also
# silently dropped action_results, because 0.10*10 failed its >= 12 gate. All three
# now share abi_version::parse / parse_version_string, so the section must survive.
check "1.10 keeps action_results (ABIMerger no longer parses the suffix)" \
    "${WORK}/v1.10.abi" \
    '"action_results"'

# There is no ABI 0.x, and cdt-cpp reads a zero major as "option absent" -- accepting
# one would reopen the driver/codegen divergence. Must be rejected, not coerced.
# 2.0 and 10.2 are rejected rather than accepted: abigen's to_json only serializes
# action_results when major == 1, so a higher major would be stamped onto an ABI missing
# the sections that version implies, and the merger would rank it above 1.2 regardless.
for bad in "not-a-version" "0.1" "1.2.3" "1x" "2.0" "10.2"; do
    if "$CDT_CPP" -abi-version "$bad" -abigen -contract verparse \
          "-abigen_output=${WORK}/bad.abi" "${WORK}/verparse.cpp" \
          -o "${WORK}/bad.wasm" > "${WORK}/bad.log" 2>&1; then
        fail "-abi-version ${bad} is rejected"
    elif grep -q "invalid -abi-version" "${WORK}/bad.log"; then
        echo "  PASS: -abi-version ${bad} is rejected with a diagnostic"
        PASS=$((PASS + 1))
    else
        fail "-abi-version ${bad} rejected, but without the expected diagnostic"
        sed 's/^/    /' "${WORK}/bad.log"
    fi
done

# --- protobuf version promotion ---------------------------------------------------
#
# A contract with protobuf files is stamped at 1.3. That promotion used to happen after
# the abigen plugin had already run, so `-abi-version 1.1` made the plugin suppress
# action_results under its own 1.2 gate, and codegen then stamped the incomplete output
# as 1.3 -- a version promising a section the descriptors no longer carried. The
# promotion now happens before the plugin is told the version, so a non-void protobuf
# action keeps its result entry.
echo "-- protobuf version promotion --"

PB_SRC="${SOURCE_DIR:-}"
PB_GEN="${BUILD_DIR}/tests/unit/test_contracts"
MAGIC_ENUM_DIR="$(find "${BUILD_DIR}/vcpkg_installed" -maxdepth 3 -type d -name magic_enum 2>/dev/null | head -1)"

if [ -z "$PB_SRC" ] || [ ! -f "${PB_SRC}/tests/unit/test_contracts/pb_tests.cpp" ] \
   || [ ! -d "${PB_GEN}/test" ] || [ -z "$MAGIC_ENUM_DIR" ]; then
    echo "  SKIP: protobuf inputs not locatable in this build tree"
else
    if "$CDT_CPP" -abigen -abi-version 1.1 -contract pb_tests \
          -protobuf-dir "${PB_SRC}/tests/unit/test_contracts" -protobuf-files test.proto \
          -I "$PB_GEN" -I "${PB_SRC}/tests/unit/test_contracts" -I "$(dirname "$MAGIC_ENUM_DIR")" \
          "-abigen_output=${WORK}/pb11.abi" "${PB_SRC}/tests/unit/test_contracts/pb_tests.cpp" \
          -o "${WORK}/pb11.wasm" > "${WORK}/pb11.log" 2>&1; then
        check "1.1 + protobuf is promoted to 1.3" \
            "${WORK}/pb11.abi" '"version": "sysio::abi/1.3"'
        check "1.1 + protobuf keeps the non-void action's result" \
            "${WORK}/pb11.abi" '"name": "hiproto"'
    else
        fail "1.1 + protobuf builds"
        sed 's/^/    /' "${WORK}/pb11.log"
    fi
fi

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]