# Skelly EQ V2.0 — release checks

This is a full **source** update with a stable-editor fallback, not a verified FL Studio binary.

## Windows / GitHub Actions

1. Put the **contents** of the `SkellyEQ` folder at the root of your repository.
2. Commit/push, then run `.github/workflows/build.yml` (or use `cmake -S . -B build -A x64` with MSVC).
3. The build workflow creates a `SkellyEQ-V2.0-Windows-VST3` artifact and runs portable DSP tests.
4. Fully close FL Studio. Back up V1.4 or your last working VST3. Install only one Skelly EQ VST3, rescan, and verify the editor displays `v2.0`.

## Required FL Studio acceptance tests

- Load into a blank project, close and reopen editor 10 times, remove plugin, insert again.
- Save/reopen project with A/B snapshots, edited curve, graph range, and band selection.
- Play sound and check filters at 44.1, 48 and 96 kHz, buffers 64/128/512.
- Change frequency, gain and Q while playing; perform fast sweeps at high Q; verify no spikes, NaNs, or crashes.
- Toggle bypass, Solo, routing, L/R–M/S, cutoff slopes, analyzer and phase selection.
- Resize minimum/maximum UI and check that graph nodes/side panel/bottom knobs do not overlap.
- Compare analyzer Off vs On idle CPU and check that close/reopen clears stale analyzer data.

## Known limitations

- `phaseMode=Linear` is **not** linear-phase FIR; UI labels `LINEAR N/A`. Modes share the min-phase DSP path.
- No oversampling or advanced Nyquist frequency matching, multiband dynamic EQ, sidechain, loudness metering, or equal-loudness matching.
- Filter-type/topology changes are not dual-bank crossfaded.
- Product polish is a work in progress; V1.4 is the confirmed FL Studio safe baseline.

## Validation executed for this source package

- Portable production-header DSP smoke tests: pass (g++, C++17).
- ZIP CRC check and art assets checks: performed during packaging.
- Full JUCE and Windows MSVC compilation: **not performed** in this environment.
- FL Studio runtime check: **not performed**.
