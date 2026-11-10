"""Skelly EQ 2.6 static UI/build contract (runs without JUCE)."""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
ui = (root / 'Source/RestoredEditor.cpp').read_text(encoding='utf-8')
header = (root / 'Source/RestoredEditor.h').read_text(encoding='utf-8')
dsp = (root / 'Source/dsp/BiquadCascade.h').read_text(encoding='utf-8')
processor = (root / 'Source/PluginProcessor.cpp').read_text(encoding='utf-8')
cmake = (root / 'CMakeLists.txt').read_text(encoding='utf-8')
assert 'VERSION 2.6.0' in cmake
assert 'setSize(1070, 529)' in ui
assert 'constexpr float baseW = 1070.0f' in ui
assert 'constexpr float baseH = 529.0f' in ui
assert 'setFixedAspectRatio' in ui and 'setResizable(true, true)' in ui
assert 'stopTimer();' in ui and 'graph.onSelection = nullptr;' in ui
assert 'setLookAndFeel(nullptr);' in ui
assert 'setSize(1338, 661)' not in ui
assert 'formatFrequency(frequency)' in ui and 'formatDecibels(gain)' in ui
assert 'formatQ(q)' in ui and 'formatPercent(analyzeAmount)' in ui
assert 'juce::Graphics::ScopedSaveState' in ui
assert 'loadArtwork("frame")' in ui and 'loadArtwork("skull_emblem")' in ui
assert 'graph.repaint()' in ui
assert 'juce::dsp::FFT fft{12}' in header
assert 'fft.performFrequencyOnlyForwardTransform' in ui
assert 'h * (0.94f - normalized * 0.82f)' in ui
assert 'freq >= sr * .5f' in ui
assert 'nyquist < 20000.0' in ui
assert 'cachedLaneA' in header and 'cachedLaneB' in header
assert 'lanesDiffer' in header and 'anySolo && !band.solo' in ui
assert 'routesToLane(band, stereoMode, ch)' in ui
assert 'routesToLane(band,mode,lane)' in processor
assert 'beginDragParam(param(dragged, "freq"))' in ui
assert 'gainGestureParam->endChangeGesture()' in ui
assert 'snapshotStatus = available ? "A RECALL" : "A EMPTY"' in ui
assert 'snapshotStatus = available ? "B RECALL" : "B EMPTY"' in ui
assert 'SKELLY_RESTORED_EDITOR' in cmake and 'SKELLY_SAFE_EDITOR' in cmake
assert 'state.replaceState(clean)' in processor and 'rampSamples' in dsp
assert 'class WetDryRamp' in dsp and 'wet[i][lane].next()' in processor
assert 'bypassRampSamples' in processor
assert 'retiredFilters' in processor and 'topology[i].next()' in processor
assert 'topologyRampSamples' in processor
assert 'updateBandControlStates();' in ui and 'SkellyBandTab' in ui
assert 'filterUsesGain' in dsp
linear = (root / 'Source/dsp/LinearPhaseFIR.h').read_text(encoding='utf-8')
assert 'impulseSize = 2049' in linear and 'latency = groupDelay + partition' in linear
assert 'std::array<std::array<Spectrum, partitions>, 4>' in linear
assert 'std::array<bool, 4> active' in linear
assert 'if (!kernel.active[(size_t)matrix]) continue;' in linear
assert 'for (int bin=0;bin<=partition;++bin)' in linear
assert 'result[(size_t)(fftSize-bin)] = std::conj' in linear
assert (root / 'tests/linear_phase_reference.cpp').exists()
assert 'q.setDoubleClickReturnValue(true, p->convertFrom0to1(p->getDefaultValue()))' in ui
assert 'setParam(i, "type", 0.f)' in ui
assert 'buildKernel' in linear and 'calculateBlock' in linear
assert 'published.compare_exchange_strong(expected,2' in linear
assert 'processMinimumSample' in processor and 'stereoFade.next()' in processor
assert 'StereoLinearFIR::buildKernel' in processor
assert 'setLatencySamples(initialPhase==0 ? StereoLinearFIR::latency : 0)' in processor
assert 'triggerAsyncUpdate()' in processor
assert '"LINEAR FIR"' in ui and '"LINEAR N/A"' not in ui

# All explicit JUCE component coordinates in resized() must stay inside the
# 1070x529 compact canvas, and clickable components must not overlap.
layout = ui.split('void SkellyEQRestoredEditor::resized()')[1].split(
    'void SkellyEQRestoredEditor::timerCallback()')[0]
rectangles = {}
for name, x, y, w, h in re.findall(
    r'put\(\s*([A-Za-z_]+(?:\[\d+\])?)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*\)',
    layout):
    rectangles[name] = tuple(map(int, (x, y, w, h)))
assert len(rectangles) >= 29, len(rectangles)
for name, (x, y, w, h) in rectangles.items():
    assert x >= 0 and y >= 0 and x + w <= 1070 and y + h <= 529, (name, x,y,w,h)
    assert w >= 19 and h >= 19, name

# Band buttons are positioned by the for loop (not captured by the regexp).
assert '35.f + 31.f * b' in layout and 'put(bandTabs' in layout
for n in range(8):
    rectangles[f'bandTabs[{n}]'] = (35 + 31*n, 490, 28, 22)

names = list(rectangles)
for i, one in enumerate(names):
    x,y,w,h = rectangles[one]
    for other in names[i+1:]:
        X,Y,W,H = rectangles[other]
        dx = max(0, min(x+w, X+W) - max(x,X))
        dy = max(0, min(y+h, Y+H) - max(y,Y))
        assert dx * dy == 0, f'Click overlap: {one} vs {other}'

# Resize invariants: scaled and integer-rounded control hitboxes must
# remain in bounds and nonoverlapping at minimum/default/maximum window sizes.
# This is not a screenshot test; the Windows/FL Studio host must still render.
for canvas_width, canvas_height in ((856,423), (1070,529), (1605,794)):
    scaled = {}
    for name, (x,y,w,h) in rectangles.items():
        x0 = round(x * canvas_width / 1070)
        y0 = round(y * canvas_height / 529)
        x1 = round((x+w) * canvas_width / 1070)
        y1 = round((y+h) * canvas_height / 529)
        assert x0>=0 and y0>=0 and x1<=canvas_width and y1<=canvas_height, (canvas_width,name)
        assert x1>x0 and y1>y0, (canvas_width, name)
        scaled[name] = (x0,y0,x1,y1)
    keys = list(scaled)
    for i, name in enumerate(keys):
        x0,y0,x1,y1=scaled[name]
        for other in keys[i+1:]:
            X0,Y0,X1,Y1=scaled[other]
            overlap_w=max(0,min(x1,X1)-max(x0,X0))
            overlap_h=max(0,min(y1,Y1)-max(y0,Y0))
            assert overlap_w*overlap_h == 0, (
                'scaled clickable overlap', canvas_width, name, other)

# UI canvas contains a clear plot, independently rendered rail, and deck.
x,y,w,h=rectangles['graph']
assert (x,y,w,h) == (50,120,836,274)
assert x+w < rectangles['typeButtons[0]'][0]
for name in ('typeSelect', 'channelSelect', 'phaseSelect', 'slopeSelect'):
    x, y, w, h = rectangles[name]
    assert 909 <= x < 1052 and x+w <= 1052 and 316 <= y < 418, name

for asset in ('frame.png', 'graph_bg.png', 'knob_cap.png', 'skull_emblem.png'):
    path = root / 'Assets' / asset
    data = path.read_bytes()
    assert len(data) > 1000 and data[:8] == b'\x89PNG\r\n\x1a\n', asset
    width, height = struct.unpack('>II', data[16:24]); assert width > 0 and height > 0, asset

# A conservative guard against accidental syntactic truncation of C++ source.
for cpp in (root / 'Source').rglob('*.cpp'):
    code = cpp.read_text(encoding='utf-8')
    scrub = re.sub(r'//[^\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"', '', code, flags=re.S)
    depth = 0
    for char in scrub:
        if char == '{': depth += 1
        elif char == '}': depth -= 1
        assert depth >= 0, f'brace underflow {cpp}'
    assert depth == 0, f'brace mismatch {cpp}'

print('PASS: V2.6 compact UI, stereo crossfade, FIR/PDC, routing, assets and presets')
