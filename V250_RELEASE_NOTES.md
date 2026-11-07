# Skelly EQ V2.5 — True Linear Phase + L/R↔M/S Crossfade

## Linear phase (real DSP; not an IIR alias)

- **2049-tap symmetric Blackman-window FIR**, frequency-sampled from the eight EQ-band magnitude responses. Response is symmetric about tap 1024, so phase delay is constant across frequency.
- **128-sample uniform-partitioned FFT convolution** (256-point FFT per partition), four stereo matrix kernels (LL/LR/RL/RR), with correct Mid/Side routing including Mid/Side-only EQ bands.
- **Total VST3 PDC: 1152 samples in Linear mode:** 1024-sample FIR group delay + 128-sample streaming block delay. This is 24 ms at 48 kHz, 26.1 ms at 44.1 kHz, and 12 ms at 96 kHz. Minimum Phase and Zero Latency modes report **0 samples**.
- FIR design, magnitude sampling, FFT kernels and memory allocation run off the audio thread, with a single-worker, lock-free publish/consume protocol. The audio callback only performs already-prepared convolution and a 1024-sample kernel-change crossfade.
- Host latency update is posted to JUCE's message thread via AsyncUpdater on `phaseMode` parameter changes (and on preset restoration), rather than calling host delay compensation directly inside the audio callback.

## Seamless L/R ↔ M/S switching

- **Minimum-phase:** at stereo-mode switch, clone the old biquad filters, coefficients, states, old lane wet/dry ramps and topology morphs. Keep processing old and new stereo transforms in parallel and crossfade their stereo **outputs over 20 ms**, across any host buffer size. A new mode request during the fade is deferred until the current fade completes.
- **Linear-phase:** the worker generates a different four-kernel FIR stereo matrix and the convolution engine crossfades the responses, preserving the full convolver input history to avoid flushing its tail.
- Existing band bypass/solo (6 ms) and filter type/slope (12 ms) transitions are retained.

## UI and compatibility

- Linear phase now reads **LINEAR FIR**, with a tooltip stating its actual FIR size and PDC. A compact footer indicates FIR/IIR processing.
- The same 1070 × 529 resizable neon-green interface, plugin/manufacturer IDs, preset state, and eight-band parameter IDs are preserved.
- The previous working safe editor is still selectable with `SKELLY_RESTORED_EDITOR=OFF`.

## Verification

- `python scripts/check_v25.py`
- `cmake -S . -B build` then `cmake --build build --config Release` and `ctest --test-dir build -C Release --output-on-failure`
- `SkellyLinearPhaseTest` checks sample-exact FIR PDC, symmetric phase, matrix routing and a live kernel morph; `SkellyDspSmoke` checks IIR invariants and transitions.
- Test in FL Studio: (1) new and saved projects, open/close UI repeatedly; (2) 44.1/48/96 kHz; (3) L/R↔M/S during audio and automation; (4) enable Linear FIR, check host latency/PDC with a loopback/reference; (5) save/reopen and verify no phase-mode changes; (6) CPU at 64–1024 sample buffers, multiple instances.

## Limits

- The fixed 2049-tap FIR is a **bounded-quality, low-latency** implementation. Sharp 20–40 Hz filters are approximated rather than reproduced exactly at every sample rate, especially at 96/192 kHz. A longer quality mode is future work.
- Switching between IIR (0 latency) and FIR (1152 latency) during playback triggers host latency reconfiguration; it may cause a short transport resynchronization. **L/R↔M/S within a given phase mode is crossfaded; IIR↔FIR phase-mode switching is not sample-continuous.**
- Rapid, repeated FIR automation faster than the worker/design cadence is coalesced and audibly transitions when a new kernel is published. Verify worst-case response under extreme automation.
- FL Studio runtime stability and CPU performance still require direct host validation even if CI and portable tests pass.
