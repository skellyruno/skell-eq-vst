# Skelly EQ V2.6 — linear FIR CPU + GUI reliability

## DSP

- Skip zero matrix crossfeed terms in L/R mode and in M/S mode when both lanes have identical magnitude. This avoids doing unnecessary FFT convolution for signal paths that are identically zero.
- Accumulate the non-redundant half of each real-input FFT spectrum and reconstruct its conjugate half before inverse FFT. No change to impulse length or algorithmic delay.
- Keep the existing 2049-tap symmetric FIR, 128-sample partitions, 1152-sample host-reported PDC, kernel crossfades, and minimum-phase bypass/slope/stereo smoothing.
- New direct-convolution reference test checks actual output samples in L/R and M/S across 44.1, 48 and 96 kHz to a tolerance of 1e-8 and frequency response to 0.7 dB (including 96 kHz window-limited approximation).
- Microbenchmark on the same x86-64 development machine: 48 kHz isolated FIR fell from ~2.15% to ~1.53% of a single CPU core, and 96 kHz from ~4.05% to ~3.20%. These are not FL Studio session CPU measurements.

## UI

- Q knob now resets to its neutral parameter default on double-click after band rebind, rather than erroneously attempting a value of zero.
- Double-clicking empty plot space with a disabled band available creates a Peak node rather than reviving an old notch/cut with unusable gain settings.
- Tooltips show 'Gain N/A' for filters that ignore gain.
- Preserve the compact 1070×529 resizable interface, existing styles/assets, routing, and host automation parameter IDs.

## Build and delivery

- Windows artifact staging verifies the actual VST3 Windows binary and uploads a single-level VST3 bundle (no ZIP-within-ZIP).
- Run source validation plus 3 portable DSP tests in CI (smoke, FIR/PDC, reference convolution).

## Known limitations

- The 2049-tap fixed-length FIR approximates narrow and very-low-frequency shapes, particularly at 96 kHz. Compared with the minimum-phase target, a tested 950 Hz Q2.5 +12 dB band was ~0.63 dB low at 96 kHz using the Blackman window. Avoid implying exact matching for every EQ shape.
- Host latency changes when switching into or out of FIR can involve FL Studio transport resynchronization, not guaranteed click-free phase-mode switching.
- Full FL Studio in-host open/close, automation and visual rendering tests require the Windows host; GitHub build and static layout checks do not substitute for that.
