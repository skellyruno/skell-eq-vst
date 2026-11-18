#!/usr/bin/env python3
"""Normalize JUCE VST3 moduleinfo.json and validate the Skelly EQ identity."""
from __future__ import annotations

import argparse
import json
import re
import tempfile
from pathlib import Path
from typing import Any

PRODUCT = "Skelly EQ"
VENDOR = "Skelly Audio"
VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:[-+][0-9A-Za-z.-]+)?$")
CID_RE = re.compile(r"^[0-9A-Fa-f]{32}$")


def _reject_non_finite(token: str) -> None:
    raise ValueError(f"Non-standard JSON numeric constant is not allowed: {token}")


def _unique_object(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise ValueError(f"Duplicate JSON object key is not allowed: {key!r}")
        result[key] = value
    return result


def strict_json_loads(source: str) -> Any:
    """Parse standard JSON while rejecting duplicate keys and NaN/Infinity."""
    return json.loads(
        source,
        object_pairs_hook=_unique_object,
        parse_constant=_reject_non_finite,
    )


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
    strict_json_loads(fixed)
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


def normalize_moduleinfo_file(path: Path) -> tuple[dict[str, Any], list[str], bool]:
    original = path.read_text(encoding="utf-8-sig")
    fixed = normalized_json(original)
    data = strict_json_loads(fixed)
    ids = validate_metadata(data)
    changed = fixed != original
    if changed:
        path.write_text(fixed, encoding="utf-8", newline="\n")
    return data, ids, changed


def self_test() -> None:
    samples = [
        ('{"value":[1,2,],"text":"literal ,} ,] here",}', {"value": [1, 2], "text": "literal ,} ,] here"}),
        (r'{"literal":"escaped \\" quote, }",}', {"literal": 'escaped " quote, }'}),
        ('{"valid":[1,2],"next":true}', {"valid": [1, 2], "next": True}),
    ]
    for source, expected in samples:
        actual = strict_json_loads(normalized_json(source))
        if actual != expected:
            raise AssertionError(f"Normalizer changed valid data: {actual!r}")

    rejected = [
        ('{"optional":NaN}', "NaN"),
        ('{"optional":Infinity}', "Infinity"),
        ('{"optional":-Infinity}', "-Infinity"),
        ('{"Name":"Skelly EQ","Name":"Skelly EQ"}', "duplicate top-level key"),
        ('{"nested":{"Vendor":"Skelly Audio","Vendor":"Other"}}', "duplicate nested key"),
    ]
    for source, label in rejected:
        try:
            strict_json_loads(source)
        except ValueError:
            pass
        else:
            raise AssertionError(f"Strict parser accepted {label}")

    valid_metadata = {
        "Name": PRODUCT,
        "Version": "2.7.0",
        "Factory Info": {"Vendor": VENDOR},
        "Classes": [{
            "CID": "0123456789abcdef0123456789abcdef",
            "Name": PRODUCT,
            "Vendor": VENDOR,
        }],
    }
    with tempfile.TemporaryDirectory(prefix="skelly-eq-moduleinfo-test-") as temp_dir:
        path = Path(temp_dir) / "moduleinfo.json"
        for invalid_constant in ("NaN", "Infinity", "-Infinity"):
            source = json.dumps(valid_metadata, separators=(",", ":"))[:-1] + ', "optional":' + invalid_constant + "}"
            path.write_text(source, encoding="utf-8")
            try:
                normalize_moduleinfo_file(path)
            except ValueError:
                pass
            else:
                raise AssertionError(f"Invalid metadata containing {invalid_constant} was accepted")
            if path.read_text(encoding="utf-8") != source:
                raise AssertionError("Rejected metadata was modified on disk")

        duplicate_source = (
            '{"Name":"Skelly EQ","Version":"2.7.0",'
            '"Factory Info":{"Vendor":"Skelly Audio","Vendor":"Other"},'
            '"Classes":[{"CID":"0123456789abcdef0123456789abcdef",'
            '"Name":"Skelly EQ","Vendor":"Skelly Audio"}]}'
        )
        path.write_text(duplicate_source, encoding="utf-8")
        try:
            normalize_moduleinfo_file(path)
        except ValueError:
            pass
        else:
            raise AssertionError("Duplicate-key metadata file was accepted")
        if path.read_text(encoding="utf-8") != duplicate_source:
            raise AssertionError("Duplicate-key metadata was modified on disk")

    print("PASS: string-aware normalization, duplicate-key rejection, non-finite rejection, and no-write-on-invalid tests")


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
    data, ids, changed = normalize_moduleinfo_file(args.moduleinfo)
    if changed:
        print(f"NORMALIZED: {args.moduleinfo}")
    else:
        print(f"PASS: strict JSON already valid: {args.moduleinfo}")
    print(f"PASS: {len(ids)} unique VST3 classes; {PRODUCT} {data['Version']} / {VENDOR}")


if __name__ == "__main__":
    main()
