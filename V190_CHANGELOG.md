# Skelly EQ — v1.9 UI update

## Main user-visible improvements

1. Unified custom JUCE look-and-feel for knobs, slider strips, switches,
   dropdowns, and snapshot/filter/band buttons; no pre-rendered fake controls.
2. A 8-band quick-selector in the bottom strip, alongside the original band
   selection dropdown.
3. Dedicated band inspector above the graph and a selected-band frequency
   vertical guide.
4. Cleaner right-hand meter/filter/phase/slope/analyzer organization.
5. EQ graph remains fully available for dragging; no dropdowns floating on
   top of response nodes.
6. Hz, kHz, dB, Q, and percent formatting preserved after APVTS attachment.
7. Double-click frequency resets to the selected band's stored default.
8. Safe-editor fallback and all previous DSP/state behavior kept intact.

## Known limitations / release gate

- JUCE/CMake source release, not a verified Windows binary.
- Needs a Windows MSVC build and a real FL Studio load/play/save/close test.
- Linear-phase FIR not implemented; UI explicitly identifies the unavailable
  mode and preserves its parameter for backwards compatibility.
- Only the selected band's single nominal combined magnitude curve is shown;
  no separate mid/side or left/right curves.
- Analyzer FFT processing remains in the editor timer, not the audio thread.
