# Skelly EQ V2.7

Skelly EQ is an eight-band JUCE 8 Windows VST3 parametric equalizer with a resizable **1070×529** neon-green skull interface, a 4096-point real-time visual analyzer, L/R and Mid/Side band routing, A/B snapshots and host-automatable controls. The plugin identifiers, automation IDs and state format are preserved from earlier versions. The FIR streaming history is cleared when returning from the minimum-phase engine, preventing replay of old buffered audio.

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

The reference test checks the FIR's actual stereo output against independent direct convolution, frequency response, and PDC timing across **44.1/48/96 kHz** in both L/R and M/S modes. The Windows GitHub Actions workflow builds the plugin and runs all three tests, including the FIR re-entry regression. The Windows artifact includes one VST3 bundle instead of a ZIP nested inside another ZIP.

## Windows build

```powershell
cmake -S . -B build -A x64 -DJUCE_PATH="C:/JUCE"
cmake --build build --config Release --parallel 2
ctest --test-dir build -C Release --output-on-failure
```

## Release format

The supported distributable is one Windows x64 VST3 bundle. CMake intentionally produces VST3 only; Standalone and macOS targets are not part of this release line. The Windows release workflow normalizes JUCE-generated moduleinfo.json to strict JSON, checks plugin identity and class IDs, verifies the PE32+ AMD64 binary, runs pluginval strictness 5, and publishes a flat bundle with SHA-256 provenance. CI artifacts remain preview-only until FL Studio, real audio A/B, target-hardware CPU/latency, licensing/support and rollback gates are accepted.

## Optional SkellWorld presence

Open **v2.7 badge → Join SkellWorld (presence only)** to show this Skelly EQ editor instance in the local SkellWorld dashboard, if the hub is running. To disconnect choose **v2.7 badge → Leave SkellWorld**. This is **off by default**, disconnects when the editor closes, and does not affect the EQ audio thread, latency, APVTS parameter IDs or saved presets. The WorldLink lives on its own thread and sends only product ID, display name, and temporary instance ID—never audio, EQ curve settings or files.

This is a source integration pilot requiring Windows VST3 CI and FL Studio host validation.
