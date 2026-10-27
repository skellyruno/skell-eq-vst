# Skelly EQ V1.8 — FL Studio visual & crash regression

1. Exit FL Studio completely, replace only the installed VST3, then rescan.
2. In a blank project, verify the header says **v1.8** and check the graph has no rectangular black overlay.
3. Resize between about 892 x 441 and 1784 x 882. Confirm the ACTIVE/SOLO/RESET CURVE row remains above the graph and the eight nodes are visible.
4. Select several bands. Verify FREQUENCY shows values such as `328 Hz` or `5.14 kHz`, GAIN shows `+5.5 dB`, Q shows `1.00`, and output shows `0.0 dB`, rather than long unformatted decimals.
5. Test mouse-drag frequency/gain, mouse-wheel Q, enabled/solo, shelf/cut/notch selection, phase/slope dropdown, A/B snapshots and input audio analyzer.
6. Close the editor, reopen it five times, save a project, exit, reload the project, then remove the plugin. Note any warnings or crashes.
7. If V1.8 fails, use `-DSKELLY_RESTORED_EDITOR=OFF -DSKELLY_SAFE_EDITOR=ON` and attach the FL Studio crash report plus build logs.

This source ZIP is not a compiled Windows binary and has not been tested inside FL Studio. The Linear phase choice remains a placeholder; only the zero-latency min-phase DSP is implemented.
