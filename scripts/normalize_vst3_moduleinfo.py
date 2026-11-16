#!/usr/bin/env python3
"""Normalize JUCE VST3 moduleinfo.json and validate the Skelly EQ identity."""
from __future__ import annotations
import argparse, json, re
from pathlib import Path
from typing import Any

PRODUCT = "Skelly EQ"
VENDOR = "Skelly Audio"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")


def normalized_json(source: str) -> str:
    out = []
    quoted = False
    escaped = False
    i = 0
    while i < len(source):
        char = source[i]
        if quoted:
            out.append(char)
            if escaped:
                escaped = False
            elif char == "\\":
                escaped = True
            elif char == '"':
                quoted = False
            i += 1
            continue
        if char == '"':
            quoted = True
            out.append(char)
            i += 1
            continue
        if char == ",":
            j = i + 1
            while j < len(source) and source[j].isspace():
                j += 1
            if j < len(source) and source[j] in "}]":
                i += 1
                continue
        out.append(char)
        i += 1
    fixed = "".join(out)
    json.loads(fixed)
    return fixed


def validate_metadata(data: Any) -> list[str]:
    if not isinstance(data, dict):
        raise ValueError("moduleinfo root must be a JSON object")
    if data.get("Name") != PRODUCT:
        raise ValueError(f"Unexpected product name: {data.get('Name')!r}")
    version = data.get("Version")
    if not isinstance(version, str) or not VERSION_RE.fullmatch(version):
        raise ValueError(f"Invalid semantic version: {version!r}")
    factory = data.get("Factory Info")
    if not isinstance(factory, dict) or factory.get("Vendor") != VENDOR:
        raise ValueError("Unexpected or missing vendor identity")
    classes = data.get("Classes")
    if not isinstance(classes, list) or not classes:
        raise ValueError("No VST3 classes")
    ids = []
    for index, entry in enumerate(classes):
        if not isinstance(entry, dict):
            raise ValueError(f"Class {index} is not an object")
        cid = entry.get("CID")
        if not isinstance(cid, str) or not CID_RE.fullmatch(cid):
            raise ValueError(f"Invalid CID at class {index}: {cid!r}")
        if entry.get("Name") != PRODUCT or entry.get("Vendor") != VENDOR:
            raise ValueError(f"Unexpected class identity at class {index}")
        ids.append(cid.lower())
    if len(set(ids)) != len(ids):
        raise ValueError("Duplicate VST3 class IDs")
    return ids


def self_test() -> None:
    samples = [
        ('{"value":[1,2,],"text":"literal ,} ,] here",}', {"value":[1,2],"text":"literal ,} ,] here"}),
        (r'{"literal":"escaped \" quote, }",}', {"literal": 'escaped " quote, }'}),
        ('{"valid":[1,2],"next":true}', {"valid":[1,2],"next":True}),
    ]
    for source, expected in samples:
        actual = json.loads(normalized_json(source))
        if actual != expected:
            raise AssertionError(f"Normalizer changed valid data: {actual!r}")
    print("PASS: string-aware metadata normalizer self-test")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("moduleinfo", type=Path, nargs="?")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()
    if args.self_test:
        self_test()
    if args.moduleinfo is None:
        if args.self_test:
            return
        parser.error("moduleinfo.json path is required")
    path = args.moduleinfo
    original = path.read_text(encoding="utf-8-sig")
    fixed = normalized_json(original)
    data = json.loads(fixed)
    ids = validate_metadata(data)
    if fixed != original:
        path.write_text(fixed, encoding="utf-8", newline="\n")
        print(f"NORMALIZED: {path}")
    else:
        print(f"PASS: strict JSON already valid: {path}")
    print(f"PASS: {len(ids)} unique VST3 classes; {PRODUCT} {data['Version']} / {VENDOR}")


if __name__ == "__main__":
    main()
