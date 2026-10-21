# Skelly EQ 1.7.0 — Rebuilt GUI / FL Studio test build

An eight-band, stereo VST3 + Standalone parametric EQ built with JUCE 8 and CMake. The default editor is a neon-green industrial skin with a separate skull emblem and **real JUCE controls**. It no longer uses a complete screenshot as the UI background, so control labels, grid ticks, and button text cannot be drawn twice.

## Changes since v1.6

- Replaced the screenshot skin with a clean frame and a separate newly created skull emblem.
- Corrected the frequency graph, scale labels, sidebar, band controls, status header and bottom strip layout.
- Restored a 4096-point Hann-windowed spectrum analyzer. Only sample collection occurs on the audio thread; FFT calculation occurs in the editor's timer (24 Hz). OFF, PRE, POST, and ON modes. (ON currently uses post-EQ data.)
- Improved band-handle visibility (numbers on active handles; amber outline for selected, dim ring for bypassed), mouse-wheel Q and host automation gestures for drags.
- Added active-band SOLO, per-band channel assignment, selected-filter indication, and basic SAVE/LOAD A and B snapshot buttons.
- Corrected snapshot storage to avoid nested copies and restored validation/persistence for saved snapshots.
- Kept v1.4 processor algorithms, parameter IDs, state parsing rules, and fallback editor for compatibility with FL Studio projects.
- Lifecycle file tracing is **OFF by default** for normal builds; can be enabled explicitly to troubleshoot.

## Important limitations

This is an **unverified source release**, not a tested Windows binary. A successful ZIP checksum/static source review is not a substitute for an MSVC compile and real FL Studio testing. `phaseMode` offers three preset values for backward compatibility, but the DSP is *currently minimum phase in all modes*. **True linear-phase convolution is not implemented**, and the 'LINEAR (N/A)' UI label makes this limitation explicit. The shown combined response is a single nominal aggregate curve, not a channel-specific L/R or M/S split plot. Biquad coefficients update at block boundaries, so aggressive automation may create zipper noise. No performance comparison to FabFilter Pro-Q has been completed.

## Windows — Visual Studio 2026 + GitHub Actions

Push the **contents of this `SkellyEQ/` folder** into the repository root, including `.github/workflows/build.yml` and `Assets/`. On each push to `main`, GitHub Actions builds Windows VST3 and macOS VST3 artifacts (JUCE fetched from GitHub). Follow Actions -> latest **Build Skelly EQ** -> Windows artifact.

Local build (JUCE 8.0.12 or compatible path):

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/your/checkout/JUCE" -DSKELLY_RESTORED_EDITOR=ON
cmake --build build --config Release --parallel 2
```

Typical Windows VST3 location: `build/SkellyEQ_artefacts/Release/VST3/Skelly EQ.vst3` (exact path may vary). After replacing a VST3, restart FL Studio and run the plugin manager rescan to avoid testing a cached older binary.

Safe-editor fallback if the rebuilt editor fails:

```powershell
cmake -S . -B build-safe -A x64 -DJUCE_PATH="C:/your/checkout/JUCE" -DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON
cmake --build build-safe --config Release --parallel 2
```

Diagnostic log option (do **not** enable by default): `-DSKELLY_TRACE_LIFECYCLE=ON`. Writes checkpoints to `%APPDATA%/SkellyAudio/SkellyEQ-v1.7-trace.txt`.

## Controls

Drag one of the eight band handles to change frequency and gain. Shift-drag is fine adjust; Control constrains to the primary direction; Alt-drag changes Q; scroll-wheel over a handle changes Q. Double-click a handle to bypass/enable its band. Double-click empty graph space to turn on and place an unused band. The selected band has editable FREQ, GAIN, Q, FILTER, CHANNEL, ACTIVE, and SOLO controls.

Use SAVE A/B to record a snapshot and LOAD A/B to recall. RESET CURVE overwrites all eight bands with an example preset; it never activates on load automatically. Analyzer modes are in the right pane.

## FL Studio release checklist

Read `V170_FL_STUDIO_TEST.md` before using in an existing project. Test plugin scan -> add to blank project -> open/close 10× -> play audio -> edit nodes -> project save/reopen -> A/B persistence -> close and remove plugin. Compare with the v1.4 fallback if instability returns.
