# Skelly EQ V2.4 — filter morph & compact UI refinement

## DSP
- **12 ms old/new filter-state crossfade** for changing EQ filter type or cut slope. The previous TDF-II filters run briefly as the new coefficients and memory start; this avoids hard transitions and improves audio smoothness while automating filter shapes.
- Only cut filters react to the global slope control. Peak/shelf/tilt stages no longer unnecessarily reset when slope changes.
- Fully bypassed lanes still skip filter DSP; wet/dry automation smoothing from V2.3 is preserved.
- 8 bands, same plugin IDs and parameter IDs, 0-sample reported latency.

## Compact UI
- Cut, notch and band-pass nodes now display and drag at **0 dB** (their saved gain value remains untouched for host compatibility).
- Gain knob is disabled for those gainless filter modes; a label and tooltip explain why.
- Band selector shows clearer amber selection, Solo/bypassed status and tooltips.
- Kept the 1070×529 resizable design, artwork layers, spectrum analyzer, clean meter rail, and original safe editor fallback.

## Verification and limitations
- Run `python scripts/check_v24.py`, `ctest`, and a 48k/96k host test including repeated type/slope automation and plugin open/close cycles.
- Rapid multiple filter-type changes during one morph window still restart from the latest active filter bank; pathological fast type switching can produce small discontinuities.
- Global L/R↔M/S switching is not yet crossfaded and the Linear choice remains unavailable (minimum-phase processing only).
- JUCE/MSVC compilation and FL Studio runtime must pass separately from portable DSP and static checks.
