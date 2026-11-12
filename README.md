# Skelly EQ V2.7

Skelly EQ is an eight-band JUCE 8 VST3/Standalone parametric equalizer with a resizable **1070×529** neon-green skull interface, a 4096-point real-time visual analyzer, L/R and Mid/Side band routing, A/B snapshots and host-automatable controls. The plugin identifiers, automation IDs and state format are preserved from earlier versions. The FIR streaming history is cleared when returning from the minimum-phase engine, preventing replay of old buffered audio.

## Audio paths

- **Minimum Phase / Zero Latency:** double-precision TDF-II biquad cascades. Frequency/gain/Q coefficient ramps (8 ms), band bypass/Solo/routing ramps (6 ms), filter type/cut slope morphs (12 ms), and L/R↔M/S crossfades (20 ms). Reports zero algorithmic latency.
- **Linear FIR:** 2049-tap symmetric FIR, 128-sample partitioned FFT convolution, **1152 samples reported latency** (24 ms at 48 kHz). Kernels are generated off the audio thread, published with a lock-free handoff and crossfaded while processing. V2.7 skips redundant cross-channel matrices and FFT conjugate bins to reduce CPU work. L/R↔M/S automation produces updated FIR kernels rather than using the IIR mode-switch crossfade.
- Output level metering has independent L/R peak and RMS readings. The last-resort ±32 host-safety clamp is **not a mastering limiter**; excessive boosts can still clip the DAW output.

## Testing

After setting up a C++17 compiler, the portable DSP tests require no JUCE download:

```sh
python scripts/check_v26.py
c++ -O2 -std=c++17 -I tests/juce_stub tests/dsp_smoke.cpp -o skelly_smoke && ./skelly_smoke
c++ -O2 -std=c++17 -I tests/juce_stub tests/linear_phase.cpp -o skelly_linear && ./skelly_linear
c++ -O2 -std=c++17 -I tests/juce_stub tests/linear_phase_reference.cpp -o skelly_reference && ./skelly_reference
```

The reference test checks the FIR's actual stereo output against independent direct convolution, frequency response, and PDC timing across **44.1/48/96 kHz** in both L/R and M/S modes. The Windows/macOS GitHub Actions workflow builds the plugin and runs all three tests, including the FIR re-entry regression. The Windows artifact includes one VST3 bundle instead of a ZIP nested inside another ZIP.

## Windows build

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

## macOS build

```sh
cmake -S . -B build -G Xcode -DJUCE_PATH=/path/to/JUCE
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

If `JUCE_PATH` is omitted, CMake fetches the pinned JUCE **8.0.12** source. The Windows VST3 normally builds beneath `build/SkellyEQ_artefacts/Release/VST3/`.

To isolate crashes, configure `-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON` for the simplified editor, or `-DSKELLY_DIAGNOSTIC_EDITOR=ON` for the minimal diagnostic UI. The restored editor is the normal default.

## Current limitations and safety

- The fixed-length linear-phase FIR approximates very narrow/low-frequency filters, particularly at 96 kHz. Its latency changes when switching phase modes; FL Studio might resynchronize and cannot be guaranteed glitch-free in all host situations.
- Source-level UI geometry tests and successful CI builds do **not** prove in-host graphical behavior, automation timing or FL Studio open/close stability. Verify those in a blank FL Studio project before relying on the new VST3 in production.
- Keep your last working VST3 as a rollback. Re-scan after replacing the plugin to avoid duplicate versions.

See `V270_RELEASE_NOTES.md` for the latest implementation notes and `HISTORY_V1_X.md` / `V2xx_RELEASE_NOTES.md` for prior decisions and fixes.

## Optional SkellWorld presence

Open **OPTION → Join SkellWorld (presence only)** to show this Skelly EQ editor instance in the local SkellWorld dashboard, if the hub is running. To disconnect choose **OPTION → Leave SkellWorld**. This is **off by default**, disconnects when the editor closes, and does not affect the EQ audio thread, latency, APVTS parameter IDs or saved presets. The WorldLink lives on its own thread and sends only product ID, display name, and temporary instance ID—never audio, EQ curve settings or files.

This is a source integration pilot requiring Windows VST3 CI and FL Studio host validation.
