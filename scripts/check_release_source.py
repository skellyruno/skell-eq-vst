#!/usr/bin/env python3
"""Guard Skelly EQ's Windows VST3-only release identity and program naming."""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
processor = (ROOT / "Source" / "PluginProcessor.h").read_text(encoding="utf-8")
readme = (ROOT / "README.md").read_text(encoding="utf-8")

matches = re.findall(r"juce_add_plugin\s*\(\s*SkellyEQ\b(.*?)\)", cmake, flags=re.S)
if len(matches) != 1:
    raise SystemExit(f"Expected exactly one SkellyEQ JUCE target, found {len(matches)}")
block = matches[0]
formats = re.search(r"\bFORMATS\s+([A-Za-z0-9_]+)", block)
if not formats or formats.group(1) != "VST3" or re.search(r"\bStandalone\b", block):
    raise SystemExit("Release policy requires exactly FORMATS VST3")
for label, pattern in {
    "product name": r'PRODUCT_NAME\s+"Skelly EQ"',
    "bundle ID": r'BUNDLE_ID\s+"audio\.skelly\.eq"',
    "manufacturer code": r'PLUGIN_MANUFACTURER_CODE\s+SkAu',
    "plugin code": r'PLUGIN_CODE\s+SeQ1',
}.items():
    if not re.search(pattern, block):
        raise SystemExit(f"Protected VST3 {label} missing")
if not re.search(r"GIT_TAG\s+8\.0\.12\b", cmake):
    raise SystemExit("Expected pinned JUCE 8.0.12 FetchContent fallback")
if not re.search(r"getNumPrograms\s*\(\s*\)\s*override\s*\{\s*return\s+1\s*;\s*\}", processor):
    raise SystemExit("Expected one stable default VST3 program")
if not re.search(r'getProgramName\s*\(\s*int\s*\)\s*override\s*\{\s*return\s+juce::String\s*\(\s*"Default"\s*\)\s*;\s*\}', processor):
    raise SystemExit("VST3 program 0 must have the stable non-empty name 'Default'")
if "VST3/Standalone" in readme or "## macOS build" in readme:
    raise SystemExit("README still advertises a non-release target")
print("PASS: Windows VST3-only target, protected identity, JUCE 8.0.12 and named default program")
