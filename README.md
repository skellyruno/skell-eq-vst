# Skelly EQ V2.2 — UI refinement, graph correctness, reliability checks (2026-11-06)

**Default window: 1070 × 529.** Same compact size as V2.1 (40% smaller in each dimension than the original 1784 × 882 artwork canvas). Plugin manufacturer and product IDs, parameter IDs, and saved-state layout are unchanged.

Changes from V2.1:

- **Honest response graph:** the EQ contour now respects active Solo bands and each band’s L/R or M/S lane assignment. The bright green line is the dB-average of the two processed lanes, with faint amber/cyan lines when the lane responses differ; routing logic is shared with the processor.
- **Nyquist visualization:** at low sample rates, the graph marks unavailable high frequencies rather than showing an aliased response; the analyzer does not invent FFT magnitudes above Nyquist.
- **Predictable band dragging:** choosing the nearest overlapping node (preferring the selected node), proper continuous host gestures when switching between frequency/gain and Alt+Q, and formatted hover tooltips.
- **A/B feedback:** temporary non-modal A/B save/recall status including a clear EMPTY status when no snapshot exists.
- **Micro-polish:** selected-band type and Solo state, higher-contrast plot instructions, clarified output-meter labels, hover cursors, and better redraw coverage.
- **CI/tests:** added `scripts/check_v22.py` validating layout control boundaries and collisions, source invariants, resources, and fallback behavior. Extended portable DSP tests for lane-routing in stereo and M/S. Updated GitHub Actions artifact names.

**Important:** True linear-phase convolution is still **not implemented**. The Linear choice remains in the saved parameter schema for compatibility but the audio DSP remains minimum-phase. This V2.2 source has passed local portable DSP and static checks, **not** a Windows/MSVC JUCE build or an FL Studio runtime test. Keep your last known-working build for rollback.

### Test checklist in FL Studio

1. Close FL Studio. Install the freshly built V2.2 `.vst3` while avoiding duplicate old plugin bundles; rescan if needed.
2. Open a *blank project*. Add/remove Skelly EQ and open/close its window at least 10 times. Save/reopen the project. Verify the window remains 1070×529 initially.
3. Move a band, scroll Q, switch Alt+drag and regular drag mid-gesture, then automate Frequency/Gain/Q and check recorded host automation.
4. Try Left/Right and Mid/Side routing with Solo, turn bands on/off, and inspect the separated response curves. Check the analyzer at 44.1, 48 and 96 kHz.
5. Save A/B, recall both, and try loading an empty snapshot; use the safe editor fallback if the restored UI crashes (`-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON`).

### Portable checks (no JUCE download needed)

```sh
python scripts/check_v22.py
c++ -std=c++17 -O2 -I tests/juce_stub tests/dsp_smoke.cpp -o skelly_dsp_smoke
./skelly_dsp_smoke
```

---

# Skelly EQ V2.1 — Compact interface (2026-10-28)

**Default window:** 1070 × 529 (40% smaller in each dimension than the original 1784 × 882 reference). The rebuilt UI uses native compact text/control sizes. **Full MSVC/FL Studio build remains unverified.** See [`V210_COMPACT_UI_NOTES.md`](V210_COMPACT_UI_NOTES.md) for the changes and test steps.

---

# Skelly EQ V2.0 — DSP and GUI refinement (2026-10-27)

**Build status:** Portable DSP smoke tests pass with g++ on Linux. A full JUCE build, MSVC compilation, VST3 validation, and FL Studio runtime testing are **not available in this environment**. Treat V2.0 as a source test release. V1.4's simple editor remains the fallback build.

## Changes from 1.9

- **Continuous biquad coefficient ramps:** frequency/gain/Q automation ramps filter coefficients over approximately 8 ms, independently of DAW block size; changing routing, type, slope, or enable state clears stale filter memory. No allocations or FFT work added to the real-time filter loop.
- **Fixed a Solo edge case:** bypassed bands marked Solo no longer mute other bands.
- **True Tilt response:** the existing Tilt type now uses a complementary low-shelf / high-shelf pair (one boosts and the other cuts); the response graph and processing use the same coefficients.
- **Improved spectrum:** precomputed 4096-point Hann window; 75% overlap; display-side log-bin smoothing and separate attack/release; FFT capped at five processed frames per timer callback to protect host UI responsiveness; analyzer AMOUNT changes display opacity, not dB readings.
- **Live UI polish:** selected bands remain reachable at ±6 dB graph range, brighter numbered EQ handles, peak clip LEDs, clearer header, and updated version identification.
- **Preset correctness:** A/B snapshot restoration discards stale prior snapshots before importing validated snapshots; all existing parameter IDs and plugin identifiers remain unchanged for DAW compatibility.
- **Automated regression tests:** `SkellyDspSmoke` compiles the production biquad header with a minimal JUCE-math shim and checks unity response, filter cutoff behavior, coefficient-ramp stability, near-Nyquist stability, and Tilt response. GitHub Actions now runs `ctest` on Windows and macOS.

### Important, still unfinished

- **True linear-phase processing is not implemented.** The `phaseMode` parameter is retained so existing project states load, but all three values currently execute the minimum-phase processing path. The UI identifies the Linear setting as unavailable; do not use it for mastering when linear phase is required.
- The two other phase labels also share one DSP path: there is no lookahead and reported latency is zero samples. This is not a different zero-phase filter.
- Analyzer display is mono-summed for visualization; it does not provide separate L/R, Mid, or Side spectrums.
- Coefficient slewing reduces parameter zipper noise but abrupt filter-type, slope, and channel changes are not yet dual-path crossfaded and may still click on challenging content.
- The built-in last-resort ±32 safety clamp is **not** a proper limiter. Use safe input and output levels.
- JUCE/FL Studio must be tested after building: scan/load, editor open-close 10×, bypass/solo, save/reopen, playback, automation, and CPU load at 44.1/48/96 kHz and small audio buffers.

### Portable DSP tests

On Linux or macOS with a C++17 compiler (these tests do not need JUCE):

```sh
c++ -std=c++17 -O2 -I tests/juce_stub tests/dsp_smoke.cpp -o skelly_dsp_smoke
./skelly_dsp_smoke
```

Or use `cmake --build build --config Release` and `ctest --test-dir build -C Release --output-on-failure` after configuring the JUCE project.

### Editor fallbacks

The default is the restored interface. For crash diagnosis, pass `-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON`. For a minimal diagnostic window, pass `-DSKELLY_DIAGNOSTIC_EDITOR=ON`. Do not overwrite the last known good V1.4 install until V2.0 has passed FL Studio testing.

---

## Build the VST3

For a Windows MSVC machine with JUCE 8.0.12 downloaded to `C:/JUCE`:

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

For macOS with Xcode:

```sh
cmake -S . -B build -G Xcode -DJUCE_PATH=/path/to/JUCE
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

If `JUCE_PATH` is not provided, CMake FetchContent attempts to download the pinned JUCE 8.0.12 release. Windows artifacts normally appear under `build/SkellyEQ_artefacts/Release/VST3` and `build/SkellyEQ_artefacts/Release/Standalone`. Exact folders may vary with JUCE and platform.

The VST3 manufacturer/plugin IDs and the complete automatable parameter layout are preserved from V1.9 to avoid breaking existing DAW sessions. The plugin's state retains the selected band and A/B snapshots. All other features and historical notes are documented in `HISTORY_V1_X.md`.

Read `V200_RELEASE_NOTES.md` before using this source in an important FL Studio session.
