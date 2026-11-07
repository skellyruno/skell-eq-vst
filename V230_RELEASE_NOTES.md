# Skelly EQ V2.3 - transition smoothing + reliability pass

This update preserves the previous compact 1070 x 529 layout, plugin identifiers, saved-state schema, and JUCE fallback editor. The audio-thread processing path now applies sample-accurate **6 ms wet/dry ramps per EQ band per processing lane** when enable, solo, or band-channel routing changes. A fully bypassed lane skips filters for CPU efficiency.

## How to test in FL Studio

1. Install the V2.3 Windows VST3 from the successful GitHub Actions artifact. Keep your last working VST3 backed up.
2. On a blank project, open/close the plugin repeatedly and test stereo audio at 44.1/48/96 kHz.
3. Toggle ON and SOLO on each band during playback; verify transitions are significantly smoother and without hard clicks.
4. Change selected band's channel assignment (Stereo/Left/Right, then Mid/Side in M/S mode). Check L/R integrity, save/reopen, and automation playback.
5. Test varying block sizes and start/stop transport. Send an audio capture or log for any artifacts.

## Limitations

- Filter **type/slope changes** still reconfigure biquad topology abruptly; they are not dual-engine crossfaded.
- Switching the **global stereo mode** L/R to M/S can still change the signal transform without whole-engine crossfade.
- **Linear phase** is not implemented; the audio engine remains minimum-phase in all phase parameter positions. The UI marks Linear unavailable.
- Portable C++ tests verify filter behavior and ramp invariants; a Windows/macOS host regression test is still required.
