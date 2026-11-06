# Skelly EQ V2.2 release notes

## Scope

This update refines the existing JUCE editor rather than generating a static UI image. The default size stays **1070 × 529**; the fixed aspect ratio is retained. A/B, filter automation IDs, plugin identity and the V1.4 safe editor remain supported.

## Key fixes

- Response cache now incorporates active Solo state, stereo/M-S routing, and lane-specific response curves.
- Route matching shares `SkellyDSP::routesToLane()` between the processor and UI, with portable tests for all channel assignments.
- Mouse gesture transitions no longer prematurely end gain automation gestures when Alt is toggled mid-drag.
- Node selection chooses the nearest target, prioritizing the current band for overlapping nodes.
- Nyquist boundary is displayed clearly at low sample rates; invalid high-frequency analyzer bins show the floor.
- A/B snapshot buttons show brief status feedback, and indicate an empty slot instead of claiming success.
- Source checks cover all directly placed controls, collision geometry, required assets and DSP/UI invariants.

## Limitations

Full JUCE / Windows MSVC compile and FL Studio integration were not run locally because JUCE sources are not available in this environment. Only standalone DSP tests and source/asset checks can run here. Linear phase and advanced filter-topology crossfading are not implemented. Do not treat the last-resort safety clamp as a mastering limiter.

## CI

GitHub Actions runs `scripts/check_v22.py`, configures JUCE 8.0.12, builds VST3 and Standalone, and runs the portable CTest suite. Windows and macOS artifact names include V2.2.
