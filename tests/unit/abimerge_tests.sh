#!/bin/bash
# Regression tests for descriptor merging, driven through `cdt-codegen --finalize`.
#
# The merge decides link-wide facts from per-translation-unit descriptors, and two of its
# rules disagreed with each other. table_is_same() treats a MISSING optional key as
# compatible -- it has to, since a TU that sees a [[sysio::table]] without its instantiation
# has no table_id, no key layout and no row to record -- but the fill list that copies the
# richer value back in did not include ____row. So a rowless entry arriving first silently
# discarded the row marker the other side carried, the resolver read the annotation as
# uninstantiated, and it declared a phantom table beside the real one. Reversing the merge
# order produced the right answer, which is the signature of the bug.
#
# Descriptors are internal and never committed, but an incremental build can still mix
# vintages: the plugin is not a declared dependency of the codegen step, so an in-place
# toolchain upgrade leaves unchanged objects with descriptors an older abigen wrote. That is
# what these fixtures stand in for -- one descriptor with the marker, one without -- and it
# is why they are hand-written rather than compiled: the toolchain tester builds everything
# with one toolchain, and so can never produce the mixture.
#
# Usage: abimerge_tests.sh <bin_dir>
set -euo pipefail

BIN_DIR="$1"
CODEGEN="${BIN_DIR}/cdt-codegen"
PASS=0
FAIL=0

pass() { echo "  PASS: $1"; PASS=$((PASS + 1)); }
fail() { echo "  FAIL: $1"; FAIL=$((FAIL + 1)); }

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# Common body. $1 is the tables array.
desc() {
    cat <<EOF
{
  "____comment": "hand-written test descriptor",
  "version": "sysio::abi/1.2",
  "structs": [
    {"name": "config_row", "base": "", "fields": [{"name": "id", "type": "uint64"}]},
    {"name": "test", "base": "", "fields": []}
  ],
  "types": [],
  "actions": [{"name": "test", "type": "test", "ricardian_contract": ""}],
  "ricardian_clauses": [], "variants": [], "abi_extensions": [], "action_results": [],
  "wasm_actions": [], "wasm_notifies": [], "wasm_entries": [], "pb_types": [],
  "tables": [$1],
  "____table_annotations": [
    {"name": "cfg", "type": "config_row", "row": "config_row", "loc": "row.hpp:5:1",
     "key_names": [], "key_types": []}
  ]
}
EOF
}

ROWLESS='{"name": "cfgtbl", "type": "config_row", "index_type": "i64",
          "key_names": ["scope", "primary_key"], "key_types": ["name", "uint64"]}'
MARKER='{"name": "cfgtbl", "type": "config_row", "index_type": "i64",
         "key_names": ["scope", "primary_key"], "key_types": ["name", "uint64"],
         "table_id": 49879, "____row": "config_row"}'

# cdt-codegen sorts its descriptor list before merging, so that ABI output does not depend on
# directory iteration order. Merge order is therefore FILENAME order, and that -- not the order
# the --desc-file options are written in -- is what these names control. An earlier draft of
# this test passed both orders on the broken code for exactly that reason.
mkdir -p "$WORK/rowless_first" "$WORK/marker_first"
desc "$ROWLESS" > "$WORK/rowless_first/1_old.desc"
desc "$MARKER"  > "$WORK/rowless_first/2_new.desc"
desc "$MARKER"  > "$WORK/marker_first/1_new.desc"
desc "$ROWLESS" > "$WORK/marker_first/2_old.desc"

# The annotation renames the one table over config_row, whichever order the descriptors
# merge in. `cfgtbl` surviving means the marker was lost; `cfg` AND `cfgtbl` together means
# it was lost and a phantom declared beside the real table.
check_order() {
    local label="$1"
    local out="$WORK/${label}.abi"
    rm -f "$out"
    if ! "$CODEGEN" --finalize --contract merge_probe \
            --desc-file "$WORK/${label}/1_"*.desc --desc-file "$WORK/${label}/2_"*.desc \
            --abi-output "$out" 2>"$WORK/${label}.err"; then
        fail "${label}: cdt-codegen exited non-zero"
        sed 's/^/      /' "$WORK/${label}.err"
        return
    fi
    local names
    names="$(python3 -c "
import json, sys
d = json.load(open(sys.argv[1]))
print(','.join(sorted('%s:%s' % (t['name'], t.get('table_id')) for t in d.get('tables', []))))
" "$out")"
    if [ "$names" = "cfg:49879" ]; then
        pass "${label}: renamed to cfg, table_id kept"
    else
        fail "${label}: expected cfg:49879, got ${names:-<none>}"
    fi
}

echo "Descriptor merge order"
check_order rowless_first
check_order marker_first

echo ""
echo "  ${PASS} passed, ${FAIL} failed"
[ "$FAIL" -eq 0 ]
