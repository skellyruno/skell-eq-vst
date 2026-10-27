# Skelly EQ 1.7.0 — FL Studio regression checklist

1. Keep a backup of v1.4, the last version confirmed working by the user. Do not overwrite your only working project or VST3.
2. Build the new source from `main` (or locally). Confirm the artifact is version **1.7.0** and the compile succeeded with no C++ errors.
3. Exit FL Studio, install the *new* `.vst3` bundle and rescan it. Do not keep duplicate plugin bundles with the same ID in FL Studio's scan paths.
4. In an empty project: insert Skelly EQ; inspect graph band handles and labels, resize UI, open/close the window 10 times, and remove the plugin.
5. Play pink noise or a track and toggle analyzer OFF/PRE/POST; watch meters, change FREQ/GAIN/Q and graph handles, and stop playback.
6. Test all filter shapes, slope, Stereo/M-S, per-band channel, bypass/solo and A/B SAVE/LOAD.
7. Save a test project, close FL Studio, reopen it, and verify the sound, band parameters, snapshots, and UI can load without a Fruity Wrapper warning.
8. If the plugin crashes, retry an *independent* build with `-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON`. If safe works but restored crashes, enable lifecycle tracing on a test build and share the trace, Windows crash dump, and GitHub Actions log.

Known limitations: minimum-phase audio engine in all three phase settings; no measured latency-versus-Pro-Q4 equivalence; combined graph is not a per-channel plot. The analyzer runs its FFT on JUCE's message thread, not the audio callback.
