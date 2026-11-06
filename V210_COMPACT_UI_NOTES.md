# Skelly EQ V2.1 — Compact GUI and FL Studio test plan

**Primary change:** the default editor is **1070 × 529** instead of the original 1784 × 882 design canvas (a 40% reduction of each dimension). The original V2.0 editor's default was 1338 × 661; V2.1 now uses a new native 1070px design, **not** a 60%-scaled rendering of the old interface.

- Complete redraw of the editor's placement grid, compact header, side panel, graph toolbar and bottom deck. Actual controls are still JUCE widgets, not a screenshot used as a control background.
- Labels, graph axes, values and JUCE widgets use compact-design sizes for improved readability instead of scaling 1784px text down to 60%.
- Enlarged the useful graph area relative to the window. The real-time FFT is computed on the message thread, not the audio callback; unchanged from V2.0.
- Fixed side-panel placement for filter shapes, type, routing, phase and slope; mode, analyzer and level are in the status row under the plot.
- Added explicit per-group geometry assertions to `scripts/check_v21.py`; added the script to the CI workflow. The previous V2.0 sanity checker is retained as a historical file only.
- Continued use of the original stable processor/parameter ID set. The previous safe editor remains available with `-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON`.

## Limitations and testing

This is **source**, not a Windows-built VST3. The portable DSP test and static geometry checks do not certify FL Studio stability, DAW automation, popup behavior, or visual rendering. Run the following in a fresh blank FL Studio project before installing over a working VST3:

1. Confirm the editor opens at approximately **1070 × 529** (subject to OS display scaling).
2. Verify all eight EQ nodes are draggable, the band buttons work, readouts show Hz/dB/Q, and controls don't overlap or clip.
3. Test changing phase, slope, filter type, band routing, M/S, FFT mode and amount.
4. Open/close the editor 10 times, remove/reload the plugin, save and reopen the project.
5. Automate a frequency, gain and Q control, and test playback at 44.1k/48k/96k with 64/128/512-sample blocks.
6. Confirm the plugin version label reads `v2.1`. If not, remove the previous plugin binary and rescan FL Studio's plugin list.

**Not addressed:** true linear phase is still unsupported; the UI's `LINEAR N/A` entry retains preset compatibility. Abrupt filter-type switches are not dual-path crossfaded.
