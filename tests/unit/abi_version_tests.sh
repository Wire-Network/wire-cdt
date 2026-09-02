#!/bin/bash
# Test ABI version and protobuf_types generation
# Usage: abi_version_tests.sh <build_dir> [source_dir] [magic_enum_include_dir]
set -euo pipefail

BUILD_DIR="$1"
SOURCE_DIR="${2:-}"
MAGIC_ENUM_INC="${3:-}"
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
pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }

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

# The magic_enum include directory is passed in from CMake, which already resolves it.
# Searching for it here found `vcpkg_installed/<triplet>/share/magic_enum` as readily as
# the include/ one -- find(1) does not order its matches -- and the share copy has no
# headers under it.
PB_SRC="${SOURCE_DIR:-}"
PB_GEN="${BUILD_DIR}/tests/unit/test_contracts"

if [ -z "$PB_SRC" ] || [ ! -f "${PB_SRC}/tests/unit/test_contracts/pb_tests.cpp" ] \
   || [ ! -d "${PB_GEN}/test" ] \
   || [ -z "$MAGIC_ENUM_INC" ] || [ ! -f "${MAGIC_ENUM_INC}/magic_enum/magic_enum.hpp" ]; then
    echo "  SKIP: protobuf inputs not locatable in this build tree"
else
    if "$CDT_CPP" -abigen -abi-version 1.1 -contract pb_tests \
          -protobuf-dir "${PB_SRC}/tests/unit/test_contracts" -protobuf-files test.proto \
          -I "$PB_GEN" -I "${PB_SRC}/tests/unit/test_contracts" -I "$MAGIC_ENUM_INC" \
          "-abigen_output=${WORK}/pb11.abi" "${PB_SRC}/tests/unit/test_contracts/pb_tests.cpp" \
          -o "${WORK}/pb11.wasm" > "${WORK}/pb11.log" 2>&1; then
        check "1.1 + protobuf is promoted to 1.3" \
            "${WORK}/pb11.abi" '"version": "sysio::abi/1.3"'
        # Must key on result_type: "name": "hiproto" also appears in the top-level actions
        # array, so asserting the name passed even when the old late-promotion path had
        # suppressed action_results entirely -- i.e. it passed on the regression it exists
        # to catch. result_type appears only under action_results.
        check "1.1 + protobuf keeps the non-void action's result" \
            "${WORK}/pb11.abi" '"result_type": "protobuf::test.ActResult"'
    else
        fail "1.1 + protobuf builds"
        sed 's/^/    /' "${WORK}/pb11.log"
    fi
fi

# --- mixed-version descriptor merge -----------------------------------------------
#
# Sections enter the format at a version, so a valid 1.1 descriptor omits action_results.
# Once the capability gate consults the MERGED version, such a descriptor merged with a
# newer one reaches merge_action_results, and indexing the older side unconditionally threw
# `Key 'action_results' not found` -- failing the very mixed-version case the gate enables.
# Driven through `cdt-codegen --finalize`, which is the real ABIMerger entry point.
echo "-- mixed-version descriptor merge --"

CDT_CODEGEN="${BUILD_DIR}/bin/cdt-codegen"
MERGE_COMMON='"types":[],"tables":[],"ricardian_clauses":[],"variants":[],"abi_extensions":[],"pb_types":[],"wasm_actions":[],"wasm_entries":[],"wasm_notifies":[]'

cat > "${WORK}/old.desc" <<EOF
{"version":"sysio::abi/1.1","structs":[{"name":"acta","base":"","fields":[]}],"actions":[{"name":"acta","type":"acta","ricardian_contract":""}],${MERGE_COMMON}}
EOF
cat > "${WORK}/new.desc" <<EOF
{"version":"sysio::abi/1.10","structs":[{"name":"actb","base":"","fields":[]}],"actions":[{"name":"actb","type":"actb","ricardian_contract":""}],${MERGE_COMMON},"action_results":[{"name":"actb","result_type":"uint64"}]}
EOF

# Both merge orders. cdt-codegen sorts the descriptor paths, so the filenames -- not the
# --desc-file argument order -- decide which document becomes the accumulator. Naming them
# a_/b_ makes each case explicit instead of accidental.
# The seed version matters as much as the order. Without --abi-version the accumulator starts
# at the 1.2 default and the constructor gives it an empty action_results, so the only absent
# key is ever on the right -- a one-sided fallback would pass. Seeding older-first at 1.1
# makes the first merge emit an intermediate with no action_results at all, so the second
# merge meets the missing section on the LEFT, which is the case that actually regressed.
merge_case() {
    local label="$1" first="$2" second="$3" seed="$4"
    local dir="${WORK}/${label}"
    mkdir -p "$dir"
    cp "${WORK}/${first}.desc"  "${dir}/a_first.desc"
    cp "${WORK}/${second}.desc" "${dir}/b_second.desc"
    if "$CDT_CODEGEN" --finalize --contract mix --output-dir "$dir" \
          --abi-version "$seed" \
          --abi-output "${dir}/mix.abi" \
          --desc-file "${dir}/a_first.desc" --desc-file "${dir}/b_second.desc" \
          > "${dir}/mix.log" 2>&1; then
        check "${label}: emits the newer version" "${dir}/mix.abi" '"version": "sysio::abi/1.10"'
        # result_type, not name: "actb" is in the actions array too, so asserting the name
        # would pass even with action_results dropped entirely.
        check "${label}: retains the newer side's action_result" "${dir}/mix.abi" '"result_type": "uint64"'
    else
        fail "${label}: descriptors merge"
        sed 's/^/    /' "${dir}/mix.log"
    fi
}

merge_case "older-first" old new 1.1
merge_case "newer-first" new old 1.1

# --- matcher identity ---------------------------------------------------------------
#
# variant_is_same asked only whether every type in one variant appeared somewhere in the
# other, with no length check, so ["uint64"] and ["uint64","string"] compared equal. Merging
# them kept the accumulator's shorter list and dropped the `string` alternative outright --
# or, with the descriptors in the other order, failed the build with "v already defined".
# Which of the two you got was decided by sorted .desc filename order.
#
# struct_is_same matched fields by set membership plus size, so the same struct declared with
# reordered fields merged as identical and the alphabetically-first .desc silently won. ABI
# field order is serialization order, so that is a wire-layout change decided by a filename.
VAR_COMMON='"types":[],"tables":[],"ricardian_clauses":[],"abi_extensions":[],"pb_types":[],"wasm_actions":[],"wasm_entries":[],"wasm_notifies":[],"action_results":[]'

mkdesc_variant() { # $1=path $2=types-json
    cat > "$1" <<EOF
{"version":"sysio::abi/1.2","structs":[],"actions":[],"variants":[{"name":"v","types":$2}],${VAR_COMMON}}
EOF
}
mkdesc_struct() {  # $1=path $2=fields-json
    cat > "$1" <<EOF
{"version":"sysio::abi/1.2","structs":[{"name":"s","base":"","fields":$2}],"actions":[],"variants":[],${VAR_COMMON}}
EOF
}

# Two descriptors declaring the SAME name with DIFFERENT content are a genuine conflict, and
# the merge must refuse. What matters as much as the refusal is that it happens in BOTH
# orders: the defect was that one order refused and the other silently kept the accumulator's
# version, so the outcome of a build depended on sorted .desc filename order.
# $1=label $2=descA $3=descB
merge_refuses_both_orders() {
    local label="$1" a="$2" b="$3" order dir
    for order in ab ba; do
        dir="${WORK}/${label// /_}_${order}"; mkdir -p "$dir"
        if [ "$order" = ab ]; then
            cp "$a" "${dir}/a_first.desc"; cp "$b" "${dir}/b_second.desc"
        else
            cp "$b" "${dir}/a_first.desc"; cp "$a" "${dir}/b_second.desc"
        fi
        if "$CDT_CODEGEN" --finalize --contract mix --output-dir "$dir" \
              --abi-output "${dir}/mix.abi" \
              --desc-file "${dir}/a_first.desc" --desc-file "${dir}/b_second.desc" \
              > "${dir}/mix.log" 2>&1; then
            fail "${label} (${order} order merged instead of refusing)"
            sed 's/^/    /' "${dir}/mix.abi"
        else
            pass "${label} (${order} order)"
        fi
    done
}

# A populated gated section PROMOTES the emitted version rather than being dropped. Master
# emitted `variants` unconditionally, so a 1.0 build produced a document stamped 1.0 that
# carried a section the format introduced at 1.1 -- inconsistent, not lossy. Gating the section
# on the requested version would have made it lossy instead, since the struct field keeps
# referencing `variant_uint64_string` after the array defining it is dropped. Promoting the
# stamp is the only option that is neither. Asserted end to end, because the interesting part
# is abigen and the merger agreeing.
promo_dir="${WORK}/promote_1_0"; mkdir -p "$promo_dir"
cat > "${promo_dir}/v.cpp" <<'EOF'
#include <sysio/sysio.hpp>
#include <variant>
#include <string>
using namespace sysio;
class [[sysio::contract]] v : public contract { public: using contract::contract;
   [[sysio::action]] void go(std::variant<uint64_t, std::string> p) { (void)p; }
};
EOF
if (cd "$promo_dir" && "${BUILD_DIR}/bin/cdt-cpp" -abigen -abigen_output=v.abi -contract=v \
        -abi-version 1.0 -o v.wasm v.cpp) > "${promo_dir}/build.log" 2>&1; then
    check "a variant at -abi-version 1.0 promotes to 1.1" \
        "${promo_dir}/v.abi" '"version": "sysio::abi/1.1"'
    check "the promoted document still defines the variant it references" \
        "${promo_dir}/v.abi" '"name": "variant_uint64_string"'
else
    fail "a variant at -abi-version 1.0 builds"
    sed 's/^/    /' "${promo_dir}/build.log"
fi

# "version" is the first key of every ABI this toolchain emits and ojson preserves insertion
# order, so assigning it after the sections moved it to the end of the object -- changing the
# bytes of every contract's ABI. The abigen-pass fixtures pin this too; asserted here as well
# because the merger is where the ordering is decided.
if [ "$(head -3 "${promo_dir}/v.abi" | grep -c '"version"')" -eq 1 ]; then
    pass "version stays the leading key of the emitted ABI"
else
    fail "version stays the leading key of the emitted ABI"
    sed 's/^/    /' <<< "$(head -3 "${promo_dir}/v.abi")"
fi

# --- merger: tables ------------------------------------------------------------------
#
# The merger's table path had NO coverage at all -- every merge fixture above uses
# "tables":[] -- so a regression there was invisible to ctest.
#
# The case that matters is a multi-file contract. abigen writes `secondary_indexes` only when
# non-empty and `key_names` only when the indexed instantiation is visible, so a translation
# unit that sees a table's [[sysio::table]] but not its `kv::index` instantiation emits a
# descriptor with those keys absent or empty. That must merge with the richer one, in either
# order, and keep the richer metadata. It must NOT depend on which .desc sorts first.
TBL_COMMON='"types":[],"actions":[],"ricardian_clauses":[],"variants":[],"abi_extensions":[],"pb_types":[],"wasm_actions":[],"wasm_entries":[],"wasm_notifies":[],"action_results":[],"structs":[]'

cat > "${WORK}/t_rich.desc" <<EOF
{"version":"sysio::abi/1.2",${TBL_COMMON},"tables":[{"name":"mytbl","type":"row","index_type":"i64","key_names":["id"],"key_types":["uint64"],"table_id":25830,"secondary_indexes":[{"name":"byowner","type":"name","table_id":37799}]}]}
EOF
# key_names/key_types empty AND secondary_indexes absent entirely -- an absent key reads as
# jsoncons null, whose .empty() is false, so this is the shape that regressed.
cat > "${WORK}/t_poor.desc" <<EOF
{"version":"sysio::abi/1.2",${TBL_COMMON},"tables":[{"name":"mytbl","type":"row","index_type":"i64","key_names":[],"key_types":[],"table_id":25830}]}
EOF

merge_table_case() {   # $1=label $2=firstdesc $3=seconddesc
    local label="$1" dir="${WORK}/${1// /_}"
    mkdir -p "$dir"
    cp "$2" "${dir}/a_first.desc"; cp "$3" "${dir}/b_second.desc"
    if "$CDT_CODEGEN" --finalize --contract mix --output-dir "$dir" \
          --abi-output "${dir}/mix.abi" \
          --desc-file "${dir}/a_first.desc" --desc-file "${dir}/b_second.desc" \
          > "${dir}/mix.log" 2>&1; then
        # Each of the three lists is copied independently, so each needs its own assertion.
        # Checking only key_names and secondary_indexes left key_types pinned by nothing:
        # dropping it from the merge loop kept every case green while "uint64" vanished.
        check "${label}: keeps key_names"          "${dir}/mix.abi" '"id"'
        check "${label}: keeps key_types"          "${dir}/mix.abi" '"uint64"'
        check "${label}: keeps secondary_indexes"  "${dir}/mix.abi" '"byowner"'
    else
        fail "${label}: descriptors merge"
        sed 's/^/    /' "${dir}/mix.log"
    fi
}
merge_table_case "partial table desc, rich first" "${WORK}/t_rich.desc" "${WORK}/t_poor.desc"
merge_table_case "partial table desc, poor first" "${WORK}/t_poor.desc" "${WORK}/t_rich.desc"

# Split richness: each descriptor carries one optional list the other lacks. Replacing the
# accumulator wholesale -- as an earlier revision did on any of the three keys -- discarded
# whichever list the accumulator was richer in, so the result depended on which .desc sorted
# first. Merging per key gives the union either way.
cat > "${WORK}/t_keys_only.desc" <<EOF
{"version":"sysio::abi/1.2",${TBL_COMMON},"tables":[{"name":"mytbl","type":"row","index_type":"i64","key_names":["id"],"key_types":["uint64"],"table_id":25830}]}
EOF
cat > "${WORK}/t_idx_only.desc" <<EOF
{"version":"sysio::abi/1.2",${TBL_COMMON},"tables":[{"name":"mytbl","type":"row","index_type":"i64","key_names":[],"key_types":[],"table_id":25830,"secondary_indexes":[{"name":"byowner","type":"name","table_id":37799}]}]}
EOF
merge_table_case "split richness, keys first" "${WORK}/t_keys_only.desc" "${WORK}/t_idx_only.desc"
merge_table_case "split richness, indexes first" "${WORK}/t_idx_only.desc" "${WORK}/t_keys_only.desc"

# A genuinely different table_id is a different table, not a merge -- in both orders.
sed 's/25830/40000/' "${WORK}/t_rich.desc" > "${WORK}/t_otherid.desc"
merge_refuses_both_orders "tables with different table_ids conflict" \
    "${WORK}/t_rich.desc" "${WORK}/t_otherid.desc"

# A descriptor declaring a foreign ABI namespace must not produce one. Ordering ignores the
# prefix by design, so an eosio:: descriptor can be ingested -- but merge_version returned the
# winning document's RAW string, so the merged ABI was stamped eosio::abi/1.10, which the
# runtime's abi_serializer rejects outright. Nothing reached ABIMerger with a foreign prefix
# before this: the eosio:: fixture in abidiff_tests exercises only the differ.
printf '{"version":"sysio::abi/1.2",%s,"tables":[]}\n' "$TBL_COMMON" > "${WORK}/ns_local.desc"
printf '{"version":"eosio::abi/1.10",%s,"tables":[]}\n' "$TBL_COMMON" > "${WORK}/ns_foreign.desc"
ns_dir="${WORK}/ns"; mkdir -p "$ns_dir"
cp "${WORK}/ns_local.desc" "${ns_dir}/a_first.desc"
cp "${WORK}/ns_foreign.desc" "${ns_dir}/b_second.desc"
if "$CDT_CODEGEN" --finalize --contract ns --output-dir "$ns_dir" \
      --abi-output "${ns_dir}/ns.abi" \
      --desc-file "${ns_dir}/a_first.desc" --desc-file "${ns_dir}/b_second.desc" \
      > "${ns_dir}/ns.log" 2>&1; then
    check "a foreign ABI namespace is canonicalised on merge" \
        "${ns_dir}/ns.abi" '"version": "sysio::abi/1.10"'
    check_absent "the merged ABI carries no foreign namespace" \
        "${ns_dir}/ns.abi" 'eosio::abi'
else
    fail "a descriptor with a foreign namespace merges"
    sed 's/^/    /' "${ns_dir}/ns.log"
fi

mkdesc_variant "${WORK}/v_short.desc" '["uint64"]'
mkdesc_variant "${WORK}/v_long.desc"  '["uint64","string"]'
merge_refuses_both_orders "variants of differing length conflict" \
    "${WORK}/v_short.desc" "${WORK}/v_long.desc"

mkdesc_struct "${WORK}/s_ab.desc" '[{"name":"a","type":"uint64"},{"name":"b","type":"string"}]'
mkdesc_struct "${WORK}/s_ba.desc" '[{"name":"b","type":"string"},{"name":"a","type":"uint64"}]'
merge_refuses_both_orders "reordered struct fields conflict" \
    "${WORK}/s_ab.desc" "${WORK}/s_ba.desc"

# A descriptor missing a REQUIRED section is truncated, not merely older, and must be
# rejected rather than merged as empty -- otherwise contract interface content is dropped
# silently. Baseline sections are required at every version; `variants` and `action_results`
# are required from the version that introduced them (1.1 and 1.2) and may be absent only
# below it; `enums` alone is optional everywhere, being emitted only when non-empty.
cat > "${WORK}/truncated.desc" <<EOF
{"version":"sysio::abi/1.2","structs":[],"types":[],"tables":[],"ricardian_clauses":[],"variants":[],"abi_extensions":[],"pb_types":[],"wasm_actions":[],"wasm_entries":[],"wasm_notifies":[],"action_results":[]}
EOF
if "$CDT_CODEGEN" --finalize --contract trunc --output-dir "$WORK" \
      --abi-output "${WORK}/trunc.abi" --desc-file "${WORK}/truncated.desc" \
      > "${WORK}/trunc.log" 2>&1; then
    fail "a descriptor missing a required section is rejected"
else
    pass "a descriptor missing a required section is rejected"
fi

# Each validation rule needs its own case: without these, reverting either the versionless
# rejection or the per-version thresholds leaves the suite green.
#
# $1 label, $2 expected outcome (accept|reject), $3 seed version, $4 descriptor JSON,
# $5 expected diagnostic fragment when rejecting.
validation_case() {
    local label="$1" expect="$2" seed="$3" body="$4" diag="${5:-}"
    local dir="${WORK}/v_${label// /_}"
    mkdir -p "$dir"
    printf '%s\n' "$body" > "${dir}/d.desc"
    if "$CDT_CODEGEN" --finalize --contract vcase --output-dir "$dir" \
          --abi-version "$seed" --abi-output "${dir}/out.abi" \
          --desc-file "${dir}/d.desc" > "${dir}/log" 2>&1; then
        if [ "$expect" = "accept" ]; then pass "$label"; else fail "$label (accepted)"; fi
    else
        if [ "$expect" = "reject" ]; then
            if [ -z "$diag" ] || grep -q "$diag" "${dir}/log"; then
                pass "$label"
            else
                fail "$label (rejected, but not with the expected diagnostic)"
                sed 's/^/    /' "${dir}/log"
            fi
        else
            fail "$label (rejected)"
            sed 's/^/    /' "${dir}/log"
        fi
    fi
}

BASE='"structs":[],"types":[],"actions":[],"tables":[],"ricardian_clauses":[],"abi_extensions":[],"pb_types":[],"wasm_actions":[],"wasm_entries":[],"wasm_notifies":[]'

validation_case "a versionless descriptor is rejected" reject 1.2 \
    "{${BASE},\"variants\":[],\"action_results\":[]}" "missing its version"

validation_case "1.0 may omit variants and action_results" accept 1.0 \
    "{\"version\":\"sysio::abi/1.0\",${BASE}}"

validation_case "1.1 requires variants" reject 1.1 \
    "{\"version\":\"sysio::abi/1.1\",${BASE},\"action_results\":[]}" "missing section : variants"

validation_case "1.1 may omit action_results" accept 1.1 \
    "{\"version\":\"sysio::abi/1.1\",${BASE},\"variants\":[]}"

validation_case "1.2 requires action_results" reject 1.2 \
    "{\"version\":\"sysio::abi/1.2\",${BASE},\"variants\":[]}" "missing section : action_results"

echo ""
echo "Results: ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]