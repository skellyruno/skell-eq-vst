#!/usr/bin/env python3
"""Normalize JUCE-generated VST3 moduleinfo.json to strict JSON.

Only removes trailing commas outside string literals. Never changes the VST3
binary. Raises on malformed metadata and duplicate class IDs.
"""
import argparse
import json
from pathlib import Path


def normalized_json(source: str) -> str:
    out = []
    quoted = False
    escaped = False
    for i, char in enumerate(source):
        if quoted:
            out.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            continue
        if char == '"':
            quoted = True
        if char == ",":
            j = i + 1
            while j < len(source) and source[j].isspace():
                j += 1
            if j < len(source) and source[j] in "}]":
                continue
        out.append(char)
    fixed = "".join(out)
    json.loads(fixed)
    return fixed


def self_test() -> None:
    samples = [
        ('{"value":[1, 2,],"text":"literal ,} ,] here",}',
         {"value": [1, 2], "text": "literal ,} ,] here"}),
        ('{"nested":{"ok":true,},"empty":[],"quote":"a\\\" ,}",}',
         {"nested":{"ok":True},"empty":[],"quote":'a" ,}'}),
        ('{"valid":[1,2],"next":true}', {"valid":[1,2],"next":True}),
    ]
    for src, expected in samples:
        actual = json.loads(normalized_json(src))
        if actual != expected:
            raise AssertionError(f"metadata scanner changed valid data: {actual}")
    print("PASS: strict JSON metadata normalization self-test")


def main() -> None:
    cli = argparse.ArgumentParser(description=__doc__)
    cli.add_argument("moduleinfo", type=Path, nargs="?")
    cli.add_argument("--self-test", action="store_true")
    args = cli.parse_args()
    if args.self_test:
        self_test()
    if args.moduleinfo is None:
        if args.self_test:
            return
        cli.error("VST3 moduleinfo.json path is required")
    original = args.moduleinfo.read_text(encoding="utf-8-sig")
    normalized = normalized_json(original)
    data = json.loads(normalized)
    if not isinstance(data.get("Classes"), list) or not data["Classes"]:
        raise ValueError("VST3 moduleinfo must list at least one plugin class")
    ids = [entry["CID"] for entry in data["Classes"]]
    if len(set(ids)) != len(ids):
        raise ValueError("Duplicate plugin class IDs in VST3 moduleinfo")
    if normalized != original:
        args.moduleinfo.write_text(normalized, encoding="utf-8", newline="\n")
        print(f"Fixed strict JSON in {args.moduleinfo}")
    else:
        print(f"PASS: metadata already valid strict JSON: {args.moduleinfo}")
    print(f"PASS: {len(ids)} unique VST3 plugin classes")


if __name__ == "__main__":
    main()
