#!/usr/bin/env python3
"""Merge multiple compile_commands.json files into one.

Usage:
    merge_compile_commands.py -o OUTPUT INPUT [INPUT ...]

Each INPUT is a path to a compile_commands.json file.  Entries are
de-duplicated by (directory, file) pair, with later files winning on
conflict.  Missing input files are silently skipped so that the merge
succeeds even when not all ExternalProjects have been built yet.
"""

import argparse
import json
import sys
from pathlib import Path


def main():
    parser = argparse.ArgumentParser(description="Merge compile_commands.json files")
    parser.add_argument("-o", "--output", required=True, help="Output file path")
    parser.add_argument("inputs", nargs="+", help="Input compile_commands.json files")
    args = parser.parse_args()

    output = Path(args.output)
    output_path = output.resolve(strict=False)
    input_paths = [Path(path_str) for path_str in args.inputs]
    external_roots = [
        p.parent.resolve(strict=False)
        for p in input_paths
        if p.resolve(strict=False) != output_path
    ]

    def is_under(path, roots):
        try:
            resolved = path.resolve(strict=False)
        except OSError:
            return False
        return any(resolved == root or root in resolved.parents for root in roots)

    def is_external_entry(entry):
        directory = entry.get("directory", "")
        file_name = entry.get("file", "")
        candidates = []
        if directory:
            candidates.append(Path(directory))
        if file_name:
            file_path = Path(file_name)
            candidates.append(file_path if file_path.is_absolute() else Path(directory) / file_path)
        return any(is_under(candidate, external_roots) for candidate in candidates)

    seen = {}  # (directory, file) -> entry
    for p in input_paths:
        if not p.exists():
            continue
        try:
            with open(p) as f:
                entries = json.load(f)
        except (json.JSONDecodeError, OSError):
            print(f"warning: skipping unreadable file {p}", file=sys.stderr)
            continue
        is_self_input = p.resolve(strict=False) == output_path
        for entry in entries:
            # The output file is also an input so root compile commands survive
            # repeated merges; filter external entries from that self-input so
            # removed ExternalProject sources do not linger forever.
            if is_self_input and is_external_entry(entry):
                continue
            key = (entry.get("directory", ""), entry.get("file", ""))
            seen[key] = entry

    merged = list(seen.values())

    # Only write if content actually changed to avoid unnecessary rebuilds
    new_content = json.dumps(merged, indent=2) + "\n"
    if output.exists():
        try:
            old_content = output.read_text()
            if old_content == new_content:
                return
        except OSError:
            pass

    output.parent.mkdir(parents=True, exist_ok=True)
    tmp_output = output.with_name(output.name + ".tmp")
    tmp_output.write_text(new_content)
    tmp_output.replace(output)


if __name__ == "__main__":
    main()
