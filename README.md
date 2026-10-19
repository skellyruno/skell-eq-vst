# Skelly EQ — JUCE VST3 / Standalone source (initial implementation)

Original green/black industrial eight-band EQ inspired by the supplied user reference image. Assets in `Assets/` are procedurally created in `make_assets.py`, not extracted from another plugin. JUCE 8.0.12, C++17, VST3 + Standalone. Based on the user's minimal skell-vst-template GitHub structure.

## Build

Prerequisites: Git, CMake 3.22+, C++17 compiler, JUCE 8 (JUCE 7+ may require minor adaptation). Internet on the first configure, or use `-DJUCE_PATH=/absolute/path/to/JUCE` (where `CMakeLists.txt` is located).

Windows, Visual Studio Developer Prompt:

```bat
cmake -S . -B build -A x64 -DJUCE_PATH=C:/JUCE
cmake --build build --config Release --parallel 2
```

macOS:

```sh
cmake -S . -B build -G Xcode -DJUCE_PATH=$HOME/JUCE
cmake --build build --config Release --parallel 2
```

Typical output: `build/SkellyEQ_artefacts/Release/VST3/Skelly EQ.vst3` and `build/SkellyEQ_artefacts/Release/Standalone/` (actual plugin filenames and directories vary by platform). The GitHub Actions workflow builds and uploads Windows and macOS artifacts.

## Current implementation

- Eight automatable parametric bands: peak, shelves, Butterworth low/high cut up to 48 dB/oct, notch, band pass and an **approximate** tilt mode.
- Cascaded double-precision TDF-II biquads. Stereo L/R and M/S routing. Note: mixed channel-assignments in stereo/M/S are a first-pass implementation.
- Analyzer: 4096-point Hann-window FFT evaluated by editor timer, with a single-producer/single-consumer audio FIFO. Configurable Off/Pre/Post/On (On currently uses Post path).
- A green vector response graph, drag-to-edit nodes, Q via wheel or Alt-drag, right-click filter menu, double-click deletion/creation in a free slot, meter panel, and host-automatable knob controls.
- Parameters and simple A/B snapshots saved using APVTS ValueTree; resizable fixed-aspect-ratio UI.
- Original procedurally generated frame, central skull, graph texture, cap illustration. Fonts use OS fallback. The image and controls are intended as an editable starting point, **not a pixel-perfect replica**.

## Explicit limitations / work remaining

**This is source deliverable, not a verified release binary.** No local JUCE SDK was installed and network DNS was unavailable during the attempted CMake configure, so C++ build validation could not be completed in this environment. Use GitHub Actions or a local JUCE checkout to compile and report any errors.

1. Phase control is surfaced and automatable, but **Linear, Min Phase and Zero Latency currently share the same minimum-phase IIR signal path**. A true linear-phase FIR with background coefficient generation, partitioned convolution, crossfade and host latency reporting is not yet implemented. The placeholder `Source/dsp/LinearPhaseFIR.h` documents the planned stage. **Do not use the Linear setting assuming a linear-phase response.**
2. Low/High cut use RBJ designs, not oversampling or a Nyquist-matched bilinear design. Coefficients update per audio block without interpolation; fast automated EQ moves may zipper.
3. A/B is a starter snapshot implementation: the active side must be saved before switching. Other preset/option behaviors are minimal. Right-click text-entry and per-band listen/solo audition are not complete.
4. The analyzer uses a coarse fixed 256-column mapping of FFT bins, not full 1/3-octave smoothing. Its FIFO can drop frames on a stalled UI without touching realtime audio. Graph `graphRange` is stored but rendering is currently fixed to ±18 dB.
5. Source does not embed the specific condensed/rough fonts requested; native font fallback is used. Static glow is drawn with layered vector strokes, not premultiplied blurred image caches. Asset `knob_cap.png` is ready but the knobs are vector-rendered instead.
6. True sample-accurate parameter smoothing, comprehensive platform testing, formal CPU profiling, and oversampling are future iterations.

## Image asset contract

| Asset | Canvas | Alpha | Purpose |
|---|---:|---|---|
| `Assets/frame.png` | 1784×882 | RGBA transparent in graph and right panel interior | Decorative metallic edge trim and rivets |
| `Assets/skull.png` | 250×215 | RGBA transparent | Original hand-drawn spiked skull crest, scaled in header |
| `Assets/graph_bg.png` | 1450×545 | RGBA opaque | Subtle green grunge behind graph |
| `Assets/knob_cap.png` | 96×96 | RGBA transparent | Optional runtime-rotatable metal knob overlay |

Regenerate assets: `python make_assets.py` (requires Pillow). Art PNGs are included and compiled using `juce_add_binary_data`.

## Source organization

`Source/PluginProcessor.*` = parameters, state, audio processing; `Source/PluginEditor.*` = scaled editor, controls, A/B and menu; `Source/dsp/` = biquads, FIFO, linear-FIR placeholder; `Source/ui/` = EQ graph, rotary knob, meters, header, theme UI.
