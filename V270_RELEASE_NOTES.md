# Skelly EQ V2.7 — clean FIR re-entry and stereo lifecycle

- Fix stale audio playback after toggling from Minimum Phase back to Linear FIR. The FIR engine clears its 2,049-tap input and overlap history without touching the concurrently published kernel.
- Rebuild stale IIR filter memories when leaving FIR, avoiding old filter states.
- In Linear mode, L/R↔M/S parameter changes update FIR routing rather than initiating a frozen IIR crossfade.
- Remove the unused min-phase `lastMode` tracking field.
- Add a regression test that processes nonzero stereo, suspends FIR processing, clears FIR history and asserts no old samples leak into subsequent zero-input output.
- Retain V2.6's convolution optimization, independent direct-convolution tests at 44.1/48/96 kHz, and UI fixes.
- Upgrade GitHub Actions to Node 24-compatible `actions/checkout@v6` and `actions/upload-artifact@v6`; do not suppress upstream JUCE/Apple SDK warnings.
- Algorithmic latency remains 1,152 samples for linear FIR and zero for minimum phase. FL Studio may re-synchronize transport on a change to the reported latency. Live phase-mode switching is not guaranteed glitch-free.
- An immediate switch from IIR back to FIR after editing settings can use an older FIR kernel until the background designer publishes the new one; in-host validation remains outstanding.
